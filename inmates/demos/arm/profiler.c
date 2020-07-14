/*
 * Jailhouse, a Linux-based partitioning hypervisor
 * 
 * DDR Profiling inmate for NXP S32V234
 * 
 * Copyright (c) Boston University
 *
 * Authors:
 *  Renato Mancuso <rmancuso@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <inmate.h>
#include <types.h>
#include <mach.h>

#include "profiler.h"

/* ================= FROM DOCUMENTATION ==================
 *
 * MMDC_MADPCR1 -> controls profiling AXI ID filtering
 * fields: PRF_AXI_ID -> bits of the AXI ID to match
 *         PRF_AXI_ID_MASK -> do-care/dont-case bits in AXI ID
 * NOTE: matching table at page 1216 of the S32V234 TRM
 * 
 * MMDC_MADPCR0 -> main profilig control register
 * fields: DBG_EN -> global profiling enable
 *         PRF_FRZ -> to stop/freeze profiling (clear to unfreeze)
 *         DBG_RST -> reset performance counters
 *         CYC_OVF -> signals occurrence of an overflow
 *
 * MMDC_MADPSR0 -> total profiling cycles
 * MMDC_MADPSR1 -> busy cycles in the DDR machinery
 * MMDC_MADPSR2 -> total number of read transactions
 * MMDC_MADPSR3 -> total number of write transactions
 * MMDC_MADPSR4 -> total number of bytes read
 * MMDC_MADPSR5 -> tital number of bytes written
 *
 **********************************************************/

/* From S32V234 Memory Map */
#define MMDC0_BASE 0x40036000
#define MMDC1_BASE 0x400A2000

/* From S32V234 TRM */
#define MMDC_MADPCR0 0x410
#define MMDC_MADPCR1 0x414

#define MMDC_MADPSR0 0x418
#define MMDC_MADPSR1 0x41C
#define MMDC_MADPSR2 0x420
#define MMDC_MADPSR3 0x424
#define MMDC_MADPSR4 0x428
#define MMDC_MADPSR5 0x42C

#define DBG_EN       (1 << 0)
#define DBG_RST      (1 << 1)
#define CYC_OVF      (1 << 3)

/* ARMv8 PMU Control */
#define ARMV8_PMCR_E            (1 << 0)  /* Enable all counters */
#define ARMV8_PMCR_P            (1 << 1)  /* Reset all counters */
#define ARMV8_PMCR_C            (1 << 2)  /* Cycle counter reset */
#define ARMV8_PMUSERENR_EN      (1 << 0)  /* EL0 access enable */
#define ARMV8_PMUSERENR_CR      (1 << 2)  /* Cycle counter read enable */
#define ARMV8_PMUSERENR_ER      (1 << 3)  /* Event counter read enable */
#define ARMV8_PMCNTENSET_EL0_EN (1 << 31) /* Performance Monitors Count Enable Set register */

/* The following address is statically configured in the cell config
 * file. */
#define LOG_MEM_START     (CONFIG_ADDL_REGION)
#define LOG_MEM_END       (CONFIG_ADDL_REGION + CONFIG_ADDL_REGION_SIZE)

/* Suppressing the prints is a good idea in production */
#define NO_PRINTS

#ifdef NO_PRINTS
//#define printk(fmt, ...) ((void)0)
#define printk(fmt, ...) (printk("."))
#endif

volatile struct config * ctrl = (struct config *)LOG_MEM_START;
volatile struct sample * log;

void acquire_samples(unsigned long available);
static void arm_v8_timing_init(void);
static inline unsigned long arm_v8_get_timing(void);

void inmate_main(void)
{
	unsigned long entries;
	unsigned long start;
	
	log = (struct sample *)(((void *)ctrl) + sizeof(struct config));	
	entries = (LOG_MEM_END - (unsigned long)log) / sizeof(struct sample);

	/* Initilize performance counters */
	arm_v8_timing_init();
	start = arm_v8_get_timing();
	
	printk("\n===== STARTING PROFILING CELL =====\n");
	printk("\nS32V234 Profiling Cell Started.\n"
	       ">> Available log entries: %d\n"
	       ">> Log start address: %p\n"
	       ">> Start time is %d\n", entries, log, start);
	
	/* First off, reset config memory */
	ctrl->control = PROF_SIGNATURE;
	ctrl->axi_value = 0;
	ctrl->axi_mask = 0;
	ctrl->count = 0;
	
	while(1) {
		ctrl->control |= PROF_SIGNATURE;

		while((ctrl->control & PROF_ENABLED) == 0);

		printk("Profiling started. Config. = 0x%08lx\n", ctrl->control);

		/* Will return only with buffer full or profiling stopped */
		acquire_samples(entries);

		/* If the buffer is full and autostop selected, stop sample acquisition  */
		if (ctrl->control & PROF_AUTOSTOP)
			ctrl->control &= ~(PROF_ENABLED);
		
	}
	/* lr should be 0, so a return will go back to the reset vector */
}

