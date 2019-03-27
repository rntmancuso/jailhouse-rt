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

#include <jailhouse/coloring.h>
#include "coloring.h"

extern struct cell *root_cell;

/**
 * Global coloring mask.
 * This value is calculated when enabling the hypervisor.
 */
static unsigned long coloring_mask;

/**
 * Pointer to the colored memory region defined in the root cell
 * configuration. This field is private and it is initialized during
 * the coloring_init function.
 */
static const struct jailhouse_memory *root_colored_memory;

/**
 * Get the first memory region in the root cell that is defined as
 * JAILHOUSE_MEM_ROOT_COLORED.
 */
static const struct jailhouse_memory *root_cell_memory(void)
{
	const struct jailhouse_memory *col_mem = root_cell->memory_regions;
	int i = 0;

	for (i = 0; i < root_cell->num_memory_regions; i++, col_mem++)
		if (col_mem->flags & JAILHOUSE_MEM_COLORED)
			return col_mem;

	return 0;
}

/**
 * Simulate coloring allocation in order to get the last address of the memory.
 *
 * @param start memory start address
 * @param size memory size
 * @param col_val coloring value
 * @return last address based on col_val
 */
static unsigned long simulate_coloring(unsigned long start,
	unsigned long size, unsigned long col_val)
{
	unsigned long end = start & HV_PAGE_MASK;

	while (size > 0) {
		end = driver_next_colored(end, col_val);
		// We assume that each step we allocate PAGE_SIZE chunks
		end  += HV_PAGE_SIZE;
		size -= HV_PAGE_SIZE;
	}

	return end;
}

/**
 * Check if the colored memory region fits inside the root colored region.
 *
 * @param mem_to_check Colored memory to check
 * @return int Error value, 0 if the memory fits in.
 */
static int col_mem_bounded(struct jailhouse_memory *mem_to_check)
{
	unsigned long long phys_end;
	unsigned long long size;
	unsigned long long coloring_mem_bound;
	int err = 0;

	if (!root_colored_memory || !mem_to_check)
		return -ENOMEM;

	phys_end = root_colored_memory->phys_start;
	size = mem_to_check->size;
	coloring_mem_bound = root_colored_memory->phys_start +
		root_colored_memory->size;

	phys_end = simulate_coloring(phys_end, size, mem_to_check->colors);

	if (phys_end > coloring_mem_bound) {
		pr_info("Error MEMORY COLORED UNBOUDED");
		pr_info("0x%llx > 0x%llx", phys_end, coloring_mem_bound);
		err = -ENOMEM;
	}

	return err;
}

static bool address_in_region(unsigned long addr,
			      const struct jailhouse_memory *region)
{
	return addr >= region->phys_start &&
	       addr < (region->phys_start + region->size);
}

/**
 * Check if the memory overalaps the root colored memory, if exists.
 */
static int mem_root_overlap(struct jailhouse_memory *mem)
{
	int err = 0;
	unsigned long phys_end;

	if (!root_colored_memory)
		return err;

	if (!mem)
		return -ENOMEM;

	if (address_in_region(mem->phys_start, root_colored_memory))
		err = -ENOMEM;

	phys_end = simulate_coloring(mem->phys_start, mem->size, mem->colors);

	if (address_in_region(phys_end, root_colored_memory))
		err = -ENOMEM;

	return err;
}

/**
 * Init the coloring sub-system.
 * Calculate and setup the coloring mask variable based on the last level cache
 * way size provided by the root cell configuration.
 *
 * @param llc_way_size Last level cache way size.
 */
void jailhouse_coloring_init(unsigned int llc_way_size)
{
	pr_info("Coloring: Init with %u bytes of LLC way size", llc_way_size);
	coloring_mask = calculate_addr_col_mask(llc_way_size);
	pr_info("Coloring: Mask calculated is 0x%lx", coloring_mask);

	if (!coloring_mask)
		return;

	pr_info("Coloring: Searching root colored region");
	root_colored_memory = root_cell_memory();

	if (root_colored_memory)
		pr_info("Coloring: Root colored region found!");
	else
		pr_info("Coloring: Root colored region NOT found!");

	pr_info("Coloring: Colors available: %u",
			(unsigned int)((coloring_mask >> HV_PAGE_SHIFT) + 1));
}

/**
 * Setup cell's colored memory region(s)
 *
 * This function must be always called after `jailhouse_coloring_init`.
 */
int jailhouse_coloring_cell_setup(struct cell *cell,
	const struct jailhouse_cell_desc *cell_desc)
{
	struct jailhouse_memory *col_mem;
	int count = 0;
	int err = 0;
	unsigned long long max_color_val = 0;
	unsigned long long available_colors = 0;

	col_mem = cell->memory_regions;
	available_colors = (coloring_mask >> HV_PAGE_SHIFT) + 1;
	max_color_val = (1ULL << available_colors) - 1;
	for (count = 0; count < cell->num_memory_regions; count++, col_mem++) {
		if (col_mem->flags & JAILHOUSE_MEM_COLORED_CELL) {

			/* Root cell colored regions are not supported (yet) */
			if (!cell->id) {
				col_mem->flags &= ~JAILHOUSE_MEM_COLORED_CELL;
				continue;
			}

			if (!coloring_mask) {
				pr_info("Error: Coloring is not active");
				return -ENOMEM;
			}

			if (!col_mem->colors) {
				pr_info("Error: colors set to 0");
				return -ENOMEM;
			}

			if (col_mem->colors > max_color_val) {
				pr_info("Error: Memory color value exceeds the max. value available");
				return -ENOMEM;
			}

			if (col_mem->phys_start) {
				pr_info("WARNING: You are using a custom colored memory. Use at your own risk.");

				// Do not allow memory overlapping
				if (mem_root_overlap(col_mem))
					return -ENOMEM;

				continue;
			}

			if (!root_colored_memory) {
				pr_info("No root cell colored region found.");
				return -ENOMEM;
			}

			col_mem->phys_start = root_colored_memory->phys_start;
			err = col_mem_bounded(col_mem);

			if (err)
				break;
		}
	}

	if (count)
		memcpy((void *)jailhouse_cell_mem_regions(cell_desc),
		cell->memory_regions,
		sizeof(struct jailhouse_memory) * cell->num_memory_regions);

	return err;
}

unsigned long long driver_next_colored(unsigned long phys,
	unsigned long col_val)
{
	return next_colored(phys, coloring_mask, col_val);
}
