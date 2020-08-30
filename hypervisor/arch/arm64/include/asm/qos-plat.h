/*
 * ARM QoS Support for Jailhouse. Board-specific definitions.
 *
 * Copyright (c) Boston University, 2020
 *
 * Authors:
 *  Renato Mancuso <rmancuso@bu.edu>
 *  Rohan Tabish <rtabish@illinois.edu>
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

/* 
 * Only support for FPD_GPV QoS regulators is currently available for
 * the ZCU102. 
 * 
 * The layout of the following regulators has been extracted from:
 * https://www.xilinx.com/html_docs/registers/ug1087/ug1087-zynq-ultrascale-registers.html
 *
 * NOTE: On the ZCU102, the FPD_GPV registers are accessible only from
 * EL3. In order for JH to be able to access these registers, the ATF
 * should be patched to allow read/write operations through the two
 * services defined below (ZCU102_QOS_READ_SMC and
 * ZCU102_QOS_WRITE_SMC)
 */
#define NIC_BASE                (0xfd700000)
#define NIC_SIZE                (1024*1024) /* 1MB Aperture */


/* There are three QoS address ranges in the ZCU 102 */

#define LPD_OFFSET 		0xA00000	//LPD_OFFSET (0xFE100000) -	(0xFD700000) = 0xA00000


#define ZCU102_QOS_READ_SMC     0x8400ff04
#define ZCU102_QOS_WRITE_SMC    0x8400ff05

#define QOS_DEVICES     	35 /*There are 18 Regulatore with CNTL */
/* Peripherials in LPD with QoS Support */
#define M_RPU0_BASE		LPD_OFFSET + (0x42100)
#define M_RPU1_BASE		LPD_OFFSET + (0x43100)
#define M_ADMA_BASE 	LPD_OFFSET + (0x44100)
#define M_AFIFM6_BASE	LPD_OFFSET + (0x45100)
#define M_DAP_BASE		LPD_OFFSET + (0x47100)	
#define M_USB0_BASE		LPD_OFFSET + (0x48100)
#define M_USB1_BASE		LPD_OFFSET + (0x49100)
#define M_INTIOU_BASE	LPD_OFFSET + (0x4A100)
#define M_INTCSUPMU_BASE		LPD_OFFSET + (0x4B100)
#define M_INTLPDINBOUND_BASE	LPD_OFFSET + (0x4C100)
#define M_INTLPDOCM_BASE	LPD_OFFSET + (0x4D100)
#define M_IB5_BASE 		LPD_OFFSET + (0xC3100)
#define M_IB6_BASE		LPD_OFFSET + (0xC4100)
#define M_IB8_BASE 		LPD_OFFSET + (0xC5100)
#define M_IB0_BASE		LPD_OFFSET + (0xC6100)
#define M_IB11_BASE 	LPD_OFFSET + (0xC7100)
#define M_IB12_BASE 	LPD_OFFSET + (0xC8100)

/* Peripherials in FPD with QoS Support */
#define M_INTFPDCCI_BASE 	(0x42100)
#define M_INTFPDSMMUTBU3_BASE	(0x43100)
#define M_INTFPDSMMUTBU4_BASE	(0x44100)
#define M_AFIFM0_BASE	(0x45100)
#define M_AFIFM1_BASE	(0x46100)
#define M_AFIFM2_BASE	(0x47100)
#define M_INITFPDSMMUTBU5_BASE	(0x48100)
#define M_DP_BASE		(0x49100)
#define M_AFIFM3_BASE		(0x4A100)
#define M_AFIFM4_BASE		(0x4B100)
#define M_AFIFM5_BASE		(0x4C100)
#define M_GPU_BASE 		(0x4D100)
#define M_PCIE_BASE		(0x4E100)
#define M_GDMA_BASE		(0x4F100)
#define M_SATA_BASE 		(0x50100)
#define M_CORESIGHT_BASE	(0x52100)
#define ISS_IB2_BASE		(0xC2100)
#define ISS_IB6_BASE		(0xC3100)

static const struct qos_device devices [QOS_DEVICES] = {
	/* LPD Start here */
	{
		.name = "rpu0",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_RPU0_BASE,
	},

	{
		.name = "rpu1",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_RPU1_BASE,
	},

	{
		.name = "adma",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_ADMA_BASE,
	},

	{
		.name = "afifm6",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_AFIFM0_BASE,
	},

	{
		.name = "dap",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_DAP_BASE,
	},

	{
		.name = "usb0",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_USB0_BASE,
	},

	{
		.name = "intiou",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_INTIOU_BASE,
	},

	{
		.name = "csupmu",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_INTCSUPMU_BASE,
	},

	{
		.name = "lpdinbound",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_INTLPDINBOUND_BASE,
	},

	{
		.name = "lpdocm",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_INTLPDOCM_BASE,
	},

	{
		.name = "ib5",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_IB5_BASE,
	},

	{
		.name = "ib6",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_IB6_BASE,
	},

	{
		.name = "ib8",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_IB8_BASE,
	},

	{
		.name = "ib0",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_IB0_BASE,
	},

	{
		.name = "ib11",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_IB11_BASE,
	},

	{
		.name = "ib12",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_IB12_BASE,
	},
	/* GPV Start here */
	{
		.name = "fpdcci",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_INTFPDCCI_BASE,
	},

	{
		.name = "smmutbu3",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_INTFPDSMMUTBU3_BASE,
	},

	{
		.name = "smmutbu4",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_INTFPDSMMUTBU3_BASE,
	},

	{
		.name = "afifm0",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_AFIFM0_BASE,
	},

	{
		.name = "afifm1",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_AFIFM1_BASE,
	},

	{
		.name = "afifm2",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_AFIFM2_BASE,
	},

	{
		.name = "smmutbu5",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_INITFPDSMMUTBU5_BASE,
	},

	{
		.name = "dp",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_DP_BASE,
	},

	{
		.name = "afifm3",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_AFIFM3_BASE,
	},

	{
		.name = "afifm4",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_AFIFM4_BASE,
	},

	{
		.name = "afifm5",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_AFIFM5_BASE,
	},

	{
		.name = "gpu",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_GPU_BASE,
	},

	{
		.name = "pcie",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_PCIE_BASE,
	},

	{
		.name = "gdma",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_GDMA_BASE,
	},

	{
		.name = "sata",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_SATA_BASE,
	},

	{
		.name = "coresight",
		.flags = (FLAGS_HAS_REGUL),
		.base = M_CORESIGHT_BASE,
	},

	{
		.name = "issib2",
		.flags = (FLAGS_HAS_REGUL),
		.base = ISS_IB2_BASE,
	},

	{
		.name = "issib6",
		.flags = (FLAGS_HAS_REGUL),
		.base = ISS_IB6_BASE,
	},
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

