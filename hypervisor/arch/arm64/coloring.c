/*
 * Jailhouse AArch64 support
 *
 * Authors:
 *  Renato Mancuso (BU) <rmancuso@bu.edu>
 *  Luca Miccio <lucmiccio@gmail.com>
 *
 * This file implements coloring in Aarch64 systems. It is implemented
 * as a unit so that cache identification is performed at setup
 * time. The code is an adaptation from the code originally proposed
 * by Luca Miccio and extended to handle SMMU configuration and
 * dynamic root cell coloring.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See the
 * COPYING file in the top-level directory.
 */

#include <jailhouse/control.h>
#include <jailhouse/paging.h>
#include <jailhouse/printk.h>
#include <jailhouse/string.h>
#include <asm/control.h>
#include <jailhouse/unit.h>
#include <jailhouse/cell.h>
#include <jailhouse/mmio.h>
#include <asm/coloring.h>

#define col_print printk
#define COL_DEBUG 1

#define MAX_CACHE_LEVELS            (7)
#define CLIDR_CTYPE(n)              GENMASK(3*(n-1)+2, 3*(n-1))
enum clidr_ctype {
	CLIDR_CTYPE_NOCACHE,
	CLIDR_CTYPE_IONLY,
	CLIDR_CTYPE_DONLY,
	CLIDR_CTYPE_IDSPLIT,
	CLIDR_CTYPE_UNIFIED,
};

#define CSSELR_LEVEL                GENMASK(3, 1)
#define CSSELR_IND                  BIT(0)

#define CCSIDR_LINE_SIZE            GENMASK(2, 0)
#define CCSIDR_ASSOC                GENMASK(12, 3)
#define CCSIDR_NUM_SETS             GENMASK(27, 13)

#define manage_colored_region(col_mem, cell, type)	\
    __manage_colored_regions(col_mem, cell, type, NULL)

#define flush_colored_region(col_mem, cell, flush)	\
    __manage_colored_regions(col_mem, cell, DCACHE, (void *)flush)

const char * cache_types[] = {"Not present", "Instr. Only", "Data Only", "I+D Split", "Unified"};

/* An SMMUv2 instance */
static struct cache {
	u64 fragment_unit_size;
	u64 fragment_unit_offset;
	/* Total size of the cache in bytes */
	u64 size;
	/* Size of a single way in bytes */
	u64 line_size;
	/* Size of each cache line in bytes */
	u64 way_size;
	/* Associativity */
	u32 assoc;
	/* Max number of colors supported by this cache */
	u64 colors;
	/* Which level is this cache at */
	int level;
} cache;

/* This is a global struct initialized at setup time */
struct col_manage_ops col_ops = {
	.map_f = arch_map_memory_region,
	.subpage_f = mmio_subpage_register,
	.unmap_root_f = unmap_from_root_cell,
	.unmap_f = arch_unmap_memory_region,
	.remap_root_f = remap_to_root_cell,
};

static int coloring_cache_detect(void)
{
	/* First, parse CLIDR_EL1 to understand how many levels are
	 * present in the system. */
	u64 reg, type;
	int i;
	
	arm_read_sysreg(clidr_el1, reg);

	/* Initialize this field to detect when no suitable caches
	 * have been found */
	cache.level = -1;
	
	for (i = 1; i <= MAX_CACHE_LEVELS; ++i) {
		u64 geom, assoc, ls, sets;
		type = FIELD_GET(CLIDR_CTYPE(i), reg);
		col_print("\tL%d Cache Type: %s\n", i, cache_types[type]);

		if (type == CLIDR_CTYPE_NOCACHE)
			continue;

		/* Fetch additional info about this cache level */
		arm_write_sysreg(csselr_el1, FIELD_PREP(CSSELR_LEVEL, i-1));
		arm_read_sysreg(ccsidr_el1, geom);

		/* Parse info about this level */
		ls = 1 << (4 + FIELD_GET(CCSIDR_LINE_SIZE, geom));
		assoc = FIELD_GET(CCSIDR_ASSOC, geom) + 1;
		sets = FIELD_GET(CCSIDR_NUM_SETS, geom) + 1;

		col_print("\t\tTotal size: %lld\n", ls * assoc * sets);
		col_print("\t\tLine size: %lld\n", ls);
		col_print("\t\tAssoc.: %lld\n", assoc);
		col_print("\t\tNum. sets: %lld\n", sets);

		if (type == CLIDR_CTYPE_IDSPLIT) {
			arm_write_sysreg(csselr_el1, FIELD_PREP(CSSELR_LEVEL, i-1) | CSSELR_IND);
			arm_read_sysreg(ccsidr_el1, geom);

			ls = 1 << (4 + FIELD_GET(CCSIDR_LINE_SIZE, geom));
			assoc = FIELD_GET(CCSIDR_ASSOC, geom) + 1;
			sets = FIELD_GET(CCSIDR_NUM_SETS, geom) + 1;
			
			col_print("\t\tTotal size (I): %lld\n", ls * assoc * sets);
			col_print("\t\tLine size (I): %lld\n", ls);
			col_print("\t\tAssoc. (I): %lld\n", assoc);
			col_print("\t\tNum. sets (I): %lld\n", sets);

		}
		
		/* Perform coloring at the last unified cache level */
		if (type == CLIDR_CTYPE_UNIFIED) {
			cache.level = i;
			
			cache.size = ls * assoc * sets;
			cache.line_size = ls;
			cache.way_size = ls * sets;
			cache.assoc = assoc;
			cache.colors = sets / (PAGE_SIZE / ls);

			/* Compute the max. number of colors */
			col_print("\t\tNum. colors: %lld\n", cache.colors);			
		}
		
	}

	col_print("\tNOTE: L%d Cache selected for coloring.\n", cache.level);
	
	/* Backward compatibility properties. TODO: remove these */
	cache.fragment_unit_size = PAGE_SIZE;
	cache.fragment_unit_offset = cache.way_size;

	return (cache.level == -1);
}

