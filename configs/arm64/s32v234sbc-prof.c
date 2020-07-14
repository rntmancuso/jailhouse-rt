/*
 * Jailhouse, a Linux-based partitioning hypervisor
 *
 * Configuration for profiling inmate on NXP S32V234 EVB eval board:
 * 1 CPU, ~1 GB or RAM, main UART, DDRC control registers
 *
 * Assuming the rootcell is s32v234sbc-rootprof, the memory layout is:
 * 0x80000000 -> 0xc0000000  (1GB, DDR0): Linux/root-cell
 * 0xc0000000 -> 0xfc000000  (DDR1)     : Profiling buffer for inmate 
 * 0xfc000000 -> 0xfff00000  (DDR1)     : Hypervisor memory
 * 0xfff00000 -> 0xfff01000  (DDR1)     : Fake UART page
 * 0xfff01000 -> 0x100000000 (DDR1)     : Loadable img mem. for inmate
 *
 * Copyright (c) Siemens AG, 2016
 *
 * Authors:
 *  Jan Kiszka <jan.kiszka@siemens.com>
 *  Renato Mancuso <rmancuso@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <jailhouse/types.h>
#include <jailhouse/cell-config.h>

#define ARRAY_SIZE(a) sizeof(a) / sizeof(a[0])

struct {
	struct jailhouse_cell_desc cell;
	__u64 cpus[1];
	struct jailhouse_memory mem_regions[7];
} __attribute__((packed)) config = {
	.cell = {
		.signature = JAILHOUSE_CELL_DESC_SIGNATURE,
		.revision = JAILHOUSE_CONFIG_REVISION,
		.name = "S32 DRAM Profiler",
		.flags = JAILHOUSE_CELL_PASSIVE_COMMREG,

		.cpu_set_size = sizeof(config.cpus),
		.num_memory_regions = ARRAY_SIZE(config.mem_regions),
		.num_irqchips = 0,
		.num_pio_regions = 0,
		.num_pci_devices = 0,
	},

	.cpus = {
		0x8,
	},

	.mem_regions = {
		/* UART */ {
			.phys_start = 0x40053000,
			.virt_start = 0x40053000,
			.size = 0x1000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_IO | JAILHOUSE_MEM_ROOTSHARED,
		},
		/* TODO: add aperture for DDR0/1 profiling registers*/

		/* MMDC0 */ {
			.phys_start = 0x40036000,
			.virt_start = 0x40036000,
			.size = 0x1000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_IO | JAILHOUSE_MEM_ROOTSHARED,
		},

		/* MMDC1 */ {
			.phys_start = 0x400A2000,
			.virt_start = 0x400A2000,
			.size = 0x1000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_IO | JAILHOUSE_MEM_ROOTSHARED,
		},
		
		/* FAKE UART SPACE */ {
			.phys_start = 0xfff00000,
			.virt_start = 0x0000face,
			.size = 0x00001000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE,
		},
		/* RAM for INMATE loadable image */ {
			.phys_start = 0xfff01000,
			.virt_start = 0,
			.size = 0x000ff000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_EXECUTE | JAILHOUSE_MEM_LOADABLE,
		},		
		/* RAM for profile log */ {
			.phys_start = 0xc0000000,
			.virt_start = 0x50000000, /* See CONFIG_ADDL_REGION */
			//.size = 0x00036000,
			.size = 0x3c000000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
			         JAILHOUSE_MEM_IO | JAILHOUSE_MEM_ROOTSHARED,
		},
		/* communication region */ {
			.virt_start = 0x80000000,
			.size = 0x00001000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_COMM_REGION,
		},
	}
};
