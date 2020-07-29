/*
 * ARM QoS Support for Jailhouse. Board-specific definitions.
 *
 * Copyright (c) Boston University, 2020
 *
 * Authors:
 *  Renato Mancuso <rmancuso@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#if CONFIG_MACH_NXP_S32 == 1

#define NIC_BASE           (0x40010000UL)
#define NIC_SIZE           (16*4096) /* 64KB Aperture */

#define QOS_DEVICES        12
#define M_FASTDMA1_BASE    (0x2380)
#define M_GPU0_BASE        (0x2480)
#define M_H264DEC_BASE     (0x2580)
#define M_GPU1_BASE        (0x2680)
#define M_CORES_BASE       (0x2780)
#define M_PDI0_BASE        (0x3180)

#define PCI_IB19_BASE      (0x6280)
#define APEX1_IB15_BASE    (0x6380)
#define APEX0_IB16_BASE    (0x6480)
#define H264_IB25_BASE     (0x6580)
#define ENET_IB12_BASE     (0x6680)
#define AXBS_IB36_BASE     (0x6A80)

static const struct qos_device devices [QOS_DEVICES] = {
	{
		.name = "fastdma1",
		.flags = (FLAGS_HAS_RWQOS | FLAGS_HAS_REGUL | FLAGS_HAS_DYNQOS),
		.base = M_FASTDMA1_BASE,
	},
	{
		.name = "gpu0",
		.flags = (FLAGS_HAS_RWQOS | FLAGS_HAS_REGUL | FLAGS_HAS_DYNQOS),
		.base = M_GPU0_BASE,
	},
	{
		.name = "h264dec0",
		.flags = (FLAGS_HAS_RWQOS | FLAGS_HAS_REGUL | FLAGS_HAS_DYNQOS),
		.base = M_H264DEC_BASE,
	},
	{
		.name = "gpu1",
		.flags = (FLAGS_HAS_RWQOS | FLAGS_HAS_REGUL | FLAGS_HAS_DYNQOS),
		.base = M_GPU1_BASE,
	},
	{
		.name = "cores",
		.flags = (FLAGS_HAS_RWQOS | FLAGS_HAS_REGUL | FLAGS_HAS_DYNQOS),
		.base = M_CORES_BASE,
	},
	{
		.name = "pdi0",
		.flags = (FLAGS_HAS_RWQOS | FLAGS_HAS_REGUL),
		.base = M_PDI0_BASE,
	},
	{
		.name = "pci",
		.flags = (FLAGS_HAS_REGUL | FLAGS_HAS_DYNQOS),
		.base = PCI_IB19_BASE,
	},
	{
		.name = "apex1",
		.flags = (FLAGS_HAS_REGUL | FLAGS_HAS_DYNQOS),
		.base = APEX1_IB15_BASE,
	},
	{
		.name = "apex0",
		.flags = (FLAGS_HAS_REGUL | FLAGS_HAS_DYNQOS),
		.base = APEX0_IB16_BASE,
	},
	{
		.name = "h264dec1",
		.flags = (FLAGS_HAS_REGUL | FLAGS_HAS_DYNQOS),
		.base = H264_IB25_BASE,
	},
	{
		.name = "enet",
		.flags = (FLAGS_HAS_REGUL | FLAGS_HAS_DYNQOS),
		.base = ENET_IB12_BASE,
	},
	{
		.name = "axbs",
		.flags = (FLAGS_HAS_REGUL | FLAGS_HAS_DYNQOS),
		.base = AXBS_IB36_BASE,
	},
}; 

/* END -- CONFIG_MACH_NXP_S32 */

#elif CONFIG_MACH_ZYNQMP_ZCU102 == 1

#include <asm/smc.h>

#define NIC_BASE             (0x40010000UL)
#define NIC_SIZE             (1024*1024) /* 1MB Aperture */

#define ZCU102_QOS_READ_SMC  0x8400ff04
#define ZCU102_QOS_WRITE_SMC 0x8400ff05

#define QOS_DEVICES          18
#define IB_FPDCCI_FPDMAIN    (0x42100)

/* Add remaining QoS base addresses here */

static const struct qos_device devices [QOS_DEVICES] = {
	{
		.name = "fpdmain",
		.flags = (FLAGS_HAS_REGUL),
		.base = IB_FPDCCI_FPDMAIN,
	},

	/* Add remaining QoS devices here. */
}; 

/* In the ZCU102, QoS registers require secure access. We must perform
 * an smc to a patched ATF to interact with them. Define read/write
 * functions accordingly */

#define qos_read32(addr)			\
    smc_arg1(ZCU102_QOS_READ_SMC, (unsigned long)addr)

#define qos_write32(addr, val)			\
    smc_arg2(ZCU102_QOS_WRITE_SMC, (unsigned long)addr, val)

/* Since we are going to use SMC calls to access any of the QoS
 * registers, do no perform a real mapping but only provide a pointer
 * that reflects the linear 1:1 mapping done in the ATF. */
#define qos_map_device(base, size)		\
    ((void *)NIC_BASE)

/* END -- CONFIG_MACH_ZYNQMP_ZCU102 */

#else

#pragma message("No QoS support implemented for this platform")

#define NIC_BASE           0
#define NIC_SIZE           0
#define QOS_DEVICES        0

static const struct qos_device devices [QOS_DEVICES];

#endif


/* If the a platform-specific way to interact with QoS registers is
 * specified, use standard mmio functions */
#ifndef qos_read32
#define qos_read32(addr)			\
    mmio_read32(addr)
#endif

#ifndef qos_write32
#define qos_write32(addr, val)			\
    mmio_write32(addr, val)
#endif

/* Unless the QoS registers are not directly accessible from EL2, use
 * the stardard mapping to access the I/O registers */
#ifndef qos_map_device
#define qos_map_device(base, size)		\
    qos_map_device(base, size);
#endif

