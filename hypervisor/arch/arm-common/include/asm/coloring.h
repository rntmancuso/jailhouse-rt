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

typedef enum {CREATE, DESTROY, START, LOADABLE, DCACHE} col_operation;

extern struct jailhouse_system *system_config;
enum failure_mode {ABORT_ON_ERROR, WARN_ON_ERROR};

struct create_functions {
  int (*map_f)(struct cell *cell, const struct jailhouse_memory *mem);
  int (*subpage_f)(struct cell *cell,
                 const struct jailhouse_memory *mem);
  int (*unmap_root_f)(const struct jailhouse_memory* mem);
};

struct destroy_functions {
  int (*unmap_f)(struct cell *cell,
                 const struct jailhouse_memory *mem);
  int (*remap_root_f)(const struct jailhouse_memory* mem, enum failure_mode mode);
};


typedef union{
  struct create_functions create_f;
  struct destroy_functions destroy_f;
  /* unmap_from_root_cell if cell is starting and mem is loadable*/
  int (*unmap_root_f)(const struct jailhouse_memory* mem);
  /* remap_to_root_cell if the cell is loadable to permit the root cell to
   * load the image */
  int (*remap_root_f)(const struct jailhouse_memory* mem, enum failure_mode mode);
  /* Flush operation used during D-Cache operation */
  enum dcache_flush flush;
} col_manage_functions;


/**
 * Manage a colored memory region based on the operation to do
 * @see enum col_operation.
*/
int manage_colored_regions(const struct jailhouse_memory_colored col_mem,
				struct cell *cell, col_manage_functions *functions,
				col_operation type);

#endif /* _JAILHOUSE_COLORING_H */
