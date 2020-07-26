/*
 * Memguard for Jailhouse
 *
 * Copyright (c) Czech Technical University in Prague, 2018
 *
 * Authors:
 *  Joel Matějka <matejjoe@fel.cvut.cz>
 *  Michal Sojka <michal.sojka@cvut.cz>
 *  Přemysl Houdek <houdepre@fel.cvut.cz>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <asm/memguard.h>
#include <asm/sysregs.h>
#include <asm/irqchip.h>
#include <jailhouse/printk.h>
#include <asm/gic.h>
#include <asm/gic_v2.h>
#include <jailhouse/control.h>

#include <asm/percpu.h>

#define MG_DEBUG 0

#if CONFIG_MACH_JETSON_TX2 == 1
/* NVIDIA TX2 Specific Support */

/* Found out by using Linux perf tool and watching /proc/interrupts
 * Parker manual says:
 *   The total size of 384 corresponds to:
 *   32 first IDs are SGI and PPI
 *   288 next IDs are global SPI, one to one mapped to the 288 LIC interrupts
 *   64 next IDs are local SPI, generated inside CCPLEX and for CCPLEX use only
 */
#define CCPLEX_IRQ_SIZE			384
#define MEMGUARD_TIMER_IRQ		26

/* Conversion from cpu_id to PMU IRQ number
 *
 * Number 296 is defined in device tree which corresponds to:
 * (32 SGI and PPI +) 288 global SPI + 4 local SPI
 * This number is base for A57 cluster, 320 is for Denvers
 */
static const int mach_cpu_id2irqn[6] = {
	32 + 296,
	32 + 320,
	32 + 321,
	32 + 297,
	32 + 298,
	32 + 299,
};

/* On Parker, only 16 priority levels are implemented */
#define IRQ_PRIORITY_MIN		0xF0
#define IRQ_PRIORITY_MAX		0x00
#define IRQ_PRIORITY_INC		0x10

#define IRQ_PRIORITY_THR		0x10


#elif CONFIG_MACH_NXP_S32 == 1
/* NXP S32 Specific Support */

/* For this SoC we have:
 - 32 SGIs and PPIs
 - 8 SPIs
 - 16 on-platform vectors
 - 152 off-platform vectors
 ------ Total = 208
*/
#define CCPLEX_IRQ_SIZE			208
#define MEMGUARD_TIMER_IRQ		26 /* Non-secure physical timer */

static const int mach_cpu_id2irqn[4] = {
    195,
    196,
    197,
    198
};

/* On s32, all 256 priority levels are implemented */
#define IRQ_PRIORITY_MIN		0xFF
#define IRQ_PRIORITY_MAX		0x00
#define IRQ_PRIORITY_INC		0x01

#define IRQ_PRIORITY_THR		0x10

#elif CONFIG_MACH_ZYNQMP_ZCU102 == 1
/* ZCU 102 Specific Support */

/* For this SoC we have:
 - 32 SGIs and PPIs
 - 8 SPIs
 - 148 system interrupts
 ------ Total = 188
*/
#define CCPLEX_IRQ_SIZE			188
#define MEMGUARD_TIMER_IRQ		26 /* Non-secure physical timer */

static const int mach_cpu_id2irqn[4] = {
    175,
    176,
    177,
    178
};

/* On ZCU102, 16 priority levels are implemented in non-secure state */
#define IRQ_PRIORITY_MIN		0xF0
#define IRQ_PRIORITY_MAX		0x00
#define IRQ_PRIORITY_INC		0x10

#define IRQ_PRIORITY_THR		0x10

#else
#error No MemGuard support implemented for this SoC.
#endif 


/* Address of a bit for e.g. enabling of irq with id m is calculated as:
 * ADDR = BASE + (4 * n) where n = m / 32
 * Then the position of the bit in the register is calculated as:
 * POS = m MOD 32 */
#define IRQ_BIT_OFFSET(x)		(4 * ((x)/32))
#define IRQ_BIT_POSITION(x)		((x) % 32)

