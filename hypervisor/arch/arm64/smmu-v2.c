/*
 * Jailhouse AArch64 support
 *
 * Authors:
 *  Renato Mancuso (BU) <rmancuso@bu.edu>
 *
 * This file implements support for SMMUs that follow the ARM SMMUv2
 * specification, like the ARM SMMU-500. The driver is used only if
 * appropriate structures are present in the cell configuration to
 * specify the base address and type of SMMU in use, and if a suitable
 * list of stream IDs is provided. This implementation reuses the page
 * tables of the cell to configure the SMMU on the provided list of
 * stream IDs.
 *
 * This driver is loosely based on the Linux kernel SMMU v2 driver
 * located at: drivers/iommu/arm-smmu.[c,h]
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
#include <asm/iommu.h>
#include <jailhouse/cell.h>
#include <jailhouse/mmio.h>
#include <asm/coloring.h>

#define smmu_print printk
#define SMMUV2_DEBUG 1

/* Offset of addr from start of the page. */
#define PAGE_OFFSET(addr)		((addr) & PAGE_OFFS_MASK)

#define LOWER_32_BITS(n)		((u32)(n))
#define UPPER_32_BITS(n)		((n) >> 32)

/* Configuration registers */
#define ARM_SMMU_GR0_sCR0		0x0
#define ARM_SMMU_sCR0_VMID16EN		BIT(31)
#define ARM_SMMU_sCR0_BSU		GENMASK(15, 14)
#define ARM_SMMU_sCR0_FB		BIT(13)
#define ARM_SMMU_sCR0_PTM		BIT(12)
#define ARM_SMMU_sCR0_VMIDPNE		BIT(11)
#define ARM_SMMU_sCR0_USFCFG		BIT(10)
#define ARM_SMMU_sCR0_GCFGFIE		BIT(5)
#define ARM_SMMU_sCR0_GCFGFRE		BIT(4)
#define ARM_SMMU_sCR0_EXIDENABLE	BIT(3)
#define ARM_SMMU_sCR0_GFIE		BIT(2)
#define ARM_SMMU_sCR0_GFRE		BIT(1)
#define ARM_SMMU_sCR0_CLIENTPD		BIT(0)

/* Auxiliary Configuration register */
#define ARM_SMMU_GR0_sACR		0x10

/* Identification registers */
#define ARM_SMMU_GR0_ID0		0x20
#define ARM_SMMU_ID0_S1TS		BIT(30)
#define ARM_SMMU_ID0_S2TS		BIT(29)
#define ARM_SMMU_ID0_NTS		BIT(28)
#define ARM_SMMU_ID0_SMS		BIT(27)
#define ARM_SMMU_ID0_ATOSNS		BIT(26)
#define ARM_SMMU_ID0_PTFS_NO_AARCH32	BIT(25)
#define ARM_SMMU_ID0_PTFS_NO_AARCH32S	BIT(24)
#define ARM_SMMU_ID0_NUMIRPT		GENMASK(23, 16)
#define ARM_SMMU_ID0_CTTW		BIT(14)
#define ARM_SMMU_ID0_NUMSIDB		GENMASK(12, 9)
#define ARM_SMMU_ID0_EXIDS		BIT(8)
#define ARM_SMMU_ID0_NUMSMRG		GENMASK(7, 0)

#define ARM_SMMU_GR0_ID1		0x24
#define ARM_SMMU_ID1_PAGESIZE		BIT(31)
#define ARM_SMMU_ID1_NUMPAGENDXB	GENMASK(30, 28)
#define ARM_SMMU_ID1_NUMS2CB		GENMASK(23, 16)
#define ARM_SMMU_ID1_NUMCB		GENMASK(7, 0)

#define ARM_SMMU_GR0_ID2		0x28
#define ARM_SMMU_ID2_VMID16		BIT(15)
#define ARM_SMMU_ID2_PTFS_64K		BIT(14)
#define ARM_SMMU_ID2_PTFS_16K		BIT(13)
#define ARM_SMMU_ID2_PTFS_4K		BIT(12)
#define ARM_SMMU_ID2_UBS		GENMASK(11, 8)
#define ARM_SMMU_ID2_OAS		GENMASK(7, 4)
#define ARM_SMMU_ID2_IAS		GENMASK(3, 0)

#define ARM_SMMU_GR0_ID3		0x2c
#define ARM_SMMU_GR0_ID4		0x30
#define ARM_SMMU_GR0_ID5		0x34
#define ARM_SMMU_GR0_ID6		0x38

#define ARM_SMMU_GR0_ID7		0x3c
#define ARM_SMMU_ID7_MAJOR		GENMASK(7, 4)
#define ARM_SMMU_ID7_MINOR		GENMASK(3, 0)

#define ARM_SMMU_GR0_sGFAR		0x40
#define ARM_SMMU_GR0_sGFSR		0x48
#define ARM_SMMU_sGFSR_USF		BIT(1)

#define ARM_SMMU_GR0_sGFSYNR0		0x50
#define ARM_SMMU_GR0_sGFSYNR1		0x54
#define ARM_SMMU_GR0_sGFSYNR2		0x58

/* Global TLB invalidation */
#define ARM_SMMU_GR0_TLBIVMID		0x64
#define ARM_SMMU_GR0_TLBIALLNSNH	0x68
#define ARM_SMMU_GR0_TLBIALLH		0x6c
#define ARM_SMMU_GR0_sTLBGSYNC		0x70

#define ARM_SMMU_GR0_sTLBGSTATUS	0x74
#define ARM_SMMU_sTLBGSTATUS_GSACTIVE	BIT(0)

#define ARM_SMMU_GR0_sGATS1UR	0x100
#define ARM_SMMU_GR0_sGATS1UW	0x108
#define ARM_SMMU_GR0_sGATS1PR	0x110
#define ARM_SMMU_GR0_sGATS1PW	0x118
#define ARM_SMMU_GR0_sGATS12UR	0x120
#define ARM_SMMU_GR0_sGATS12UW	0x128
#define ARM_SMMU_GR0_sGATS12PR	0x130
#define ARM_SMMU_GR0_sGATS12PW	0x138

