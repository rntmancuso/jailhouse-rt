/*
 * Jailhouse AArch64 support
 * Cache coloring
 *
 * Copyright (C) 2018 Università di Modena e Reggio Emilia
 *
 * Authors:
 *   Luca Miccio <lucmiccio@gmail.com>
 *
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <asm/coloring.h>
#include <jailhouse/coloring-common.h>

int manage_colored_regions(const struct jailhouse_memory_colored col_mem,
				struct cell *cell, col_manage_functions *functions,
				col_operation type)
{
	int MAX_COLORS;
	int err = 0;
	struct jailhouse_color_desc colors_desc = \
						system_config->platform_info.coloring_desc;
	struct jailhouse_memory frag_mem_region;
	__u64 f_size = colors_desc.fragment_unit_size;
	__u64 f_offset = colors_desc.fragment_unit_offset;
	__u64 colors = col_mem.colors;
	__u64 phys_start = col_mem.memory.phys_start;
	__u64 virt_start = col_mem.memory.virt_start;
	__u64 flags = col_mem.memory.flags;
	MAX_COLORS = f_offset/f_size;
	bool mask[MAX_COLORS];
	unsigned long region_addr, region_size, size;
	unsigned long vaddr = TEMPORARY_MAPPING_BASE +
		this_cpu_id() * PAGE_SIZE * NUM_TEMPORARY_PAGES;
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
					err = functions->create_f.unmap_root_f(&frag_mem_region);
					if (err)
						return err;
				}

				if (JAILHOUSE_MEMORY_IS_SUBPAGE(&frag_mem_region))
					err = functions->create_f.subpage_f(cell, &frag_mem_region);
				else
					err = functions->create_f.map_f(cell, &frag_mem_region);

				if(err)
					return err;
			break;

			case DESTROY:
				if (!JAILHOUSE_MEMORY_IS_SUBPAGE(&frag_mem_region)){
					err = functions->destroy_f.unmap_f(cell, &frag_mem_region);
					if(err)
						return err;
				}

				if (!(frag_mem_region.flags & (JAILHOUSE_MEM_COMM_REGION |
						JAILHOUSE_MEM_ROOTSHARED))){
					err = functions->destroy_f.remap_root_f(&frag_mem_region,
								WARN_ON_ERROR);
					if(err)
						return err;
				}
			break;

			case START:
				if (frag_mem_region.flags & JAILHOUSE_MEM_LOADABLE) {
					err = functions->unmap_root_f(&frag_mem_region);
					if(err)
						return err;
				}
			break;

			case LOADABLE:
				if (frag_mem_region.flags & JAILHOUSE_MEM_LOADABLE) {
					err = functions->remap_root_f(&frag_mem_region, ABORT_ON_ERROR);
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
					paging_create(&hv_paging_structs, region_addr, size,
						      vaddr, PAGE_DEFAULT_FLAGS,
						      PAGING_NON_COHERENT);

					arm_dcaches_flush((void *)vaddr, size,
										functions->flush);

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