/* Similarly for bytes (e.g. irq priority) */
#define IRQ_BYTE_OFFSET(x)		(4 * ((x)/4))
#define IRQ_BYTE_POSITION(x)	(((x) % 4) * 8)
#define IRQ_BYTE_MASK			0xFF

#define CNTHP_CTL_EL2_ENABLE	(1<<0)
#define CNTHP_CTL_EL2_IMASK	(1<<1)

#if !defined(UINT32_MAX)
#define UINT32_MAX		0xffffffffU /* 4294967295U */
#endif

#if !defined(UINT64_MAX)
#define UINT64_MAX		0xffffffffffffffffULL /* 18446744073709551615 */
#endif

/* Reg def copied from kvm_arm.h */
/* Hyp Debug Configuration Register bits */
#define MDCR_EL2_TDRA		(1 << 11)
#define MDCR_EL2_TDOSA		(1 << 10)
#define MDCR_EL2_TDA		(1 << 9)
#define MDCR_EL2_TDE		(1 << 8)
#define MDCR_EL2_HPME		(1 << 7)
#define MDCR_EL2_TPM		(1 << 6)
#define MDCR_EL2_TPMCR		(1 << 5)
#define MDCR_EL2_HPMN_MASK	(0x1F)

#define PMCR_EL0_N_POS		(11)
#define PMCR_EL0_N_MASK		(0x1F << PMCR_EL0_N_POS)

#define PMEVTYPER_P				(1 << 31) /* EL1 modes filtering bit */
#define PMEVTYPER_U				(1 << 30) /* EL0 filtering bit */
#define PMEVTYPER_NSK			(1 << 29) /* Non-secure EL1 (kernel) modes filtering bit */
#define PMEVTYPER_NSU			(1 << 28) /* Non-secure User mode filtering bit */
#define PMEVTYPER_NSH			(1 << 27) /* Non-secure Hyp modes filtering bit */
#define PMEVTYPER_M				(1 << 26) /* Secure EL3 filtering bit */
#define PMEVTYPER_MT			(1 << 25) /* Multithreading */
#define PMEVTYPER_EVTCOUNT_MASK 0x3ff

/* PMU events copied from drivers/misc/tegra-profiler/armv8_events.h */

/* Required events. */
#define QUADD_ARMV8_HW_EVENT_PMNC_SW_INCR		0x00
#define QUADD_ARMV8_HW_EVENT_L1_DCACHE_REFILL		0x03
#define QUADD_ARMV8_HW_EVENT_L1_DCACHE_ACCESS		0x04
#define QUADD_ARMV8_HW_EVENT_PC_BRANCH_MIS_PRED		0x10
#define QUADD_ARMV8_HW_EVENT_CLOCK_CYCLES		0x11
#define QUADD_ARMV8_HW_EVENT_PC_BRANCH_PRED		0x12

/* At least one of the following is required. */
#define QUADD_ARMV8_HW_EVENT_INSTR_EXECUTED		0x08
#define QUADD_ARMV8_HW_EVENT_OP_SPEC			0x1B

/* Common architectural events. */
#define QUADD_ARMV8_HW_EVENT_MEM_READ			0x06
#define QUADD_ARMV8_HW_EVENT_MEM_WRITE			0x07
#define QUADD_ARMV8_HW_EVENT_EXC_TAKEN			0x09
#define QUADD_ARMV8_HW_EVENT_EXC_EXECUTED		0x0A
#define QUADD_ARMV8_HW_EVENT_CID_WRITE			0x0B
#define QUADD_ARMV8_HW_EVENT_PC_WRITE			0x0C
#define QUADD_ARMV8_HW_EVENT_PC_IMM_BRANCH		0x0D
#define QUADD_ARMV8_HW_EVENT_PC_PROC_RETURN		0x0E
#define QUADD_ARMV8_HW_EVENT_MEM_UNALIGNED_ACCESS	0x0F
#define QUADD_ARMV8_HW_EVENT_TTBR_WRITE			0x1C