#define ARM_SMMU_GR0_sGPAR 	0x180
#define ARM_SMMU_GR0_sGATSR 	0x188

/* Stream mapping registers */
#define ARM_SMMU_GR0_SMR(n)		(0x800 + ((n) << 2))
#define ARM_SMMU_SMR_VALID		BIT(31)
#define ARM_SMMU_SMR_MASK		GENMASK(31, 16)
#define ARM_SMMU_SMR_ID			GENMASK(15, 0)

#define ARM_SMMU_GR0_S2CR(n)		(0xc00 + ((n) << 2))
#define ARM_SMMU_S2CR_PRIVCFG		GENMASK(25, 24)
enum arm_smmu_s2cr_privcfg {
	S2CR_PRIVCFG_DEFAULT,
	S2CR_PRIVCFG_DIPAN,
	S2CR_PRIVCFG_UNPRIV,
	S2CR_PRIVCFG_PRIV,
};
#define ARM_SMMU_S2CR_TYPE		GENMASK(17, 16)
enum arm_smmu_s2cr_type {
	S2CR_TYPE_TRANS,
	S2CR_TYPE_BYPASS,
	S2CR_TYPE_FAULT,
};
#define ARM_SMMU_S2CR_EXIDVALID		BIT(10)
#define ARM_SMMU_S2CR_CBNDX		GENMASK(7, 0)

/* Context bank attribute registers */
#define ARM_SMMU_GR1_CBAR(n)		(0x0 + ((n) << 2))
#define ARM_SMMU_CBAR_IRPTNDX		GENMASK(31, 24)
#define ARM_SMMU_CBAR_TYPE		GENMASK(17, 16)
enum arm_smmu_cbar_type {
	CBAR_TYPE_S2_TRANS,
	CBAR_TYPE_S1_TRANS_S2_BYPASS,
	CBAR_TYPE_S1_TRANS_S2_FAULT,
	CBAR_TYPE_S1_TRANS_S2_TRANS,
};
#define ARM_SMMU_CBAR_S1_MEMATTR	GENMASK(15, 12)
#define ARM_SMMU_CBAR_S1_MEMATTR_WB	0xf
#define ARM_SMMU_CBAR_S1_BPSHCFG	GENMASK(9, 8)
#define ARM_SMMU_CBAR_S1_BPSHCFG_NSH	3
#define ARM_SMMU_CBAR_VMID		GENMASK(7, 0)

#define ARM_SMMU_GR1_CBFRSYNRA(n)	(0x400 + ((n) << 2))

#define ARM_SMMU_GR0_NSGFAR		0x440
#define ARM_SMMU_GR0_NSGFSR		0x448
#define ARM_SMMU_GR0_NSGFSYNR0		0x450

#define ARM_SMMU_GR1_CBA2R(n)		(0x800 + ((n) << 2))
#define ARM_SMMU_CBA2R_VMID16		GENMASK(31, 16)
#define ARM_SMMU_CBA2R_VA64		BIT(0)

#define ARM_SMMU_CB_SCTLR		0x0
#define ARM_SMMU_SCTLR_S1_ASIDPNE	BIT(12)
#define ARM_SMMU_SCTLR_CFCFG		BIT(7)
#define ARM_SMMU_SCTLR_CFIE		BIT(6)
#define ARM_SMMU_SCTLR_CFRE		BIT(5)
#define ARM_SMMU_SCTLR_E		BIT(4)
#define ARM_SMMU_SCTLR_AFE		BIT(2)
#define ARM_SMMU_SCTLR_TRE		BIT(1)
#define ARM_SMMU_SCTLR_M		BIT(0)

#define ARM_SMMU_CB_ACTLR		0x4

#define ARM_SMMU_CB_RESUME		0x8
#define ARM_SMMU_RESUME_TERMINATE	BIT(0)

#define ARM_SMMU_CB_TCR2		0x10
#define ARM_SMMU_TCR2_SEP		GENMASK(17, 15)
#define ARM_SMMU_TCR2_SEP_UPSTREAM	0x7
#define ARM_SMMU_TCR2_AS		BIT(4)
#define ARM_SMMU_TCR2_PASIZE		GENMASK(3, 0)

#define ARM_SMMU_CB_TTBR0		0x20
#define ARM_SMMU_CB_TTBR1		0x28
#define ARM_SMMU_TTBRn_ASID		GENMASK_ULL(63, 48)

#define ARM_SMMU_CB_TCR			0x30
#define ARM_SMMU_TCR_EAE		BIT(31)
#define ARM_SMMU_TCR_EPD1		BIT(23)
#define ARM_SMMU_TCR_TG0		GENMASK(15, 14)
#define ARM_SMMU_TCR_SH0		GENMASK(13, 12)
#define ARM_SMMU_TCR_ORGN0		GENMASK(11, 10)
#define ARM_SMMU_TCR_IRGN0		GENMASK(9, 8)
#define ARM_SMMU_TCR_T0SZ		GENMASK(5, 0)

#define ARM_SMMU_VTCR_RES1		BIT(31)
#define ARM_SMMU_VTCR_PS		GENMASK(18, 16)
#define ARM_SMMU_VTCR_TG0		ARM_SMMU_TCR_TG0
#define ARM_SMMU_VTCR_SH0		ARM_SMMU_TCR_SH0
#define ARM_SMMU_VTCR_ORGN0		ARM_SMMU_TCR_ORGN0
#define ARM_SMMU_VTCR_IRGN0		ARM_SMMU_TCR_IRGN0
#define ARM_SMMU_VTCR_SL0		GENMASK(7, 6)
#define ARM_SMMU_VTCR_T0SZ		ARM_SMMU_TCR_T0SZ

#define ARM_SMMU_CB_CONTEXTIDR		0x34
#define ARM_SMMU_CB_S1_MAIR0		0x38
#define ARM_SMMU_CB_S1_MAIR1		0x3c

#define ARM_SMMU_CB_PAR			0x50
#define ARM_SMMU_CB_PAR_F		BIT(0)

