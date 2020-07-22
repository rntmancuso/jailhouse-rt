/*
 * Jailhouse Cache coloring for ARM64
 *
 * Copyright (C) 2018 Università di Modena e Reggio Emilia
 *
 * Authors:
 *   Luca Miccio <lucmiccio@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <asm/bitops.h>
#include <asm/percpu.h>
#include <jailhouse/cell.h>
#include <jailhouse/cell-config.h>
#include <jailhouse/printk.h>
#include <jailhouse/paging.h>

#ifndef _JAILHOUSE_COLORING_H
#define _JAILHOUSE_COLORING_H

typedef enum {CREATE, DESTROY, START, LOAD, DCACHE} col_operation;

extern struct jailhouse_system *system_config;

/**
 * Loop-generating macro for iterating over all colored memory regions
 * of a configuration.
 * @param mem		Iteration variable holding the reference to the current
 * 			memory region (const struct jailhouse_memory *).
 * @param config	Cell or system configuration containing the regions.
 * @param counter	Helper variable (unsigned int).
 */
#define for_each_col_mem_region(mem, config, counter)			\
	for ((mem) = jailhouse_cell_col_mem_regions(config), (counter) = 0;	\
	     (counter) < (config)->num_memory_regions_colored;			\
	     (mem)++, (counter)++)

struct col_manage_ops {
  int (*map_f)(struct cell *cell, const struct jailhouse_memory *mem);
  int (*subpage_f)(struct cell *cell,
                 const struct jailhouse_memory *mem);
  int (*unmap_f)(struct cell *cell,
                 const struct jailhouse_memory *mem);
  /* unmap_from_root_cell if cell is starting and mem is loadable*/
  int (*unmap_root_f)(const struct jailhouse_memory* mem);
  /* remap_to_root_cell if the cell is loadable to permit the root cell to
   * load the image */
  int (*remap_root_f)(const struct jailhouse_memory* mem, enum failure_mode mode);
  /* Flush operation used during D-Cache operation */
  enum dcache_flush flush;	
};

extern struct col_manage_ops col_ops;

/**
 * Applies the same operation to all the colored memory regions
 * @see enum col_operation.
*/
int __coloring_cell_apply_to_col_mem(struct cell *cell, col_operation type, void * extra);

#define coloring_cell_create(cell)	\
    __coloring_cell_apply_to_col_mem(cell, CREATE, NULL)

#define coloring_cell_destroy(cell)	\
    __coloring_cell_apply_to_col_mem(cell, DESTROY, NULL)

#define coloring_cell_start(cell)	\
    __coloring_cell_apply_to_col_mem(cell, START, NULL)

#define coloring_cell_load(cell)	\
    __coloring_cell_apply_to_col_mem(cell, LOAD, NULL)

#define coloring_cell_flush(cell, flush_type)		\
    __coloring_cell_apply_to_col_mem(cell, DCACHE, (void *)flush_type)

#endif /* _JAILHOUSE_COLORING_H */