/* Common microarchitectural events. */
#define QUADD_ARMV8_HW_EVENT_L1_ICACHE_REFILL		0x01
#define QUADD_ARMV8_HW_EVENT_ITLB_REFILL		0x02
#define QUADD_ARMV8_HW_EVENT_DTLB_REFILL		0x05
#define QUADD_ARMV8_HW_EVENT_MEM_ACCESS			0x13
#define QUADD_ARMV8_HW_EVENT_L1_ICACHE_ACCESS		0x14
#define QUADD_ARMV8_HW_EVENT_L1_DCACHE_WB		0x15
#define QUADD_ARMV8_HW_EVENT_L2_CACHE_ACCESS		0x16
#define QUADD_ARMV8_HW_EVENT_L2_CACHE_REFILL		0x17
#define QUADD_ARMV8_HW_EVENT_L2_CACHE_WB		0x18
#define QUADD_ARMV8_HW_EVENT_BUS_ACCESS			0x19
#define QUADD_ARMV8_HW_EVENT_MEM_ERROR			0x1A
#define QUADD_ARMV8_HW_EVENT_BUS_CYCLES			0x1D

/* ARMv8 Cortex-A57 specific event types. */
#define QUADD_ARMV8_A57_HW_EVENT_L1D_CACHE_REFILL_LD	0x42
#define QUADD_ARMV8_A57_HW_EVENT_L1D_CACHE_REFILL_ST	0x43
#define QUADD_ARMV8_A57_HW_EVENT_L2D_CACHE_REFILL_LD	0x52
#define QUADD_ARMV8_A57_HW_EVENT_L2D_CACHE_REFILL_ST	0x53

extern void *gicc_base;
extern void *gicd_base;


#define DEFAULT_EVENTS_MAX 10
#define DEBUG_MG

#define PMU_INDEX 5

#define PMUREG(name, num) name ## num ## _EL0
#define PMEVCNTR(num) PMUREG(PMEVCNTR, num)
#define PMEVTYPER(num) PMUREG(PMEVTYPER, num)

static inline int gicv2_get_prio(int irqn)
{
	u32 prio = mmio_read32(gicd_base + GICD_IPRIORITYR + IRQ_BYTE_OFFSET(irqn));
	return (prio >> IRQ_BYTE_POSITION(irqn)) & IRQ_BYTE_MASK;
}

static inline void gicv2_set_prio(int irqn, int prio)
{
	u32 p = mmio_read32(gicd_base + GICD_IPRIORITYR + IRQ_BYTE_OFFSET(irqn));
	p &= ~(IRQ_BYTE_MASK << IRQ_BYTE_POSITION(irqn));
	p |= (prio & IRQ_BYTE_MASK) << IRQ_BYTE_POSITION(irqn);
	mmio_write32(gicd_base + GICD_IPRIORITYR + IRQ_BYTE_OFFSET(irqn), p);
}

static inline int gicv2_get_targets(int irqn)
{
	u32 t = mmio_read32(gicd_base + GICD_ITARGETSR + IRQ_BYTE_OFFSET(irqn));
	return (t >> IRQ_BYTE_POSITION(irqn)) & IRQ_BYTE_MASK;
}

static inline void gicv2_set_targets(int irqn, int targets)
{
	u32 t = mmio_read32(gicd_base + GICD_ITARGETSR + IRQ_BYTE_OFFSET(irqn));
	t &= ~(IRQ_BYTE_MASK << IRQ_BYTE_POSITION(irqn));
	t |= (targets & IRQ_BYTE_MASK) << IRQ_BYTE_POSITION(irqn);
	mmio_write32(gicd_base + GICD_ITARGETSR + IRQ_BYTE_OFFSET(irqn), t);
}

/* Globally lower (numerically increase) all current priorities and
 * set maximal priority to timer and PMU IRQs */
