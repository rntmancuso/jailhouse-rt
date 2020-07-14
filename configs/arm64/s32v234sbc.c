/*
 * Jailhouse, a Linux-based partitioning hypervisor
 *
 * Configuration for NXP S32V234 EVB SoC
 *
 * Copyright (C) 2016 Evidence Srl
 *
 * Authors:
 *  Claudio Scordino <claudio@evidence.eu.com>
 *  Bruno Morelli <b.morelli@evidence.eu.com>
 *  Renato Mancuso <rmancuso@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * NOTE: Add "mem=1984M vmalloc=512M" to the kernel command line.
 */

#include <jailhouse/types.h>
#include <jailhouse/cell-config.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

struct {
	struct jailhouse_system header;
	__u64 cpus[1];
	struct jailhouse_memory mem_regions[4];
	struct jailhouse_irqchip irqchips[1];
	struct jailhouse_pci_device pci_devices[1];
} __attribute__((packed)) config = {
	.header = {
		.signature = JAILHOUSE_SYSTEM_SIGNATURE,
		.revision = JAILHOUSE_CONFIG_REVISION,
		.hypervisor_memory = {
			.phys_start = 0xfc000000,
			.size = 0x3f00000,
		},
		.debug_console = {
			.address = 0x40053000,
			.size = 0x1000,
			.flags = JAILHOUSE_CON1_TYPE_S32 |
			         JAILHOUSE_CON1_ACCESS_MMIO |
				 JAILHOUSE_CON1_REGDIST_4 |
			         JAILHOUSE_CON2_TYPE_ROOTPAGE,
		},
		.platform_info = {
			.pci_mmconfig_base = 0x7e100000,
			.pci_mmconfig_end_bus = 0,
			.pci_is_virtual = 1,
			.pci_domain = -1,
			//.llc_way_size = 0x04000,

			.arm = {
				.gic_version = 2,
				.gicd_base = 0x7d001000,
				.gicc_base = 0x7d002000,
				.gich_base = 0x7d004000,
				.gicv_base = 0x7d006000,
				.maintenance_irq = 25,
			}
		},
		.root_cell = {
			.name = "NXP S32V234",
			.cpu_set_size = sizeof(config.cpus),
			.num_memory_regions = ARRAY_SIZE(config.mem_regions),
			.num_irqchips = ARRAY_SIZE(config.irqchips),
			.num_pci_devices = ARRAY_SIZE(config.pci_devices),
			/* The GICv2 supports up to 480
			 * interrupts. The S32 uses up to 207.
			The root cell will use from 212 to 217. 
			Note: Jailhouse	adds 32 (GIC's SPI) 
			to the .vpci_irq_base , so 180 is the base value*/
			.vpci_irq_base = 180,
		},
	},

	.cpus = {
		0xf,
	},


	.mem_regions = {

		/* MMIO (permissive) */ {
			.phys_start = 0x40000000,
			.virt_start = 0x40000000,
			.size =	      0x00100000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_IO,
		},
		
		/* System RAM */ {
			.phys_start = 0x80000000,
			.virt_start = 0x80000000,
			.size =       0x40000000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_EXECUTE,
		},
		/* System RAM */ {
			.phys_start = 0xc0000000,
			.virt_start = 0xc0000000,
			.size =       0x3c000000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_EXECUTE,
		},

		/* IVSHMEM shared memory region */ {
			.phys_start = 0xfff00000,
			.virt_start = 0xfff00000,
			.size =       0x00100000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE,
		},
	},
	.irqchips = {
		/* GIC */ {
			.address = 0x7d001000,
			.pin_base = 32,
			.pin_bitmap = {
				0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff
			},
		}
	},

	.pci_devices = {
		/* 0001:00:00.0 */ {
			.type = JAILHOUSE_PCI_TYPE_IVSHMEM,
			.domain = 1,
			.bdf = 0x00,
			.bar_mask = {
				0xffffff00, 0xffffffff, 0x00000000,
				0x00000000, 0x00000000, 0x00000000,
			},
			.shmem_region = 3,
			.shmem_protocol = JAILHOUSE_SHMEM_PROTO_VETH,
		},
	},
};