#define ARM_SMMU_CB_FSR			0x58
#define ARM_SMMU_FSR_MULTI		BIT(31)
#define ARM_SMMU_FSR_SS			BIT(30)
#define ARM_SMMU_FSR_UUT		BIT(8)
#define ARM_SMMU_FSR_ASF		BIT(7)
#define ARM_SMMU_FSR_TLBLKF		BIT(6)
#define ARM_SMMU_FSR_TLBMCF		BIT(5)
#define ARM_SMMU_FSR_EF			BIT(4)
#define ARM_SMMU_FSR_PF			BIT(3)
#define ARM_SMMU_FSR_AFF		BIT(2)
#define ARM_SMMU_FSR_TF			BIT(1)

#define ARM_SMMU_FSR_IGN		(ARM_SMMU_FSR_AFF |		\
					 ARM_SMMU_FSR_ASF |		\
					 ARM_SMMU_FSR_TLBMCF |		\
					 ARM_SMMU_FSR_TLBLKF)

#define ARM_SMMU_FSR_FAULT		(ARM_SMMU_FSR_MULTI |		\
					 ARM_SMMU_FSR_SS |		\
					 ARM_SMMU_FSR_UUT |		\
					 ARM_SMMU_FSR_EF |		\
					 ARM_SMMU_FSR_PF |		\
					 ARM_SMMU_FSR_TF |		\
					 ARM_SMMU_FSR_IGN)

#define ARM_SMMU_CB_FAR			0x60

#define ARM_SMMU_CB_FSYNR0		0x68
#define ARM_SMMU_FSYNR0_WNR		BIT(4)

#define ARM_SMMU_CB_S1_TLBIVA		0x600
#define ARM_SMMU_CB_S1_TLBIASID		0x610
#define ARM_SMMU_CB_S1_TLBIVAL		0x620
#define ARM_SMMU_CB_S2_TLBIIPAS2	0x630
#define ARM_SMMU_CB_S2_TLBIIPAS2L	0x638
#define ARM_SMMU_CB_TLBSYNC		0x7f0
#define ARM_SMMU_CB_TLBSTATUS		0x7f4
#define ARM_SMMU_CB_ATS1PR		0x800

#define ARM_SMMU_CB_ATSR		0x8f0
#define ARM_SMMU_ATSR_ACTIVE		BIT(0)


/* Maximum number of context banks per SMMU */
#define ARM_SMMU_MAX_CBS		128
#define ARM_SMMU_MAX_SMES		128

#define ARM_SMMU_FEAT_COHERENT_WALK	(1 << 0)
#define ARM_SMMU_FEAT_STREAM_MATCH	(1 << 1)
#define ARM_SMMU_FEAT_TRANS_S1		(1 << 2)
#define ARM_SMMU_FEAT_TRANS_S2		(1 << 3)
#define ARM_SMMU_FEAT_TRANS_NESTED	(1 << 4)
#define ARM_SMMU_FEAT_TRANS_OPS		(1 << 5)
#define ARM_SMMU_FEAT_VMID16		(1 << 6)
#define ARM_SMMU_FEAT_FMT_AARCH64_4K	(1 << 7)
#define ARM_SMMU_FEAT_FMT_AARCH64_16K	(1 << 8)
#define ARM_SMMU_FEAT_FMT_AARCH64_64K	(1 << 9)
#define ARM_SMMU_FEAT_FMT_AARCH32_L	(1 << 10)
#define ARM_SMMU_FEAT_FMT_AARCH32_S	(1 << 11)
#define ARM_SMMU_FEAT_EXIDS		(1 << 12)

#define WRITE_DUMMY_VAL -1

/* An SMMUv2 instance */
static struct arm_smmu_device {
	void				* base;
	unsigned int			numpage;
	u32				features;
	unsigned int			sid_mask;
	unsigned int			pgshift;
    	u32				num_context_banks;
	u32				num_s2_context_banks;
	u32                             num_mapping_groups;
	int                             cell_to_sm[ARM_SMMU_MAX_SMES];
	int                             cell_to_cb[ARM_SMMU_MAX_CBS];
} smmu[JAILHOUSE_MAX_IOMMU_UNITS];

static inline void * arm_smmu_page(struct arm_smmu_device *smmu, int n)
{
	return smmu->base + (n << smmu->pgshift);
}

static inline u32 arm_smmu_readl(struct arm_smmu_device *smmu, int page, int offset)
{
	return mmio_read32(arm_smmu_page(smmu, page) + offset);
}

static inline void arm_smmu_writel(struct arm_smmu_device *smmu, int page,
				   int offset, u32 val)
{
	mmio_write32(arm_smmu_page(smmu, page) + offset, val);
}

static inline u64 arm_smmu_readq(struct arm_smmu_device *smmu, int page, int offset)
{
	return mmio_read64(arm_smmu_page(smmu, page) + offset);
}

static inline void arm_smmu_writeq(struct arm_smmu_device *smmu, int page,
				   int offset, u64 val)
{
	mmio_write64(arm_smmu_page(smmu, page) + offset, val);
}

#define ARM_SMMU_GR0		0
#define ARM_SMMU_GR1		1
#define ARM_SMMU_CB(s, n)	((s)->numpage + (n))
//#define ARM_SMMU_CB(s, n)   ((((s)->numpage + (n)) << (s)->pgshift))

#define arm_smmu_gr0_read(s, o)		\
	arm_smmu_readl((s), ARM_SMMU_GR0, (o))
#define arm_smmu_gr0_write(s, o, v)	\
	arm_smmu_writel((s), ARM_SMMU_GR0, (o), (v))

#define arm_smmu_gr0_readq(s, o)		\
	arm_smmu_readq((s), ARM_SMMU_GR0, (o))
#define arm_smmu_gr0_writeq(s, o, v)	\
	arm_smmu_writeq((s), ARM_SMMU_GR0, (o), (v))

#define arm_smmu_gr1_read(s, o)		\
	arm_smmu_readl((s), ARM_SMMU_GR1, (o))
#define arm_smmu_gr1_write(s, o, v)	\
	arm_smmu_writel((s), ARM_SMMU_GR1, (o), (v))

#define arm_smmu_cb_read(s, n, o)	\
	arm_smmu_readl((s), ARM_SMMU_CB((s), (n)), (o))
#define arm_smmu_cb_write(s, n, o, v)	\
	arm_smmu_writel((s), ARM_SMMU_CB((s), (n)), (o), (v))