static inline void memguard_init_priorities(void)
{
	int i;

	for (i = 0; i < CCPLEX_IRQ_SIZE; i++) {
		u32 prio = gicv2_get_prio(i);

		/* Avoid chaning the priorities, which are low enough
		 * and never set minimal (i.e. always masked)
		 * priority. */
		while (prio < IRQ_PRIORITY_THR &&
		       prio < IRQ_PRIORITY_MIN - IRQ_PRIORITY_INC)
			prio += IRQ_PRIORITY_INC;
		gicv2_set_prio(i, prio);
	}

	for (i = 0; i < ARRAY_SIZE(mach_cpu_id2irqn); i++) {
		gicv2_set_prio(mach_cpu_id2irqn[i], IRQ_PRIORITY_MAX + IRQ_PRIORITY_INC);
	}

	gicv2_set_prio(MEMGUARD_TIMER_IRQ, IRQ_PRIORITY_MAX);
}

static inline void memguard_dump_timer_regs(void)
{
	/* This function dumps the configuration of the timer registers */
	u64 reg;
	arm_read_sysreg(CNTPCT_EL0, reg);
	printk("CNT: %lld\n", reg);
	
	arm_read_sysreg(CNTHP_CVAL_EL2, reg);
	printk("CMP: %lld\n", reg);
	
	arm_read_sysreg(CNTHP_CTL_EL2, reg);
	printk("CTL: %lld\n", reg);
	
}

static inline void memguard_print_priorities(void)
{
	int i, j;
	u32 prio;
	for (i = 0; i < CCPLEX_IRQ_SIZE / 4; i++) {
		prio = mmio_read32(gicd_base + GICD_IPRIORITYR + (4 * i));
		for (j = 0; j < 4; j++) {
			printk("%3d %02x\n", i * 4 + j, (prio >> (8 * j)) & 0xFF);
		}
	}
	prio = mmio_read32(gicc_base + GICC_PMR);
	printk("mask: 0x%08x\n", prio);
}

static inline u64 memguard_timer_count(void)
{
	u64 reg64;
	arm_read_sysreg(CNTPCT_EL0, reg64);
	return reg64;
}

static inline u32 memguard_pmu_count(void)
{
	u32 reg32;
	arm_read_sysreg(PMEVCNTR(PMU_INDEX), reg32);
	return reg32;
}

static inline void memguard_pmu_irq_enable(unsigned int cpu_id, u8 targets)
{
	int irqn = mach_cpu_id2irqn[cpu_id];

	/* Enable interrupt for counter at index */
	arm_write_sysreg(PMINTENSET_EL1, 1 << PMU_INDEX);

	/* Enable PMU interrupt for current core */
	mmio_write32(gicd_base + GICD_ISENABLER + IRQ_BIT_OFFSET(irqn),
		     1 << IRQ_BIT_POSITION(irqn));

	gicv2_set_targets(irqn, targets);
}

static inline void memguard_pmu_irq_disable(unsigned int cpu_id)
{
	int irqn = mach_cpu_id2irqn[cpu_id];

	arm_write_sysreg(PMINTENCLR_EL1, 1 << PMU_INDEX);

	mmio_write32(gicd_base + GICD_ICENABLER + IRQ_BIT_OFFSET(irqn),
		     1 << IRQ_BIT_POSITION(irqn));
}

static inline void memguard_pmu_count_enable(void)
{
	arm_write_sysreg(PMCNTENSET_EL0, 1 << PMU_INDEX);
}

static inline void memguard_pmu_count_disable(void)
{
	arm_write_sysreg(PMCNTENCLR_EL0, 1 << PMU_INDEX);
}

static inline void memguard_pmu_set_budget(u32 budget)
{
	arm_write_sysreg(PMEVCNTR(PMU_INDEX), (u32)UINT32_MAX - budget);
	arm_write_sysreg(PMEVTYPER(PMU_INDEX),
			 //QUADD_ARMV8_HW_EVENT_BUS_ACCESS
			 //QUADD_ARMV8_HW_EVENT_CLOCK_CYCLES
			 QUADD_ARMV8_HW_EVENT_L2_CACHE_REFILL
		);
}

