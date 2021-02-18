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

#define SCHIM_RECOLORING_ENABLE 1

#define col_print(fmt, ...)			\
	printk("[COL] " fmt, ##__VA_ARGS__)

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
	.smmu_map_f = NULL, /* Will be initialized by the SMMU support */
	.subpage_f = mmio_subpage_register,
	.unmap_root_f = unmap_from_root_cell,
	.unmap_f = arch_unmap_memory_region,
	.smmu_unmap_f = NULL, /* Will be initialized by the SMMU support */
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
 *   Perform a copy of memory from a non-colored space to a colored
 *   space. The two spaces could be overlapping in physical memory, so
 *   go in reverse. Also map the contiguous space a bit at a time to
 *   take it easy on the pool pages.
 */
static void colored_copy(const struct jailhouse_memory_colored * col_mem)
{
	if(SCHIM_RECOLORING_ENABLE) {
//		col_print("\tCopy in progress\n");
		unsigned long phys_addr, virt_addr, tot_size, size;
		int i;
//		col_print("\tline 176\n");
		tot_size = col_mem->memory.size;
//		col_print("\tline 178\n");
		/* Find the first page that does not belong to the non-colored
		 * mapping */
		phys_addr = col_mem->memory.phys_start + tot_size;
		virt_addr = col_mem->memory.virt_start + tot_size;
//		col_print("\tline 182\n");
		while (tot_size > 0) {
			size = MIN(tot_size,
				   NUM_TEMPORARY_PAGES * PAGE_SIZE);
//		    col_print("\tline 186\n");
			phys_addr -= size;
			virt_addr -= size;
			/* If we have reached the beginning (end of copy),
			 * make sure we do not exceed the boundary of the
			 * region */
			if (phys_addr < col_mem->memory.phys_start)
				phys_addr = col_mem->memory.phys_start;
		   if (virt_addr < col_mem->memory.virt_start)
			   virt_addr = col_mem->memory.virt_start;
//			col_print("\tline 193\n");
			/* cannot fail, mapping area is preallocated */
			paging_create(&this_cpu_data()->pg_structs, phys_addr,
				      size, TEMPORARY_MAPPING_BASE,
				      PAGE_DEFAULT_FLAGS,
				      PAGING_NON_COHERENT | PAGING_NO_HUGE);
//		    col_print("\tline 199\n");
			/* Actual data copy operation */
			for (i = (size >> PAGE_SHIFT) - 1; i >= 0; --i) {
				/* Destination: colored mapping created via HV_CREATE */
				/* Source: non-colored mapping defined above */
//				col_print("\t\t @ %d : memcpy(%lx, %lx, %x)\n", i, (ROOT_MAP_OFFSET + virt_addr + (i << PAGE_SHIFT)), (TEMPORARY_MAPPING_BASE + (i << PAGE_SHIFT)), PAGE_SIZE);
//				col_print("\t\t dest = %lx + %lx + (%x << %x)\n", ROOT_MAP_OFFSET, phys_addr, i, PAGE_SHIFT);
//				col_print("\t\t src  = %lx + (%x << %x)\n", TEMPORARY_MAPPING_BASE, i, PAGE_SHIFT);
				//col_print("\t\t Content @ %lx = %x\n", phys_addr, *((unsigned*)phys_addr));
//				memcpy((void *)ROOT_MAP_OFFSET + phys_addr + (i << PAGE_SHIFT),
//				       (void *)TEMPORARY_MAPPING_BASE + (i << PAGE_SHIFT),
//				       PAGE_SIZE);
				memcpy((void *)ROOT_MAP_OFFSET + virt_addr + (i << PAGE_SHIFT),
					   (void *)TEMPORARY_MAPPING_BASE + (i << PAGE_SHIFT),
					   PAGE_SIZE);
			}
//			col_print("\tline 208\n");
			tot_size -= size;
		}
	}
	else {
		col_print("\tCopy skipped\n");
	}

}

static void colored_uncopy(const struct jailhouse_memory_colored * col_mem)
{
	if(SCHIM_RECOLORING_ENABLE) {
		col_print("\tUncopy in progress\n");
		unsigned long phys_addr, virt_addr, tot_size, size;
		int i;

		tot_size = col_mem->memory.size;

		/* Find the first page in the non-colored mapping */
		phys_addr = col_mem->memory.phys_start;
		virt_addr = col_mem->memory.virt_start;

		while (tot_size > 0) {
			size = MIN(tot_size,
				   NUM_TEMPORARY_PAGES * PAGE_SIZE);

			/* cannot fail, mapping area is preallocated */
			paging_create(&this_cpu_data()->pg_structs, phys_addr,
				      size, TEMPORARY_MAPPING_BASE,
				      PAGE_DEFAULT_FLAGS,
				      PAGING_NON_COHERENT | PAGING_NO_HUGE);

			/* Actual data copy operation */
			for (i = 0; i < (size >> PAGE_SHIFT); ++i) {
				/* Destination: colored mapping created via HV_CREATE */
				/* Source: non-colored mapping defined above */
//				memcpy((void *)TEMPORARY_MAPPING_BASE + (i << PAGE_SHIFT),
//				       (void *)ROOT_MAP_OFFSET + phys_addr + (i << PAGE_SHIFT),
//				       PAGE_SIZE);
			   memcpy((void *)TEMPORARY_MAPPING_BASE + (i << PAGE_SHIFT),
				       (void *)ROOT_MAP_OFFSET + virt_addr + (i << PAGE_SHIFT),
				       PAGE_SIZE);
			}

			phys_addr += size;
			virt_addr += size;
			tot_size -= size;
		}
	}
	else {
		col_print("\tUncopy skipped\n");
	}

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
	int err = -EINVAL;
	struct jailhouse_memory frag_mem_region;
	__u64 f_size = cache.fragment_unit_size;
	__u64 f_offset = cache.fragment_unit_offset;
	__u64 colors = col_mem.colors;
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


	r = 0;
	/* for (r = 0; r < (int)(col_mem.memory.size/f_offset); r++) */
	while (virt_start < col_mem.memory.virt_start + col_mem.memory.size) {
		for (k =0; k < MAX_COLORS*2; k+=2) {

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

#if 0
			col_print("V: 0x%08llx -> P: 0x%08llx (size = 0x%08llx)\n",
				  frag_mem_region.virt_start, frag_mem_region.phys_start,
				  frag_mem_region.size);
#endif

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

			case HV_CREATE:

				/* Map colored region that is linearly
				 * mapped from the HV's point of
				 * view. It will be used to copy the
				 * content of the physical memory of
				 * the root cell. */
				err = paging_create(&this_cpu_data()->pg_structs,
						    frag_mem_region.phys_start,
						    frag_mem_region.size,
						    frag_mem_region.virt_start + ROOT_MAP_OFFSET,
						    PAGE_DEFAULT_FLAGS, PAGING_NON_COHERENT);

				if(err)
					return err;
				break;

			case SMMU_CREATE:

				/* Throw an error if no SMMU mapping function was installed */
				if (!col_ops.smmu_map_f)
					return -ENOSYS;

				err = col_ops.smmu_map_f(cell, &frag_mem_region);

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

			case HV_DESTROY:

				err = paging_destroy(&this_cpu_data()->pg_structs,
						     frag_mem_region.virt_start + ROOT_MAP_OFFSET,
						     frag_mem_region.size,
						     PAGING_NON_COHERENT);

				if(err)
					return err;

				break;

			case SMMU_DESTROY:
				/* Throw an error if no SMMU mapping function was installed */
				if (!col_ops.smmu_unmap_f)
					return -ENOSYS;

				/* TODO: implement this */

				break;

			case START:
				if (frag_mem_region.flags & JAILHOUSE_MEM_LOADABLE) {

					/* Correct fragment geometry
					 * to be located far away from
					 * useful memory */
					frag_mem_region.virt_start += ROOT_MAP_OFFSET;

					err = arch_unmap_memory_region(&root_cell, &frag_mem_region);
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

					if(err)
						return err;
				}
				break;

			case DCACHE:
				region_addr = frag_mem_region.phys_start;
				region_size = frag_mem_region.size;

				err = 0;

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

		++r;
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
			  "(SIZE: 0x%08llx, COL: 0x%08llx, extra: %p)\n",
			  type, col_mem->memory.phys_start,
			  col_mem->memory.virt_start,
			  col_mem->memory.size, col_mem->colors, extra);

		err = __manage_colored_region(*col_mem, cell, type, extra);

		col_print("Result: %d\n", err);
		if(err)
			return err;
	}

	return 0;
}

static void coloring_cell_exit(struct cell *cell)
{
	int n, err;
	const struct jailhouse_memory_colored *col_mem;

	/* Free up this mapping first, to take it easy on pool
	 * pages */
	coloring_cell_destroy(cell);

	/* If this was the root-cell, then we need to un-do coloring
	 * of the memory already loaded for Linux. Just to be safe,
	 * un-do coloring for any colored memory area. */
	if (cell == &root_cell) {
		for_each_col_mem_region(col_mem, cell->config, n) {
			/* Create a linear mapping of the colored
			 * region for the hypervisor */
			err = __manage_colored_region(*col_mem, cell, HV_CREATE, NULL);

			if(err) {
				col_print("ERROR: HV_CREATE returned %d\n", err);
 				return;
			}

			/* Ready to map a contiguous view of the
			 * memory that needs to be copy-colored, but
			 * do this in little steps because the colored
			 * mapping likely used quite a bit of pool
			 * pages. */
			col_print("\tPerfoming color rewinding of root-cell...\n");
			colored_uncopy(col_mem);
			col_print("\tDone!\n");

			/* Alroght. We can now release all the temporary mappings */
			err = __manage_colored_region(*col_mem, cell, HV_DESTROY, NULL);

			if(err) {
				col_print("ERROR: HV_DESTROY returned %d\n", err);
 				return;
			}

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

	/* If this was the root-cell, then we need to perform coloring
	 * of the memory already loaded for Linux. Just to be safe,
	 * expand any colored memory area. */
	if (cell == &root_cell) {
		for_each_col_mem_region(col_mem, cell->config, n) {
			/* Expand colored memory regions */
			/* NOTE: we better have a working
			 * coloring-aware SMMU here. */
			err = __manage_colored_region(*col_mem, cell, HV_CREATE, NULL);

			if(err) {
				col_print("ERROR: HV_CREATE returned %d\n", err);
 				return err;
			}

			/* Ready to map a contiguous view of the
			 * memory that needs to be copy-colored, but
			 * do this in little steps because the colored
			 * mapping likely used quite a bit of pool
			 * pages. */
			col_print("\tPerfoming dynamic recoloring of root-cell...\n");
			colored_copy(col_mem);
			col_print("\tDone!\n");

			/* Alright. We can now release all the temporary mappings */
			err = __manage_colored_region(*col_mem, cell, HV_DESTROY, NULL);

			if(err) {
				col_print("ERROR: HV_DESTROY returned %d\n", err);
 				return err;
			}
		}
	}

	/* Do this after the colored-copy, to reduce the likelihood
	 * that it will fail due to a lack of pool pages needed to
	 * maintain the colored mapping. */
	err = coloring_cell_create(cell);

	if (err)
		return err;

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