#define arm_smmu_cb_readq(s, n, o)	\
	arm_smmu_readq((s), ARM_SMMU_CB((s), (n)), (o))
#define arm_smmu_cb_writeq(s, n, o, v)	\
	arm_smmu_writeq((s), ARM_SMMU_CB((s), (n)), (o), (v))


/******* END *******/

#define ARM_MMU500_ACTLR_CPRE		(1 << 1)
#define ARM_MMU500_ACR_CACHE_LOCK	(1 << 26)
#define ARM_MMU500_ACR_S2CRB_TLBEN	(1 << 10)
#define ARM_MMU500_ACR_SMTNMB_TLBEN	(1 << 8)

static int arm_mmu500_reset(struct arm_smmu_device *smmu)
{	
	u32 reg, major;
	int i;
	/*
	 * On MMU-500 r2p0 onwards we need to clear ACR.CACHE_LOCK before
	 * writes to the context bank ACTLRs will stick. And we just hope that
	 * Secure has also cleared SACR.CACHE_LOCK for this to take effect...
	 */
	reg = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_ID7);
	major = FIELD_GET(ARM_SMMU_ID7_MAJOR, reg);
	reg = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_sACR);
	if (major >= 2)
		reg &= ~ARM_MMU500_ACR_CACHE_LOCK;
	/*
	 * Allow unmatched Stream IDs to allocate bypass
	 * TLB entries for reduced latency.
	 */
	reg |= ARM_MMU500_ACR_SMTNMB_TLBEN | ARM_MMU500_ACR_S2CRB_TLBEN;
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_sACR, reg);

	/*
	 * Disable MMU-500's not-particularly-beneficial next-page
	 * prefetcher for the sake of errata #841119 and #826419.
	 */
	for (i = 0; i < smmu->num_context_banks; ++i) {
		reg = arm_smmu_cb_read(smmu, i, ARM_SMMU_CB_ACTLR);
		reg &= ~ARM_MMU500_ACTLR_CPRE;
		arm_smmu_cb_write(smmu, i, ARM_SMMU_CB_ACTLR, reg);
	}

	return 0;
}

static void __arm_smmu_tlb_sync(struct arm_smmu_device * smmu, int page,
				int sync, int status)
{
	u32 reg;

#if SMMUV2_DEBUG == 1
	smmu_print("SMMUv2 Sync Started.\n");
#endif
	arm_smmu_writel(smmu, page, sync, WRITE_DUMMY_VAL);
	while(1) {
		reg = arm_smmu_readl(smmu, page, status);
		if (!(reg & ARM_SMMU_sTLBGSTATUS_GSACTIVE))
			break;
	}
	
}

static void arm_smmu_tlb_sync_global(struct arm_smmu_device *smmu)
{
	__arm_smmu_tlb_sync(smmu, ARM_SMMU_GR0, ARM_SMMU_GR0_sTLBGSYNC,
			    ARM_SMMU_GR0_sTLBGSTATUS);
}

/*
 * static void arm_smmu_tlb_sync_context(struct arm_smmu_device *smmu, int cbndx)
 * {
 * 	__arm_smmu_tlb_sync(smmu, ARM_SMMU_CB(smmu, cbndx),
 * 			    ARM_SMMU_CB_TLBSYNC, ARM_SMMU_CB_TLBSTATUS);
 * }
 */

/* Reset a single stream matching entry, which includes SMR and S2CR
 * registers */
static int arm_smmu_reset_sme(struct arm_smmu_device *smmu, int i)
{
	/* Work on S2CR first */
	u32 reg = FIELD_PREP(ARM_SMMU_S2CR_TYPE, S2CR_TYPE_BYPASS) |
		FIELD_PREP(ARM_SMMU_S2CR_CBNDX, 0) |
		FIELD_PREP(ARM_SMMU_S2CR_PRIVCFG, 0);
	
	if (smmu->features & ARM_SMMU_FEAT_EXIDS)
		reg &= ~ARM_SMMU_S2CR_EXIDVALID;
	
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_S2CR(i), reg);

	/* Reset SMR next */
	reg = FIELD_PREP(ARM_SMMU_SMR_ID, 0) |
		FIELD_PREP(ARM_SMMU_SMR_MASK, 0);

	if (!(smmu->features & ARM_SMMU_FEAT_EXIDS))
		reg &= ~ARM_SMMU_SMR_VALID;
	
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_SMR(i), reg);

	return 0;
}

#if SMMUV2_DEBUG == 1
static void arm_smmu_print_fault_status(struct arm_smmu_device *smmu)
{
	u32 reg;
	int i;
	
	smmu_print("######## FAULT DUMP #########\n");
	reg = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_sGFSR);
	smmu_print("\t(sGFSR): 0x%08x\n", reg);
	reg = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_sGFAR);
	smmu_print("\t(sGFAR): 0x%08x\n", reg);
	reg = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_sGFSYNR0);
	smmu_print("\t(sGFSYNR0): 0x%08x\n", reg);

	reg = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_NSGFSR);
	smmu_print("\t(NSGFSR): 0x%08x\n", reg);
	reg = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_NSGFAR);
	smmu_print("\t(NSGFAR): 0x%08x\n", reg);
	reg = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_NSGFSYNR0);
	smmu_print("\t(NSGFSYNR0): 0x%08x\n", reg);

	for (i = 0; i < smmu->num_context_banks; ++i) {
		reg = arm_smmu_cb_read(smmu, i, ARM_SMMU_CB_FSR);
		smmu_print("\t[%d] (CB_FSR): 0x%08x; ", i, reg);

		reg = arm_smmu_cb_read(smmu, i, ARM_SMMU_CB_FSYNR0);
		smmu_print("(CB_FSYNR0): 0x%08x\n", reg);
	}

	smmu_print("############ END ############\n");
	
}

static void arm_smmu_test_transl(struct arm_smmu_device *smmu, u64 addr, u32 cbndx)
{
	/* Initiate address translation */
	arm_smmu_gr0_writeq(smmu, ARM_SMMU_GR0_sGATS12UR, addr | cbndx);
	smmu_print("DEBUG: Attempting to translate 0x%08llx in bank %d\n", addr, cbndx);

	/* Wait for the result to become available */
	while(arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_sGATSR) & 0x01);

	smmu_print("\tResult (GPAR): 0x%08llx\n", arm_smmu_gr0_readq(smmu, ARM_SMMU_GR0_sGPAR));
	smmu_print("\t\t(sGFSR): 0x%08x\n", arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_sGFSR));

}

