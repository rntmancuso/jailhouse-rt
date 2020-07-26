/*
 * MemGuard Support for Jailhouse
 *
 * Copyright (c) Boston University, 2020
 *
 * Authors:
 *  Renato Mancuso <rmancuso@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef _JAILHOUSE_MEMGUARD_COMMON_H
#define _JAILHOUSE_MEMGUARD_COMMON_H

struct memguard_params {
	unsigned long budget_time;
	unsigned long budget_memory;
	unsigned long flags;
};

#endif /* _JAILHOUSE_MEMGUARD_COMMON_H */