static void memguard_pmu_isr(volatile struct memguard *memguard)
{
#if MG_DEBUG == 1	
	u32 cntval = memguard_pmu_count();
	u64 timval = memguard_timer_count();

	static u32 print_cnt = 0;
#endif
	
	/* Clear overflow flag */
	arm_write_sysreg(PMOVSCLR_EL0, 1 << PMU_INDEX);

#if MG_DEBUG == 1	
	if (print_cnt < 100)
		printk("[%d] _isr_pmu: p: %u t: %llu (CPU %d)\n",
		       ++print_cnt, cntval, timval, this_cpu_id());
#endif
	memguard->memory_overrun = true;
	if (memguard->flags & MGF_PERIODIC)
		memguard->block = 1; /* Block after EOI signalling */
}

void memguard_block_if_needed(void)
{
	volatile struct memguard *memguard = &this_cpu_data()->memguard;
	
	if (memguard->block == 1) {
		unsigned long spsr, elr;

		/* Do not block while handling other nested IRQs */
		memguard->block = 2;

		arm_read_sysreg(ELR_EL2, elr);
		arm_read_sysreg(SPSR_EL2, spsr);
		asm volatile("msr daifclr, #3" : : : "memory"); /* enable IRQs and FIQs */
		
		/*
		 * This loop should be race-free. When the timer IRQ
		 * arrives between the while condition and wfe, it
		 * sets the Event Register and wfe will not wait.
		 */

		while (memguard->block)
			asm volatile("wfe");
		
			
		asm volatile("msr daifset, #3" : : : "memory"); /* disable IRQs and FIQs */
		arm_write_sysreg(ELR_EL2, elr);
		arm_write_sysreg(SPSR_EL2, spsr);
	}
}


static inline void memguard_pmu_init(unsigned int cpu_id, u8 irq_targets)
{
	u32 reg32;
	u64 reg;

	arm_read_sysreg(PMCR_EL0, reg32);

	if (PMU_INDEX != ((reg32 & PMCR_EL0_N_MASK) >> PMCR_EL0_N_POS) - 1) {
		panic_printk("Memguard PMU index mismatch\n");
		panic_stop();
	}

	/* Reserve a performance counter at index for hypervisor
	 * (decrease number of accessible counters from EL1 and EL0) */
	arm_read_sysreg(MDCR_EL2, reg);
	reg &= ~MDCR_EL2_HPMN_MASK;
	reg |= MDCR_EL2_HPME + (PMU_INDEX - 1);
	arm_write_sysreg(MDCR_EL2, reg);

	/* Allocate the counter for hypervisor */
	memguard_pmu_count_disable();
	arm_write_sysreg(PMOVSCLR_EL0, 1 << PMU_INDEX); // Clear overflow flag

	memguard_pmu_irq_enable(cpu_id, irq_targets);
}

static inline void memguard_timer_irq_enable(void)
{
	/* Configure compare value first! (timer >= compare -> isr) */
	u32 reg;
	arm_read_sysreg(CNTHP_CTL_EL2, reg);
	reg &= ~CNTHP_CTL_EL2_IMASK;
	arm_write_sysreg(CNTHP_CTL_EL2, reg);

	mmio_write32(gicd_base + GICD_ISENABLER, (1 << MEMGUARD_TIMER_IRQ));
}

static inline void memguard_timer_irq_disable(void)
{
	u32 reg;
	arm_read_sysreg(CNTHP_CTL_EL2, reg);
	reg |= CNTHP_CTL_EL2_IMASK;
	arm_write_sysreg(CNTHP_CTL_EL2, reg);

	mmio_write32(gicd_base + GICD_ICENABLER, (1 << MEMGUARD_TIMER_IRQ));
}

static inline void memguard_timer_enable(void)
{
	u32 reg;
	arm_read_sysreg(CNTHP_CTL_EL2, reg);
	reg |= CNTHP_CTL_EL2_ENABLE;
	arm_write_sysreg(CNTHP_CTL_EL2, reg);
}

static inline void memguard_timer_set_cmpval(u64 cmp)
{
	arm_write_sysreg(CNTHP_CVAL_EL2, cmp);
}

static inline void memguard_timer_disable(void)
{
	u32 reg;
	arm_read_sysreg(CNTHP_CTL_EL2, reg);
	reg &= ~CNTHP_CTL_EL2_ENABLE;
	arm_write_sysreg(CNTHP_CTL_EL2, reg);
}