static void arm_smmu_dump_config(struct arm_smmu_device *smmu)
{
	int i;
	u32 reg;
	u64 vttbr;

	smmu_print("--- CONFIG DUMP ----\n");
	smmu_print("sCR0 = 0x%08x\n", arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_sCR0));
	smmu_print("SME Registers:\n");
	for (i = 0; i < smmu->num_mapping_groups; ++i) {
		reg = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_SMR(i));		
		smmu_print("%d) SMR = 0x%08x; ", i, reg);
		
		reg = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_S2CR(i));		
		smmu_print("S2CR = 0x%08x;\n", reg);
	}

	smmu_print("Conext Banks:\n");	
	for (i = 0; i < smmu->num_context_banks; ++i) {
		reg = arm_smmu_cb_read(smmu, i,  ARM_SMMU_CB_SCTLR);
		smmu_print("%d) SCTLR = 0x%08x; ", i, reg);

		reg = arm_smmu_cb_read(smmu, i,  ARM_SMMU_CB_TCR);
		smmu_print("TCR = 0x%08x; ", reg);

		reg = arm_smmu_gr1_read(smmu, ARM_SMMU_GR1_CBAR(i));
		smmu_print("CBAR = 0x%08x; ", reg);

		reg = arm_smmu_gr1_read(smmu, ARM_SMMU_GR1_CBA2R(i));
		smmu_print("CBA2R = 0x%08x;\n", reg);

		vttbr = arm_smmu_cb_readq(smmu, i,  ARM_SMMU_CB_TTBR0);
		smmu_print("TTBR0 = 0x%08llx;\n", vttbr);		
	}

	arm_smmu_test_transl(smmu, 0x0000000050098200, 0);
	
	smmu_print("------- END --------\n");
}
#endif

static int arm_smmu_map_memory_region(struct cell *cell,
			       const struct jailhouse_memory *mem)
{
	u64 phys_start = mem->phys_start;
	unsigned long access_flags = PTE_FLAG_VALID | PTE_ACCESS_FLAG;
	unsigned long paging_flags = PAGING_COHERENT | PAGING_HUGE;
	int err = 0;

	if (mem->flags & JAILHOUSE_MEM_READ)
		access_flags |= S2_PTE_ACCESS_RO;
	if (mem->flags & JAILHOUSE_MEM_WRITE)
		access_flags |= S2_PTE_ACCESS_WO;
	if (mem->flags & JAILHOUSE_MEM_IO)
		access_flags |= S2_PTE_FLAG_DEVICE;
	else
		access_flags |= S2_PTE_FLAG_NC;
	
	if (mem->flags & JAILHOUSE_MEM_COMM_REGION)
		phys_start = paging_hvirt2phys(&cell->comm_page);

	err = paging_create(&cell->arch.iomm, phys_start, mem->size,
			    mem->virt_start, access_flags, paging_flags);

	return err;
}

static int arm_smmu_device_reset(struct arm_smmu_device *smmu)
{
	int i;
	u32 reg;

	/* If bypass is disabled, we better make sure that the cell
	 * configuration provides an initial configuration that allows
	 * the root-cell to use basic I/O peripherals necessary to
	 * maintain system stability. */
	int disable_bypass = 1;

#if SMMUV2_DEBUG == 1
	arm_smmu_dump_config(smmu);
#endif
	
	/* Clear the global fault status register */
	reg = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_sGFSR);
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_sGFSR, reg);

	/*
	 * Reset stream mapping groups: Initial values mark all SMRn as
	 * invalid and all S2CRn as bypass unless overridden.
	 */
	for (i = 0; i < smmu->num_mapping_groups; ++i)
		arm_smmu_reset_sme(smmu, i);

	/* Make sure all context banks are disabled and clear CB_FSR  */
	for (i = 0; i < smmu->num_context_banks; ++i) {
		/* Disable bank by resetting SCTLR */
		arm_smmu_cb_write(smmu, i, ARM_SMMU_CB_SCTLR, 0);
		arm_smmu_cb_write(smmu, i, ARM_SMMU_CB_FSR, ARM_SMMU_FSR_FAULT);
	}

	/* last thing to do, reset the cell-to-sm table */
	for (i = 0; i < ARM_SMMU_MAX_SMES; ++i) {
		/* Placeholder value to never match any cell */
		smmu->cell_to_sm[i] = -1;
	}
	for (i = 0; i < ARM_SMMU_MAX_CBS; ++i) {
		/* Placeholder value to never match any cell */
		smmu->cell_to_cb[i] = -1;
	}
	
	/* Invalidate the TLB, just in case */
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_TLBIALLH, WRITE_DUMMY_VAL);
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_TLBIALLNSNH, WRITE_DUMMY_VAL);

	/* Set up fault handling */
	reg = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_sCR0);

	/* Enable fault reporting */
	reg |= (ARM_SMMU_sCR0_GFRE | ARM_SMMU_sCR0_GFIE |
		ARM_SMMU_sCR0_GCFGFRE | ARM_SMMU_sCR0_GCFGFIE);

	/* Disable TLB broadcasting. */
	reg |= (ARM_SMMU_sCR0_VMIDPNE | ARM_SMMU_sCR0_PTM);

	/* Enable client access, handling unmatched streams as appropriate */
	reg &= ~ARM_SMMU_sCR0_CLIENTPD;
	if (disable_bypass)
		reg |= ARM_SMMU_sCR0_USFCFG;
	else
		reg &= ~ARM_SMMU_sCR0_USFCFG;

	/* Disable forced broadcasting */
	reg &= ~ARM_SMMU_sCR0_FB;

	/* Don't upgrade barriers */
	reg &= ~(ARM_SMMU_sCR0_BSU);

	if (smmu->features & ARM_SMMU_FEAT_VMID16)
		reg |= ARM_SMMU_sCR0_VMID16EN;

	if (smmu->features & ARM_SMMU_FEAT_EXIDS)
		reg |= ARM_SMMU_sCR0_EXIDENABLE;

	/* SMMU-500 specific reset precedure */
	arm_mmu500_reset(smmu);

	/* Push the button */
	arm_smmu_tlb_sync_global(smmu);
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_sCR0, reg);
	
	return 0;
}

