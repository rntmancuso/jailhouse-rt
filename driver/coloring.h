/*
 * Jailhouse, a Linux-based partitioning hypervisor
 *
 * Copyright (c) UniversitÃ  di Modena e Reggio Emilia, 2019
 *
 * Authors:
 *  Luca Miccio <lucmiccio@gmail.com>
 *  Marco Solieri <ms@xt3.it>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef _JAILHOUSE_DRIVER_COLORING_H
#define _JAILHOUSE_DRIVER_COLORING_H

#include "cell.h"

/*
 * Cache coloring support not tested on ARMv7 (yet) so
 * enable it only for ARMv8
 */
#ifdef CONFIG_ARM64

int jailhouse_coloring_cell_setup(struct cell *cell,
	const struct jailhouse_cell_desc *cell_desc);

void jailhouse_coloring_init(unsigned int llc_way_size);

unsigned long long driver_next_colored(unsigned long phys,
	unsigned long col_val);

#else /* !CONFIG_ARM64 */

static inline int jailhouse_coloring_cell_setup(struct cell *cell,
	const struct jailhouse_cell_desc *cell_desc)
{
	return 0;
}
static inline void jailhouse_coloring_init(unsigned int llc_way_size)
{
}

static inline unsigned long long driver_next_colored(unsigned long phys,
	unsigned long col_val)
{
	return phys;
}

#endif /* CONFIG_ARM64 */

#endif /* !_JAILHOUSE_DRIVER_COLORING_H */