/**
 * Find all the ranges (i,j) s.t (i<=k<=j and mask[k]=1) in mask
 */
static void ranges_in_mask(bool* mask, int size, int* values)
{
	int i = 0, j = 0, k = 0, current_value = 0;

	/* Setup all values to -1 */
	for(i = 0;i < size*2; i++)
		values[i] = -1;
	for (k = 0; k < size; k++) {
		if(mask[k]){
			i = k;
			j = i;
			while(mask[++j] && j < size);
			values[current_value] = i;
			values[++current_value] = j-1;
			current_value++;
			k = j;
		}
	}
}


static int __manage_colored_region(const struct jailhouse_memory_colored col_mem,
				    struct cell *cell, col_operation type, void * extra)
{
	int MAX_COLORS;
	int err = 0;
	struct jailhouse_memory frag_mem_region;
	__u64 f_size = cache.fragment_unit_size;
	__u64 f_offset = cache.fragment_unit_offset;
	__u64 colors = (1 << cache.colors) - 1;
	__u64 phys_start = col_mem.memory.phys_start;
	__u64 virt_start = col_mem.memory.virt_start;
	__u64 flags = col_mem.memory.flags;
	MAX_COLORS = f_offset/f_size;
	bool mask[MAX_COLORS];
	unsigned long region_addr, region_size, size;
	int i, r, k;

	/* Get bit mask from color mask */
	for (i = MAX_COLORS-1; i >= 0; --i, colors >>= 1)
		mask[i] = (colors & 1);

	/* Find all (i,j) s.t i<=j in mask */
	int ranges[MAX_COLORS*2];
	ranges_in_mask(mask,MAX_COLORS,ranges);

