/*
 * Jailhouse, a Linux-based partitioning hypervisor
 *
 * Copyright (c) Siemens AG, 2018
 *
 * Authors:
 *  Jan Kiszka <jan.kiszka@siemens.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <asm/memguard-data.h>

#define ARCH_PERCPU_FIELDS						\
	ARM_PERCPU_FIELDS						\
	unsigned long id_aa64mmfr0;					\
	struct memguard memguard;