static inline void memguard_timer_init(void)
{
	/* Set compare value to 200 years from 0 */
	memguard_timer_set_cmpval(UINT64_MAX);
	memguard_timer_irq_enable();
}

static void memguard_timer_isr(volatile struct memguard *memguard)
{

	u32 cntval = memguard_pmu_count();
#if MG_DEBUG == 1
	u64 timval = memguard_timer_count();

	static u32 print_cnt[4] = {0, 0, 0, 0};
	if (print_cnt[this_cpu_id()] < 100)
		printk("[%d] _isr_tim p: %u t: %llu (CPU %d)\n",
		       ++print_cnt[this_cpu_id()], cntval, timval, this_cpu_id());
#endif
	memguard->time_overrun = true;

	if (memguard->flags & MGF_PERIODIC) {
		memguard->last_time += memguard->budget_time;
		memguard->pmu_evt_cnt += memguard->budget_memory + 1 + cntval;
		memguard_timer_set_cmpval(memguard->last_time);
		memguard_pmu_set_budget(memguard->budget_memory);
		memguard->block = 0;
	} else {
		memguard_timer_disable();
	}
}

static bool is_memguard_pmu_irq(u32 irqn)
{
	u32 reg;
	
	if (irqn != mach_cpu_id2irqn[this_cpu_id()])
		return false;

	arm_read_sysreg(PMOVSCLR_EL0, reg);

	return (reg & (1 << PMU_INDEX)) != 0;
}

bool memguard_handle_interrupt(u32 irqn)
{
#if MG_DEBUG == 1
	static u32 print_cnt = 0;

	if ((print_cnt < 100 || this_cpu_data()->memguard.block) &&
	    ((this_cpu_id() == 2 && irqn != 30 && irqn != 26) ||
	     (irqn >= mach_cpu_id2irqn[0] && (irqn <= mach_cpu_id2irqn[3]))))
	{
		printk("[%d] Received MG interrupt on CPU %d, nr = %d (block = %d)\n",
		       ++print_cnt, this_cpu_id(), irqn, this_cpu_data()->memguard.block);
		memguard_dump_timer_regs();
	}
	
#endif
	
	if (is_memguard_pmu_irq(irqn)) {
		memguard_pmu_isr(&this_cpu_data()->memguard);
		return true;
	} else if (irqn == MEMGUARD_TIMER_IRQ) {
		memguard_timer_isr(&this_cpu_data()->memguard);
		return true;
	}

	return false;
}

void memguard_init(u8 local_irq_target)
{
	struct per_cpu *cpu_data = this_cpu_data();
	struct memguard *memguard = &cpu_data->memguard;

	printk("initializing memguard on CPU %d ", this_cpu_id());
	
	memset(memguard, 0, sizeof(*memguard));

	memguard_pmu_init(this_cpu_id(), local_irq_target);

	memguard_timer_init();

	/* Interrupt controller can filter interrupts with lower priorities
	 * lower number = higher priority
	 * Described in detail in Interrupt prioritization
	 * ARM GIC Architecture Specification
	 * TODO: Implement priorities */

	/* TODO: Do this only on one core */
	memguard_init_priorities();
}

void memguard_suspend()
{
	memguard_pmu_count_disable();
	memguard_timer_disable();

	memguard_timer_set_cmpval(UINT64_MAX);
}

void memguard_exit()
{
	printk("memguard_exit\n");

	u32 reg32;

	memguard_pmu_count_disable();
	memguard_timer_disable();

	memguard_pmu_irq_disable(this_cpu_id());
	memguard_timer_irq_disable();

	/* Make the memguard counter visible again to non-secure mode */
	arm_read_sysreg(MDCR_EL2, reg32);
	reg32 &= ~(MDCR_EL2_HPMN_MASK);
	reg32 |= MDCR_EL2_HPME + PMU_INDEX - 1;
	arm_write_sysreg(MDCR_EL2, reg32);
}