static int arm_smmu_device_init_features(struct arm_smmu_device *smmu)
{
	u32 id;
	unsigned int size;

	/* ID7 */
	id = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_ID7);

	smmu_print("\nSMMUv2 (r%lup%lu) Support -- Features\n",
		   FIELD_GET(ARM_SMMU_ID7_MAJOR, id),
		   FIELD_GET(ARM_SMMU_ID7_MINOR, id)
		);
	
	/* ID0 */
	id = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_ID0);

	smmu->features = 0;

	if (id & ARM_SMMU_ID0_S1TS) {
		smmu->features |= ARM_SMMU_FEAT_TRANS_S1;
		smmu_print("\tstage 1 translation\n");
	}

	if (id & ARM_SMMU_ID0_CTTW) {
		smmu_print("\tcoherent page table walk supported!\n");
	} else {
		smmu_print("\tcoherent page table walk NOT supported.\n");
				
	}

	
	if (id & ARM_SMMU_ID0_S2TS) {
		smmu->features |= ARM_SMMU_FEAT_TRANS_S2;
		smmu_print("\tstage 2 translation\n");
	}

	if (id & ARM_SMMU_ID0_NTS) {
		smmu->features |= ARM_SMMU_FEAT_TRANS_NESTED;
		smmu_print("\tnested translation\n");
	}

	if (!(smmu->features &
		(ARM_SMMU_FEAT_TRANS_S1 | ARM_SMMU_FEAT_TRANS_S2))) {
		smmu_print("\tno translation support!\n");
	}

	/* Max. number of entries we have for stream matching/indexing */
	if (id & ARM_SMMU_ID0_EXIDS) {
		smmu->features |= ARM_SMMU_FEAT_EXIDS;
		size = 1 << 16;
		smmu_print("\textended StreamIDs supported\n");
	} else {
		size = 1 << FIELD_GET(ARM_SMMU_ID0_NUMSIDB, id);
		smmu_print("\textended StreamIDs NOT supported, %lu available\n",
			   FIELD_GET(ARM_SMMU_ID0_NUMSIDB, id));
	}

	/* Because size is a power of 2, the following is correct */
	smmu->sid_mask = size - 1;
	if (id & ARM_SMMU_ID0_SMS) {
		smmu->features |= ARM_SMMU_FEAT_STREAM_MATCH;
		size = FIELD_GET(ARM_SMMU_ID0_NUMSMRG, id);
		if (size == 0) {
			printk("ERROR: stream-matching supported, but no SMRs present!\n");
			return -ENODEV;
		}
		
		/* Remember the number of mapping groups */
		smmu->num_mapping_groups = size;

		smmu_print("\tstream matching with %u register groups", size);
	} else {
		printk("ERROR: stream-matching NOT supported.\n");
		return -ENODEV;		
	}

	/* ID1 */
	id = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_ID1);
	smmu->pgshift = (id & ARM_SMMU_ID1_PAGESIZE) ? 16 : 12;
	smmu_print("\tpage shift is %u\n", smmu->pgshift);

	size = 1 << (FIELD_GET(ARM_SMMU_ID1_NUMPAGENDXB, id) + 1);
	smmu_print("\tnumpagendxb = %lu\n", FIELD_GET(ARM_SMMU_ID1_NUMPAGENDXB, id));

	/* This is required to correctly address context banks */
	smmu->numpage = size;

	smmu->num_s2_context_banks = FIELD_GET(ARM_SMMU_ID1_NUMS2CB, id);
	smmu->num_context_banks = FIELD_GET(ARM_SMMU_ID1_NUMCB, id);
	if (smmu->num_s2_context_banks > smmu->num_context_banks) {
		smmu_print("ERROR: impossible number of S2 context banks!\n");
		return -ENODEV;
	}
	smmu_print("\t%u context banks (%u stage-2 only)\n",
		   smmu->num_context_banks, smmu->num_s2_context_banks);

	/* ID2 */
	id = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_ID2);

	
	if (id & ARM_SMMU_ID2_VMID16) {
		smmu->features |= ARM_SMMU_FEAT_VMID16;
		smmu_print("\t16-bit VMIDs supported!\n");
	}
	
	/* Check which page table format is supported. It must be
	 * compatible with what used by JH for arm64 systems. */
	if (id & ARM_SMMU_ID2_PTFS_4K) {
		smmu->features |= ARM_SMMU_FEAT_FMT_AARCH64_4K;
		smmu_print("\taarch64 granule size 4K supported!\n");
	}
	if (id & ARM_SMMU_ID2_PTFS_16K) {
		smmu->features |= ARM_SMMU_FEAT_FMT_AARCH64_16K;
		smmu_print("\taarch64 granule size 16K supported!\n");
	}
	if (id & ARM_SMMU_ID2_PTFS_64K) {
		smmu->features |= ARM_SMMU_FEAT_FMT_AARCH64_64K;
		smmu_print("\taarch64 granule size 64K supported!\n");
	}
	
	return 0;
}