	for (r = 0; r < (int)(col_mem.memory.size/f_offset); r++) {
		for (k =0; k < MAX_COLORS*2; k+=2){

			/* Calculate mem region */
			if(ranges[k] == -1)
				continue;

			int i = ranges[k];
			int j = ranges[k+1];

			frag_mem_region.size = (j - i + 1) * f_size;
			frag_mem_region.phys_start = phys_start + (i * f_size) + (r * f_offset);
			frag_mem_region.virt_start = virt_start;
			frag_mem_region.flags = flags;
			virt_start += frag_mem_region.size;

			switch (type) {
			case CREATE:
				if (!(frag_mem_region.flags & (JAILHOUSE_MEM_COMM_REGION |
							       JAILHOUSE_MEM_ROOTSHARED))) {
					err = col_ops.unmap_root_f(&frag_mem_region);
					if (err)
						return err;
				}

				if (JAILHOUSE_MEMORY_IS_SUBPAGE(&frag_mem_region))
					err = col_ops.subpage_f(cell, &frag_mem_region);
				else
					err = col_ops.map_f(cell, &frag_mem_region);

				if(err)
					return err;
				break;

			case DESTROY:
				if (!JAILHOUSE_MEMORY_IS_SUBPAGE(&frag_mem_region)){
					err = col_ops.unmap_f(cell, &frag_mem_region);
					if(err)
						return err;
				}

				if (!(frag_mem_region.flags & (JAILHOUSE_MEM_COMM_REGION |
							       JAILHOUSE_MEM_ROOTSHARED))){
					err = col_ops.remap_root_f(&frag_mem_region,
									     WARN_ON_ERROR);
					if(err)
						return err;
				}
				break;

			case START:
				if (frag_mem_region.flags & JAILHOUSE_MEM_LOADABLE) {
					
					/* Correct fragment geometry
					 * to be located far away from
					 * useful memory */
					frag_mem_region.virt_start += ROOT_MAP_OFFSET;
					
					err = arch_unmap_memory_region(&root_cell, &frag_mem_region);

					/*
					 * err = col_ops.unmap_root_f(&frag_mem_region);
					 */
					
					if(err)
						return err;
				}
				break;

			case LOAD:
				if (frag_mem_region.flags & JAILHOUSE_MEM_LOADABLE) {
					
					/* Correct fragment geometry
					 * to be located far away from
					 * useful memory */
					frag_mem_region.virt_start += ROOT_MAP_OFFSET;

					/* Create an ad-hoc mapping just to load this image */
					err = arch_map_memory_region(&root_cell, &frag_mem_region);
					
					/*
					 * err = col_ops.remap_root_f(&frag_mem_region,
					 * 			   ABORT_ON_ERROR);
					 */
					if(err)
						return err;
				}
				break;

			case DCACHE:
				region_addr = frag_mem_region.phys_start;
				region_size = frag_mem_region.size;

				while (region_size > 0) {
					size = MIN(region_size,
						   NUM_TEMPORARY_PAGES * PAGE_SIZE);

					/* cannot fail, mapping area is preallocated */
					paging_create(&this_cpu_data()->pg_structs, region_addr,
						      size, TEMPORARY_MAPPING_BASE,
						      PAGE_DEFAULT_FLAGS,
						      PAGING_NON_COHERENT | PAGING_NO_HUGE);
					
					arm_dcaches_flush((void *)TEMPORARY_MAPPING_BASE, size,
							  (enum dcache_flush)(extra));
								       
					region_addr += size;
					region_size -= size;
				}
				break;

			default:
				break;
			}
		}
	}

	return err;
}

int __coloring_cell_apply_to_col_mem(struct cell *cell, col_operation type, void * extra)
{

	int err, n;
	const struct jailhouse_memory_colored *col_mem;

	/* No coloring can be performed if no suitable cache level has
	 * been detected */
	if (cache.level == -1 && cell->config->num_memory_regions_colored > 0) {
		printk("ERROR: Colored regions exist but no suitable cache level found.\n");
		return -ENODEV;
	}
	
	for_each_col_mem_region(col_mem, cell->config, n) {
		col_print("Colored OP %d: "
			  "PHYS 0x%08llx -> VIRT 0x%08llx "
			  "(COL: 0x%08llx, extra: %p)\n",
			  type, col_mem->memory.phys_start,
			  col_mem->memory.virt_start, col_mem->colors, extra);

		err = __manage_colored_region(*col_mem, cell, type, extra);

		col_print("Result: %d\n", err);
		if(err)
			return err;
	}
	
	return 0;
}

static void coloring_cell_exit(struct cell *cell)
{
	int n;
	const struct jailhouse_memory_colored *col_mem;

	coloring_cell_destroy(cell);

	/* If this was the root-cell, then we need to un-do coloring
	 * of the memory already loaded for Linux. Just to be safe,
	 * un-do coloring for any colored memory area. */
	if (cell == &root_cell) {
		for_each_col_mem_region(col_mem, cell->config, n) {
			/* TODO compress colored memory regions */
			/* NOTE: we better have a working
			 * coloring-aware SMMU here. */
			
		}
	}	
	
}

static void coloring_shutdown(void)
{
	return coloring_cell_exit(&root_cell);
}

static int coloring_cell_init(struct cell *cell)
{

	int err, n;
	const struct jailhouse_memory_colored *col_mem;

	err = coloring_cell_create(cell);

	if (err)
		return err;

	/* If this was the root-cell, then we need to perform coloring
	 * of the memory already loaded for Linux. Just to be safe,
	 * expand any colored memory area. */
	if (cell == &root_cell) {
		for_each_col_mem_region(col_mem, cell->config, n) {
			/* TODO expand colored memory regions */
			/* NOTE: we better have a working
			 * coloring-aware SMMU here. */
			
			if(err)
				return err;
		}
	}	
	
	return 0;
}
	
static int coloring_init(void)
{
	int ret;
	
	/* Perform cache identification */
	ret = coloring_cache_detect();

	/* If unable to perform coloring, just skip this unit */
	if (ret)
		return 0;
	
	return coloring_cell_init(&root_cell);
}

DEFINE_UNIT_MMIO_COUNT_REGIONS_STUB(coloring);
DEFINE_UNIT(coloring, "Cache Coloring (Aarch64)");