void acquire_samples(unsigned long available)
{
	/* Init all the helper variables to direct sampling  */
	unsigned long now, next;
		
	/* Number of clock cycles that need to elapse between samples */
	unsigned long interval = PROF_INTERVAL(ctrl->control);

	/* Shall we use number of transactions or bytes? */
	uint16_t read_off, write_off;
	
	/* Pointer to target MMDCx registers */
	void * base;

	/* Pointer to current sample */
	volatile struct sample * cur = log;

	/* Detect which registers to use for sampling */
	printk("Reading bytes count? %d\n", (ctrl->control & PROF_BYTES) >> 3);
	if (ctrl->control & PROF_BYTES) {
		read_off = MMDC_MADPSR4;
		write_off = MMDC_MADPSR5;
	} else {
		read_off = MMDC_MADPSR2;
		write_off = MMDC_MADPSR3;		
	}
       
	/* Detect MMDCx target */
	printk("Selecting MMDC%d\n", (ctrl->control & PROF_TARGET) >> 2);
	if (ctrl->control & PROF_TARGET)
		base = (void *)MMDC1_BASE;
	else
		base = (void *)MMDC0_BASE;

	printk("Profiling interval: %d\n", interval);
	
	/* Reset count of samples, just in case */
	ctrl->count = 0;
	
	/* Program selected AXI ID filter */
	mmio_write32(base + MMDC_MADPCR1,
		     (ctrl->axi_mask << 16) | ctrl->axi_value); 

	/* Reset counters and clear overflow */	
	mmio_write32(base + MMDC_MADPCR0, CYC_OVF | DBG_RST);

	/* Enable profiling */	
	mmio_write32(base + MMDC_MADPCR0, DBG_EN);

	/* Set stopping point */
	if (ctrl->maxcount < available)
		available = ctrl->maxcount;
	
	printk("Configuration OKAY! Start time is %ld\n", arm_v8_get_timing());
	
	now = arm_v8_get_timing();
	next = now + interval;
	
	/* Ready to sample! */
	while(available--) {
		while((now = arm_v8_get_timing()) < next);
		/* Beginning of next interval */
		cur->cycles = now;
		cur->count = ctrl->count++;
		next += interval;
				
		/* Fill up current sample */
		cur->total_cycles = mmio_read32(base + MMDC_MADPSR0);
		cur->busy_cycles = mmio_read32(base + MMDC_MADPSR1);
		cur->reads = mmio_read32(base + read_off);
		cur->writes = mmio_read32(base + write_off);
		
		/* Point to next sample & keep track of total count */
		cur++;
		
		/* Check if we need to stop */
		if((ctrl->control & PROF_ENABLED) == 0)
			break;
	}

	/* Disable profiling */	
	mmio_write32(base + MMDC_MADPCR0, 0);	
}

static void arm_v8_timing_init(void)
{
	volatile uint32_t value = 0;
	
	/* Enable Performance Counter */
	asm volatile("MRS %0, PMCR_EL0" : "=r" (value));
	value |= ARMV8_PMCR_E; /* Enable */
	value |= ARMV8_PMCR_C; /* Cycle counter reset */
	value |= ARMV8_PMCR_P; /* Reset all counters */
	asm volatile("MSR PMCR_EL0, %0" : : "r" (value));

	/* Enable cycle counter register */
	asm volatile("MRS %0, PMCNTENSET_EL0" : "=r" (value));
	value |= ARMV8_PMCNTENSET_EL0_EN;
	asm volatile("MSR PMCNTENSET_EL0, %0" : : "r" (value));
}

static inline unsigned long arm_v8_get_timing(void)
{
	unsigned long volatile result = 0;
	asm volatile("MRS %0, PMCCNTR_EL0" : "=r" (result));
	return result;
}