static int arm_smmu_init_cb(struct arm_smmu_device *smmu, u32 cbndx, struct cell *cell)
{
	struct paging_structures *pg_structs = &cell->arch.iomm;
	u64 vttbr;
	u32 reg;
	u32 vmid = cell->config->id;	

	/* Get root page table */
	vttbr = paging_hvirt2phys(pg_structs->root_table);

	/* Setup CBA2R */
	/* Enable aarch64 desacriptor format */
	reg = ARM_SMMU_CBA2R_VA64;
	if (smmu->features & ARM_SMMU_FEAT_VMID16)
		reg |= FIELD_PREP(ARM_SMMU_CBA2R_VMID16, vmid);
	arm_smmu_gr1_write(smmu, ARM_SMMU_GR1_CBA2R(cbndx), reg);

	/* Setup CBAR */
	reg = FIELD_PREP(ARM_SMMU_CBAR_TYPE, CBAR_TYPE_S2_TRANS);
	if (!(smmu->features & ARM_SMMU_FEAT_VMID16)) {
		/* 8-bit VMIDs live in CBAR */
		reg |= FIELD_PREP(ARM_SMMU_CBAR_VMID, vmid);
	}
	arm_smmu_gr1_write(smmu, ARM_SMMU_GR1_CBAR(cbndx), reg);
	
	/* Write CB at specified index */
	arm_smmu_cb_write(smmu, cbndx, ARM_SMMU_CB_TCR,
			  FIELD_PREP(ARM_SMMU_VTCR_TG0, 0) | /* 4kb granule size */
			  FIELD_PREP(ARM_SMMU_VTCR_T0SZ, 16) | /* smallest allowed value TTBR0 */
			  FIELD_PREP(ARM_SMMU_VTCR_SL0, 2) | /* start lookup from L0 */
			  FIELD_PREP(ARM_SMMU_VTCR_PS, 2)  /* 40-bits physical address */
		);

	/* Write translation table base address --- should already be
	 * correctly aligned. Because T0SZ = 16, bits 47:12 are used */
	arm_smmu_cb_writeq(smmu, cbndx, ARM_SMMU_CB_TTBR0, vttbr);

	/* Enable translation */
	reg = ARM_SMMU_SCTLR_CFIE | ARM_SMMU_SCTLR_CFRE | ARM_SMMU_SCTLR_AFE |
		ARM_SMMU_SCTLR_TRE | ARM_SMMU_SCTLR_M;
	arm_smmu_cb_write(smmu, cbndx, ARM_SMMU_CB_SCTLR, reg);

	return 0;
}

static int arm_smmu_write_sme(struct arm_smmu_device *smmu,
			      u32 vmid, u16 smidx, u16 cbndx,
			      u16 match_id, u16 ignore_bits,
			      enum arm_smmu_s2cr_type type)
{
	u32 reg;
	
	smmu_print("\t[Cell %d] SM = %d, setting ID = 0x%x, MASK = 0x%x\n",
		   vmid, smidx, match_id, ignore_bits);
	
	/* Setup S2CR */
	reg = FIELD_PREP(ARM_SMMU_S2CR_TYPE, type) |
		//reg = FIELD_PREP(ARM_SMMU_S2CR_TYPE, S2CR_TYPE_BYPASS) |
		FIELD_PREP(ARM_SMMU_S2CR_CBNDX, cbndx) |
		FIELD_PREP(ARM_SMMU_S2CR_PRIVCFG, S2CR_PRIVCFG_DEFAULT);
	
	if (smmu->features & ARM_SMMU_FEAT_EXIDS)
		reg |= ARM_SMMU_S2CR_EXIDVALID;
	
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_S2CR(smidx), reg);

	/* Setup SMR */
	reg = FIELD_PREP(ARM_SMMU_SMR_ID, match_id) |
		FIELD_PREP(ARM_SMMU_SMR_MASK, ignore_bits);
	
	if (!(smmu->features & ARM_SMMU_FEAT_EXIDS))
		reg |= ARM_SMMU_SMR_VALID;
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_SMR(smidx), reg);
		
	return 0;
}

/* the advatnage of doing this with a static configuration provided in
 * the cell config file is that we have global view of all the sids to
 * be assigned to this cell. We can then find the most efficient way
 * to represent this group of IDs with a mask/id pair. */
int arm_smmu_setup_stream_matching_compat(struct arm_smmu_device *smmu, u32 smidx,
					  struct cell *cell);

int arm_smmu_setup_stream_matching_compat(struct arm_smmu_device *smmu, u32 smidx,
					  struct cell *cell)
{
	/* Track all the bits that are always zero/one across all the
	 * sids */
	u16 sid;
	u16 all_ones = 0xFFFF, all_zeros = 0xFFFF;

	/* Bits set to 1 will be ignored in matching */	
	u32 ignore_bits = 0;
	u32 match_id = 0;
	
	u32 vmid = cell->config->id;	
	int i;
	
	/* If this is specified, the list of sids is actually a list
	 * of pairs. The first is an ID, the second is a mask. */
	int id_mask_pairs = 1;
	
	for_each_stream_id(sid, cell->config, i) {
		if (id_mask_pairs && ((i & 0x01) == 1)) {
			/* All bits set to 0 in the mask will be
			 * ignored for SMMU matching */
			ignore_bits |= ~sid;
			continue;
		}

		all_ones &= sid;
		all_zeros &= ~sid;
	}

	/* Match all the bits always set to 1 */
	match_id |= all_ones;

	/* Find the bits that are not always 1s or 0s, and ignore them */
	ignore_bits |= ((~all_ones) & (~all_zeros)) & ((1 << 15) - 1);

	arm_smmu_write_sme(smmu, vmid, smidx, smidx, match_id, ignore_bits, S2CR_TYPE_TRANS);

	return 0;
}

static int arm_smmu_setup_stream_matching(struct arm_smmu_device *smmu, u32 cbndx,
					  struct cell *cell)
{
	/* Track all the bits that are always zero/one across all the
	 * sids */
	u16 sid, smidx = 0;

	/* Bits set to 1 will be ignored in matching */	
	u32 ignore_bits = 0;
	u32 match_id = 0;
	
	u32 vmid = cell->config->id;	
	int i;
	
	/* If this is specified, the list of sids is actually a list
	 * of pairs. The first is an ID, the second is a mask. */
	int id_mask_pairs = 1;
	
	for_each_stream_id(sid, cell->config, i) {
		if (id_mask_pairs && ((i & 0x01) == 1)) {
			/* All bits set to 0 in the mask will be
			 * ignored for SMMU matching */
			ignore_bits = ~sid & ((1 << 15) - 1);

			/* Remember that this SME has been allocated
			 * to the cell */
			while(smmu->cell_to_sm[smidx] != -1)
				smidx++;

			if(smidx >= smmu->num_mapping_groups) {
				printk("ERROR: not enough mapping groups.\n");
				return -EINVAL;
			}
			
			if (match_id == 0)
				arm_smmu_write_sme(smmu, vmid, smidx++,
						   cbndx, match_id, ignore_bits,
						   S2CR_TYPE_BYPASS);
						
			else 
				arm_smmu_write_sme(smmu, vmid, smidx++,
						   cbndx, match_id, ignore_bits,
						   S2CR_TYPE_TRANS);

				
			continue;
		}

		match_id = sid;
	}