static inline void memguard_mask_interrupts(void)
{
	mmio_write32(gicc_base + GICC_PMR, IRQ_PRIORITY_THR);
}

static inline void memguard_unmask_interrupts(void)
{
	mmio_write32(gicc_base + GICC_PMR, IRQ_PRIORITY_MIN);
}

/**
 * Syscall called on PREM phases borders
 *
 * budget_time - time in us
 * budget_memory - the number of PMU events (i.e. cache misses)
 * flags - see MGF_*
 *
 * Returns profiling data for the last phase.
 */
long memguard_call(unsigned long budget_time, unsigned long budget_memory,
		   unsigned long flags)
{
	u64 retval = 0;
	u32 freq;
	
	struct per_cpu *cpu_data = this_cpu_data();
	struct memguard *memguard = &cpu_data->memguard;
		
	/* Prevent race conditions with timer and PMU IRQ handlers */
	memguard_pmu_count_disable();
	memguard_timer_disable();

	printk("memguard_call %lu %lu %lx (CPU %d)\n",
	       budget_time, budget_memory, flags, this_cpu_id());
	
	/* Store statistics since last call for profiling */
	u64 timval = memguard_timer_count();
	u32 cntval = memguard_pmu_count();

	arm_read_sysreg(CNTFRQ_EL0, freq);

	u64 pmu_evt_cnt = memguard->pmu_evt_cnt + memguard->budget_memory + 1 + cntval;
	u64 time_us = (timval - memguard->start_time) * 1000000 / freq;
	retval = (memguard->time_overrun ? MGRET_OVER_TIM_MASK : 0ul) |
		 (memguard->memory_overrun ? MGRET_OVER_MEM_MASK : 0ul) |
		 (pmu_evt_cnt <= MGRET_MEM_MASK >> MGRET_MEM_POS ?
			  pmu_evt_cnt << MGRET_MEM_POS :
			  MGRET_MEM_MASK) |
		 (time_us <= MGRET_TIM_MASK >> MGRET_TIM_POS ?
			  time_us << MGRET_TIM_POS :
			  MGRET_TIM_MASK);
	
	/* Setup memguard according to this call parameters */
	memguard->time_overrun = false;
	memguard->memory_overrun = false;
	memguard->block = 0;
	memguard->flags = (flags & MGF_PERIODIC) ? MGF_PERIODIC : 0;
	if (flags & MGF_PERIODIC && budget_time == 0)
		return retval | MGRET_ERROR_MASK;

	if (flags & MGF_MASK_INT)
		memguard_mask_interrupts();
	else
		memguard_unmask_interrupts();

	memguard->pmu_evt_cnt = 0;
	memguard->budget_memory = budget_memory;
	if (budget_memory > 0)
		memguard_pmu_set_budget(budget_memory);

	if (budget_time > 0) {
		memguard->start_time = memguard->last_time = timval;
		memguard->budget_time = ((u64)budget_time * freq + 999999) / 1000000;
		memguard_timer_set_cmpval(memguard->last_time + memguard->budget_time);
	}

	if (budget_memory > 0)
		memguard_pmu_count_enable(); /* Keep this before memguard_timer_enable() */
	if (budget_time > 0)
		memguard_timer_enable();

	return retval;
}


long memguard_call_params(unsigned long params_ptr)
{
	unsigned long params_page_offs = params_ptr & ~PAGE_MASK;
	unsigned int params_pages;
	void *params_mapping;
	
	/* The settings currently reside in kernel memory. Use
	 * temporary mapping to make the settings readable by the
	 * hypervisor. No need to clean up the mapping because this is
	 * only temporary by design. */
	params_pages = PAGES(params_page_offs + sizeof(struct memguard_params));
	params_mapping = paging_get_guest_pages(NULL, params_ptr, params_pages,
					     PAGE_READONLY_FLAGS);

	/* This should never happen. */
	if (!params_mapping)
		return -ENOMEM;

	struct memguard_params * params = (struct memguard_params *)
		(params_mapping + params_page_offs);

	return memguard_call(params->budget_time, params->budget_memory, params->flags);
}
