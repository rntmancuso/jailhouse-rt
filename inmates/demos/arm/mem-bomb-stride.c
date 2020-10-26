/*
 * Jailhouse, a Linux-based partitioning hypervisor
 *
 * Copyright (c) ARM Limited, 2014
 * Copyright (c) Siemens AG, 2014-2017
 *
 * Authors:
 *  Jean-Philippe Brucker <jean-philippe.brucker@arm.com>
 *  Jan Kiszka <jan.kiszka@siemens.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <inmate.h>
#include <gic.h>
#include <asm/sysregs.h>
#include <jailhouse/memguard-common.h>

#define MEM_SIZE_MB         4 /* Memory size will be 4 MB */
#define MEM_SIZE            (MEM_SIZE_MB * 1024 * 1024) 
#define LINE_SIZE           (64) //Cache line size is 64 bytes
#define PAGE_SHIFT          (12) //Page size is 4 KB
#define BIT(x)              (1UL << x)

#define print(fmt, ...)							\
	printk("[BOMB %d] " fmt, id, ##__VA_ARGS__)

/* Virtual address of command and control interface */
#define CMD_REGION_BASE     0x500000
#define CMD_ENABLE          BIT(0)
#define CMD_DO_READS        BIT(1)
#define CMD_DO_WRITES       BIT(2)
#define CMD_VERBOSE         BIT(3)
#define CMD_BOMB_ID_SHIFT   4

/* Functions to interact with CPU cycle counter */
#define magic_timing_begin(cycles)\
	do{								\
		asm volatile("mrs %0, CNTVCT_EL0": "=r"(*(cycles)) );	\
	}while(0)

#define magic_timing_end(cycles)					\
	do{								\
		unsigned long tempCycleLo;				\
		asm volatile("mrs %0, CNTVCT_EL0":"=r"(tempCycleLo) );  \
		*(cycles) = tempCycleLo - *(cycles);			\
	}while(0)

static volatile unsigned char buffer[MEM_SIZE];
u64 crc;
u32 id;
struct memguard_params mg_params;

/* Structure of the command & control interface  */
struct control {
	u32 command;
	u32 size;
	u32 mg_budget;
};

/* Perform write-only iterations over the memory buffer */
void do_reads(volatile struct control * ctrl);

/* Perform write-only iterations over the memory buffer */
void do_writes(volatile struct control * ctrl);

/* Perform write-only iterations over the memory buffer */
void do_reads_writes(volatile struct control * ctrl);

/* Print some info about the memory setup in the inmate */
void print_mem_info(void);

/* Perform read-only iterations over the memory buffer */
void do_reads(volatile struct control * ctrl)
{
	int i, j;
	int size;
	int loops = 0;
	unsigned long cycles;
	unsigned long total = 0;
	
	crc = 0;

	size = ctrl->size & (u32)(~(PAGE_SIZE - 1));
	
	if (ctrl->command & CMD_VERBOSE)
		print("Started READ accesses with size %d.\n", size);

	while (ctrl->command & CMD_ENABLE) {
		magic_timing_begin(&cycles);
		for (i = 0; i < PAGE_SIZE; i += LINE_SIZE) {
			for (j = 0; j < size; j += PAGE_SIZE) {				
				crc += buffer[i + j];
			}
		}		
		magic_timing_end(&cycles);
		++loops;
		total += cycles;
	}

	if (ctrl->command & CMD_VERBOSE) {
		print("Done with READ accesses. Check = 0x%08llx\n", crc);
		print("\tAvg. Time: %ld\n", total / loops);
	}

}

/* Perform write-only iterations over the memory buffer */
void do_writes(volatile struct control * ctrl)
{
	int i, j;
	int size = ctrl->size;
	int loops = 0;
	unsigned long cycles;
	unsigned long total = 0;
	
	crc = 0;

	size = ctrl->size & (u32)(~(PAGE_SIZE - 1));

	if (ctrl->command & CMD_VERBOSE)
		print("Started WRITE accesses with size %d.\n", size);
	
	while (ctrl->command & CMD_ENABLE) {
		magic_timing_begin(&cycles);
		for (i = 0; i < PAGE_SIZE; i += LINE_SIZE) {
			for (j = 0; j < size; j += PAGE_SIZE) {				
				buffer[i + j] = i;
			}
		}		
		magic_timing_end(&cycles);
		++loops;
		total += cycles;
	}

	if (ctrl->command & CMD_VERBOSE) {
		print("Done with WRITE accesses.\n");
		print("\tAvg. Time: %ld\n", total / loops);
	}

	
}

/* Perform write-only iterations over the memory buffer */
void do_reads_writes(volatile struct control * ctrl)
{
	int i = 0;
	int size = ctrl->size;
	
	crc = 0;

	if (ctrl->command & CMD_VERBOSE)
		print("Started READ+WRITE accesses with size %d.\n", size);

	/* The top half will be written, the bottom half will be read */
	size = size / 2;
	
	while (ctrl->command & CMD_ENABLE) {
		for (i = 0; i < size; i += LINE_SIZE) {
			buffer[i] += buffer[i+size];
		}
	}

	if (ctrl->command & CMD_VERBOSE)
		print("Done with READ+WRITE accesses.\n");

}

/* Print some info about the memory setup in the inmate */
void print_mem_info(void)
{
	/* Read and print value of the SCTRL register */
	unsigned long sctlr, tcr;
	arm_read_sysreg(SCTLR, sctlr);
	arm_read_sysreg(TRANSL_CONT_REG, tcr);

	print("SCTLR_EL1 = 0x%08lx\n", sctlr);
	print("TCR_EL1 = 0x%08lx\n", tcr);
}

static void test_translation(unsigned long addr)
{
	unsigned long par;
	asm volatile("at s1e1r, %0" : : "r"(addr));

	arm_read_sysreg(PAR_EL1, par);

	print("Translated 0x%08lx -> 0x%08lx\n", addr, par);
}


void inmate_main(void)
{
	volatile struct control * ctrl = (volatile struct control *)(CMD_REGION_BASE);

	/* Set the ID of this bomb from the passed command */
	id = ctrl->command >> CMD_BOMB_ID_SHIFT;
	mg_params.budget_time = 1000;
	mg_params.flags = 1;
	
	print("Stride-access Memory Bomb Started.\n");

	print_mem_info();
	test_translation((unsigned long)buffer);
	jailhouse_call_arg1(JAILHOUSE_HC_QOS+1, (unsigned long)buffer);

	jailhouse_call_arg1(JAILHOUSE_HC_QOS+1, (unsigned long)0x7500000UL);
	jailhouse_call_arg1(JAILHOUSE_HC_QOS+1, (unsigned long)0x6500000UL);
	
	/* Main loop */
	while(1) {
		/* Idle while the enable bit is cleared */
		while(!(ctrl->command & CMD_ENABLE));

		if (ctrl->mg_budget > 0) {
			print("Setting MG budget %d\n", ctrl->mg_budget);
			mg_params.budget_memory = ctrl->mg_budget;
			jailhouse_call_arg1(JAILHOUSE_HC_MEMGUARD, (unsigned long)&mg_params);
		}
		
		if ((ctrl->command & CMD_DO_READS) &&
		    (ctrl->command & CMD_DO_WRITES))
			do_reads_writes(ctrl);
		else if (ctrl->command & CMD_DO_READS)
			do_reads(ctrl);
		else if (ctrl->command & CMD_DO_WRITES)
			do_writes(ctrl);
		else {
			print("Invalid command (0x%08x)\n",
			      ctrl->command);

			/* Clear the enable bit so we do not print and
			 * endless list of errors. */
			ctrl->command &= ~CMD_ENABLE;
		}
	}
	
	halt();
}