	return 0;
}


static int arm_smmuv2_cell_init(struct cell *cell)
{
	struct jailhouse_iommu *iommu;
	int ret, i, n, cbndx;
	const struct jailhouse_memory *mem;

	if (!iommu_count_units())
		return 0;

	iommu = &system_config->platform_info.iommu_units[0];
	for (i = 0; i < iommu_count_units(); iommu++, i++) {
		if (iommu->type != JAILHOUSE_IOMMU_SMMUV2)
			continue;

		/* Allocate root_page for smmu mappings */
		struct paging_structures * io_pg_structs = &cell->arch.iomm;
		io_pg_structs->hv_paging = false;
		io_pg_structs->root_paging = hv_paging_structs.root_paging;
		io_pg_structs->root_table = page_alloc(&mem_pool, 1);

		if (!io_pg_structs->root_table)
		{
			smmu_print("ERROR: unable to allocate root SMMU table\n");
			return -EINVAL;
		}
		
		for_each_mem_region(mem, cell->config, n) {
			smmu_print("Mapping region %d\n", n);
			ret = arm_smmu_map_memory_region(cell, mem);
			if (ret) {
				smmu_print("ERROR: region mapping failed with code %d.\n", ret);
				return -EINVAL;
			}
		}

		/* There is at least one SMMUv2 in the system. Assume
		 * that this is THE main SMMU and populate coloring
		 * functions with the smmu-dependent memory mapping
		 * function. */
		if (!col_ops.smmu_map_f)
			col_ops.smmu_map_f = arm_smmu_map_memory_region;
		
		/* TODO populate unmap function too */

		/* Invoke creation of colored regions in the SMMU mapping */
		ret = coloring_cell_smmu_create(cell);
		if (ret) {
			smmu_print("ERROR: colored region mapping failed with code %d.\n", ret);
			return -EINVAL;			
		}
				
		/* Find an unused stream matching context number */
		for (cbndx = 0; cbndx < smmu->num_context_banks; ++cbndx) {
			if (smmu[i].cell_to_cb[cbndx] == -1)
				break;
		}

		/* In this implementation, use only one context bank
		 * at most of a given cell. So we are limited by the
		 * number of available context banks. */
		if (cbndx >= smmu[i].num_context_banks) {
			printk("ERROR: unable to find an available stream matching context\n");
			return -ENODEV;
		}

		/* Mark the context we found as belonging to this cell */
		smmu[i].cell_to_cb[cbndx] = cell->config->id;
			
		ret = arm_smmu_setup_stream_matching(&smmu[i], cbndx, cell);
		
		if (ret)
			return ret;

		/* Setup translation context for root cell */
		ret = arm_smmu_init_cb(&smmu[i], cbndx, cell);

		if (ret)
			return ret;

#if SMMUV2_DEBUG == 1
		arm_smmu_dump_config(&smmu[i]);
#endif
		
		/* Invalidate the TLB, just in case */
		arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_TLBIALLH, WRITE_DUMMY_VAL);
		arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_TLBIALLNSNH, WRITE_DUMMY_VAL);
		arm_smmu_tlb_sync_global(smmu);

		/* Invalidate data caches */
		smmu_print("Invalidatiing CPU caches... \n");
		arm_l1l2_caches_flush();
		smmu_print("DONE!\n");

	}

	return 0;
}

static void arm_smmuv2_cell_exit(struct cell *cell)
{
	struct jailhouse_iommu *iommu;
	int i, j;

	smmu_print("Exiting SMMUv2 on cell %d\n", cell->config->id);
	
	if (!iommu_count_units())
		return;

	for (i = 0; i < JAILHOUSE_MAX_IOMMU_UNITS; i++) {
		iommu = &system_config->platform_info.iommu_units[i];
		if (iommu->type != JAILHOUSE_IOMMU_SMMUV2)
			continue;
#if SMMUV2_DEBUG == 1
		arm_smmu_print_fault_status(&smmu[i]);
#endif

		/* Find any SME associated with the cell and disable it */
		for (j = 0; j < smmu[i].num_mapping_groups; ++j) {
			if (smmu[i].cell_to_sm[j] == cell->config->id) {
				arm_smmu_write_sme(&smmu[i], 0, j, 0, 0, 0, S2CR_TYPE_BYPASS);
				smmu[i].cell_to_sm[j] = -1;
			}
		}

		for (j = 0; j < smmu[i].num_context_banks; ++j) {
			if (smmu[i].cell_to_cb[j] == cell->config->id) {
				arm_smmu_cb_write(smmu, j, ARM_SMMU_CB_SCTLR, 0);
				arm_smmu_cb_write(smmu, j, ARM_SMMU_CB_FSR, ARM_SMMU_FSR_FAULT);
				smmu[i].cell_to_cb[j] = -1;
			}
		}

		/* TODO: invalidate TLBs */
	}
}

static void arm_smmuv2_shutdown(void)
{
	return arm_smmuv2_cell_exit(&root_cell);
}

static int arm_smmuv2_init(void)
{
	struct jailhouse_iommu *iommu;
	int ret, i;

	iommu = &system_config->platform_info.iommu_units[0];
	for (i = 0; i < iommu_count_units(); iommu++, i++) {
		if (iommu->type != JAILHOUSE_IOMMU_SMMUV2)
			continue;

		smmu[i].base = paging_map_device(iommu->base, iommu->size);

		/* ToDo: irq allocation*/

		ret = arm_smmu_device_init_features(&smmu[i]);
		if (ret)
			return ret;
		
		/* Reset the device */
		ret = arm_smmu_device_reset(&smmu[i]);
		if (ret)
			return ret;
	}

	return arm_smmuv2_cell_init(&root_cell);
}

DEFINE_UNIT_MMIO_COUNT_REGIONS_STUB(arm_smmuv2);
DEFINE_UNIT(arm_smmuv2, "ARM SMMU v2");
