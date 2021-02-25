/*
 * Jailhouse, a Linux-based partitioning hypervisor
 *
 * Configuration for Xilinx ZynqMP ZCU102 eval board with colored
 * root-cell memory dynamically colored at activation time.
 *
 * Copyright (c) Siemens AG, 2016
 *
 * Authors:
 *  Jan Kiszka <jan.kiszka@siemens.com>
 *  Renato Mancuso (BU) <rmancuso@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Reservation via device tree: 0x800000000..0x83fffffff
 */

#define COMM_PHYS_ADDR      (0x87c000000)

#include <jailhouse/types.h>
#include <jailhouse/cell-config.h>

struct {
	struct jailhouse_system header;
	__u64 cpus[1];
	struct jailhouse_memory mem_regions[16];
	struct jailhouse_memory_colored col_mem[1];
	struct jailhouse_irqchip irqchips[1];
	struct jailhouse_pci_device pci_devices[2];
	__u32 stream_ids[12];
} __attribute__((packed)) config = {
	.header = {
		.signature = JAILHOUSE_SYSTEM_SIGNATURE,
		.revision = JAILHOUSE_CONFIG_REVISION,
		.flags = JAILHOUSE_SYS_VIRTUAL_DEBUG_CONSOLE,
		.hypervisor_memory = {
			.phys_start = 0x087f000000,
			.size =       0x0001000000,
		},
		.debug_console = {
			.address = 0xff000000,
			.size = 0x1000,
			.type = JAILHOUSE_CON_TYPE_XUARTPS,
			.flags = JAILHOUSE_CON_ACCESS_MMIO |
				 JAILHOUSE_CON_REGDIST_4,
		},
		.platform_info = {
			.pci_mmconfig_base = 0xfc000000,
			.pci_mmconfig_end_bus = 0,
			.pci_is_virtual = 1,
			.pci_domain = -1,
			.iommu_units = {
				{
					.type = JAILHOUSE_IOMMU_SMMUV2,
					.base = 0xFD800000,
					.size = 0x00100000,
				},
			},
			.arm = {
				.gic_version = 2,
				.gicd_base = 0xf9010000,
				.gicc_base = 0xf902f000,
				.gich_base = 0xf9040000,
				.gicv_base = 0xf906f000,
				.maintenance_irq = 25,
			},
		},
		.root_cell = {
			.name = "ZynqMP-ZCU102",

			.cpu_set_size = sizeof(config.cpus),
			.num_memory_regions = ARRAY_SIZE(config.mem_regions),
			.num_memory_regions_colored = ARRAY_SIZE(config.col_mem),
			.num_irqchips = ARRAY_SIZE(config.irqchips),
			.num_pci_devices = ARRAY_SIZE(config.pci_devices),
			.num_stream_ids = ARRAY_SIZE(config.stream_ids),

			.vpci_irq_base = 136-32,
		},
	},

	.cpus = {
		0xf,
	},

	.mem_regions = {
		/* IVSHMEM shared memory region for 0001:00:00.0 */
		JAILHOUSE_SHMEM_NET_REGIONS(0x87d000000, 0),
		/* IVSHMEM shared memory region for 0001:00:01.0 */
		JAILHOUSE_SHMEM_NET_REGIONS(0x87e000000, 0),
		/* MMIO (permissive) */ {
			.phys_start = 0xfd000000,
			.virt_start = 0xfd000000,
			.size =	      0x03000000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_IO | JAILHOUSE_MEM_ROOTSHARED,
		},
		/* RAM - Low DDR*/ {
			.phys_start = 0x40000000,
			.virt_start = 0x40000000,
			.size = 0x40000000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_EXECUTE,
		},
		/* RAM - High DDR*/ {
			.phys_start = 0x800000000,
			.virt_start = 0x800000000,
			.size = 0x7c000000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_EXECUTE,
		},
		/* PCI host bridge */ {
			.phys_start = 0x8000000000,
			.virt_start = 0x8000000000,
			.size = 0x1000000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_IO,
		},
		/* For LPD Port */ {
		  .phys_start = 0x80000000,
		  .virt_start = 0x80000000,
		  .size = 0x4000,
		  .flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
		  JAILHOUSE_MEM_EXECUTE | JAILHOUSE_MEM_IO,
		},
		/* For HPM0 Port */ {
		  .phys_start = 0x1100000000,
		  .virt_start = 0x1100000000,
		  .size = 0x40000000,
		  .flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE | JAILHOUSE_MEM_EXECUTE,
		},
		/* For HPM1 Port */ {
		  .phys_start = 0x4800000000,
		  .virt_start = 0x4800000000,
		  .size = 0x7c000000,
		  .flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE | JAILHOUSE_MEM_EXECUTE,
		},
		/* Control interface */ {
			.phys_start = COMM_PHYS_ADDR,
			.virt_start = COMM_PHYS_ADDR,
			.size = 0x00004000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
			         JAILHOUSE_MEM_IO,
		},
	},

	.col_mem = {
		{
			/* Linux RAM */
			.memory = {
				.phys_start = 0x1000000000,
				.virt_start = 0x0,
				.size = 0x0020000000,//0x0040000000, /* 1024 MB - max virt : 0x003fffc000 phys : 0x10ffff0000*/
				.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_EXECUTE,
			},

			/* Assigning 1/4 of the colors */
			.colors = 0xf000,
			.rebase_offset = 0x1000000000,
		},
	},

	.irqchips = {
		/* GIC */ {
			.address = 0xf9010000,
			.pin_base = 32,
			.pin_bitmap = {
				0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
			},
		},
	},

	.pci_devices = {
		/* 0001:00:01.0 */ {
			.type = JAILHOUSE_PCI_TYPE_IVSHMEM,
			.domain = 1,
			.bdf = 1 << 3,
			.bar_mask = JAILHOUSE_IVSHMEM_BAR_MASK_INTX,
			.shmem_regions_start = 0,
			.shmem_dev_id = 0,
			.shmem_peers = 2,
			.shmem_protocol = JAILHOUSE_SHMEM_PROTO_VETH,
		},
		/* 0001:00:02.0 */ {
			.type = JAILHOUSE_PCI_TYPE_IVSHMEM,
			.domain = 1,
			.bdf = 2 << 3,
			.bar_mask = JAILHOUSE_IVSHMEM_BAR_MASK_INTX,
			.shmem_regions_start = 4,
			.shmem_dev_id = 0,
			.shmem_peers = 2,
			.shmem_protocol = JAILHOUSE_SHMEM_PROTO_VETH,
		},
	},

	.stream_ids = {
		/* In SMMUv2, the IDs are a list of pairs of IDs and
		 * masks to match against. It is important that there
		 * is no ambiguity otherwise the SMMU will raise a
		 * multiple match and the translation will fail. In
		 * the ZCU102, the prefix of the sIDs are always the
		 * TBU (0-5) numbers used by the peripherals (Table
		 * 16-3), so we build a matching table based on that.
		 */
		/* TBU0:
		   S_AXI_HPC{0, 1}_FPD
		   SMMU TCU
		   CoreSight
		 */
		(0 << 10) | 0, ((1 << 5) - 1) << 10,

		/* TBU1:
		   SIOU peripheral's DMA units
		 */
		(1 << 10) | 0, ((1 << 5) - 1) << 10,

		/* TBU2:
		   LPD
		 */
		(2 << 10) | 0, ((1 << 5) - 1) << 10,

		/* TBU3:
		   S_AXI_HP0_FPD
		   DisplayPort
		 */
		(3 << 10) | 0, ((1 << 5) - 1) << 10,

		/* TBU4:
		   S_AXI_HP{1, 2}_FPD
		 */
		(4 << 10) | 0, ((1 << 5) - 1) << 10,

		/* TBU5:
		   S_AXI_HP3_FPD
		   FPD DMA
		 */
		(5 << 10) | 0, ((1 << 5) - 1) << 10,

	},
};
