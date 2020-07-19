/*
 * Jailhouse, a Linux-based partitioning hypervisor
 *
 * Copyright (c) Siemens AG, 2020
 *
 * Authors:
 *  Jan Kiszka <jan.kiszka@siemens.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef _JAILHOUSE_BITOPS_H
#define _JAILHOUSE_BITOPS_H

#include <jailhouse/types.h>
#include <asm/bitops.h>

#define FIELD_PREP(mask, val)	\
			(((u64)(val) << (__builtin_ffsl((mask)) - 1)) & (mask))
#define FIELD_GET(mask, reg)	\
			(((reg) & (mask)) >> (__builtin_ffsl((mask)) - 1))
#define FIELD_CLEAR(mask, reg)	\
			((reg) & (~(mask)))

#define BITS_PER_LONG 64
#define UL(x)                   ((unsigned long)x)
#define BIT(nr)			(UL(1) << (nr))
#define GENMASK(h, l) \
	(((~UL(0)) - (UL(1) << (l)) + 1) & \
	 (~UL(0) >> (BITS_PER_LONG - 1 - (h))))

#define __bf_shf(x) (__builtin_ffsll(x) - 1)

static inline __attribute__((always_inline)) void
clear_bit(unsigned int nr, volatile unsigned long *addr)
{
	addr[nr / BITS_PER_LONG] &= ~(1UL << (nr % BITS_PER_LONG));
}

static inline __attribute__((always_inline)) void
set_bit(unsigned int nr, volatile unsigned long *addr)
{
	addr[nr / BITS_PER_LONG] |= 1UL << (nr % BITS_PER_LONG);
}

#endif /* !_JAILHOUSE_BITOPS_H */
