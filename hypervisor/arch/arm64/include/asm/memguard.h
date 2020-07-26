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

#ifndef _JAILHOUSE_ASM_MEMGUARD_H
#define _JAILHOUSE_ASM_MEMGUARD_H

#include <jailhouse/types.h>
#include <asm/percpu.h>
#include <jailhouse/memguard-common.h>

void memguard_init(u8 local_irq_target);
void memguard_suspend(void);
void memguard_exit(void);
bool memguard_handle_interrupt(u32 irqn);
void memguard_block_if_needed(void);

/* Memguard flags */
#define MGF_PERIODIC      (1 << 0) /* Chooses between periodic or one-shot budget replenishment */
#define MGF_MASK_INT      (1 << 1) /* Mask (disable) low priority interrupts until next memguard call */

#define MGRET_ERROR_POS		0
#define MGRET_MEM_POS		1
#define MGRET_TIM_POS		33
#define MGRET_OVER_MEM_POS	62
#define MGRET_OVER_TIM_POS	63

#define MGRET_TIM_MASK		(0x00FFFFFFul << MGRET_TIM_POS)
#define MGRET_MEM_MASK		(0xFFFFFFFFul << MGRET_MEM_POS)
#define MGRET_OVER_MEM_MASK	(1ul << MGRET_OVER_MEM_POS)
#define MGRET_OVER_TIM_MASK	(1ul << MGRET_OVER_TIM_POS)
#define MGRET_ERROR_MASK	(1ul << MGRET_ERROR_POS)

/**
 * Main MemGuard interface for applications.
 *
 * Applications can call this via Linux system call no. 793 (patched
 * Linux kernel is needed). Later, we disable this interface and
 * memguard will be only accessible via less powerful prem
 * hypercall.
 *
 * This hypercall setups time and memory budgets for the calling CPU
 * core. Subsequent call then returns statistics from the preceding
 * call. Time budget overrun is reported with MGRET_OVER_TIM_MASK bit,
 * memory budget overrun with MGRET_OVER_MEM_MASK. Returned statistics
 * also include the total time and total number of cache misses since
 * the preceding call, which can be used for application profiling.
 *
 * The memguard functionality can be influenced by the following flags:
 *
 * - MGF_PERIODIC: When set, the memguard timer is set to expire
 *   periodically every budget_time. Overrunning memory budget causes
 *   the CPU to block (enter low-power state) until next expiration of
 *   the memguard timer. At every timer expiration, memory budget is
 *   replenished with budget_memory value.
 *
 *   Note that with this flag unset, memguard only reports budget
 *   overrun in statistics. No blocking happens. This may change in
 *   the future.
 *
 * - MGF_MASK_INT: When set, memguard disables interrupts that can be
 *   disabled and are not needed for proper memguard functionality.
 *   This is to ensure (almost) non-preemptive execution of PREM
 *   predictable intervals that is required to reduce number of
 *   unpredictable cache misses.
 *
 * @param budget_time Time budget in microseconds. When zero, time
 * monitoring is switched off.
 * @param budget_memory Memory budget (number of cache misses). When
 * zero, memory access monitoring is switched off. Must be non-zero
 * when MGF_PERIODIC is set.
 * @param flags Flags - see MGF_* constants above.
 *
 * @return Statistics since the preceding memguard call and/or the error flag.
 * These are encoded in different bits as follows:
 *   - 0     - Error (to keep compatibility with Linux smp calls)
 *   - 1-32  - Total number cache misses
 *   - 33-56 - Total time in microseconds
 *   - 62    - Memory budget overrun
 *   - 63    - Time budget overrun
 * See also MGRET_* constants.
 */
long memguard_call(unsigned long budget_time, unsigned long budget_memory,
		   unsigned long flags);

long memguard_call_params(unsigned long params_ptr);

#endif
