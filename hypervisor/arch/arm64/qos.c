/*
 * ARM QoS Support for Jailhouse
 *
 * Copyright (c) Boston University, 2020
 *
 * Authors:
 *  Renato Mancuso <rmancuso@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#include <asm/sysregs.h>
#include <jailhouse/printk.h>
#include <jailhouse/mmio.h>
#include <jailhouse/entry.h>
#include <jailhouse/paging.h>
#include <jailhouse/qos-common.h>
#include <asm/paging.h>

#include <asm/qos.h>

#define qos_print(fmt, ...)			\
	printk("[QoS] " fmt, ##__VA_ARGS__)

struct qos_device {
	char name [QOS_DEV_NAMELEN];
	__u8 flags;
	__u32 base;
};

struct qos_param {
	char name [QOS_PARAM_NAMELEN];
	__u16 reg;
	__u8 enable;
	__u8 shift;
	__u32 mask;
};

/* Apply all the setting provided in the array of settings passed as a
 * paramter */
static int qos_apply_settings(struct qos_setting * settings, int count);

/* Clear the QOS_CNTL register for all the devices */
static int qos_disable_all(void);

/* This function sets a given parameter to the desired value. It does
 * not enable the corresponding interface */
static int qos_set_param (const struct qos_device * dev, const struct qos_param * param,
		   unsigned long value);

/* Once we are done setting all the parameters, enable all the affected interfaces */
static void qos_set_enable(const struct qos_device * dev, __u32 value);

/* This function returns 1 if the selected device supports setting the
 * considered parameter */
static int qos_dev_is_capable(const struct qos_device * dev, const struct qos_param * param);


/* Board-independent QoS support */
#define FLAGS_HAS_RWQOS    (1 << 0)
#define FLAGS_HAS_REGUL    (1 << 1)
#define FLAGS_HAS_DYNQOS   (1 << 2)

/* Offsets of control registers from beginning of device-specific
 * config space */

/* The typical QoS interface has the following layout:
 * 
 * BASE: 0x??80
 * read_qos    = BASE
 * write_qos   = + 0x04
 * fn_mod      = + 0x08
----- REGULATION ------
 * qos_cntl    = + 0x0C
 * max_ot      = + 0x10
 * max_comb_ot = + 0x14
 * aw_p        = + 0x18
 * aw_b        = + 0x1C
 * aw_r        = + 0x20
 * ar_p        = + 0x24
 * ar_b        = + 0x28
 * ar_r        = + 0x2C
----- DYNAMIC QOS -----
 * tgt_latency = + 0x30
 * ki          = + 0x34
 * qos_range   = + 0x38
 */

#define READ_QOS           0x00
#define WRITE_QOS          0x04
#define FN_MOD             0x08
#define QOS_CNTL           0x0C
#define MAX_OT             0x10
#define MAX_COMB_OT        0x14
#define AW_P               0x18
#define AW_B               0x1C
#define AW_R               0x20
#define AR_P               0x24
#define AR_B               0x28
#define AR_R               0x2C
#define TGT_LATENCY        0x30
#define KI                 0x34
#define QOS_RANGE          0x38

/* QOS_CNTL REgister  */
#define EN_AWAR_OT_SHIFT    (7)
#define EN_AR_OT_SHIFT      (6)
#define EN_AW_OT_SHIFT      (5)
#define EN_AR_LATENCY_SHIFT (4)
#define EN_AW_LATENCY_SHIFT (3)
#define EN_AWAR_RATE_SHIFT  (2)
#define EN_AR_RATE_SHIFT    (1)
#define EN_AW_RATE_SHIFT    (0)
#define EN_NO_ENABLE        (31)

/* Number of settable QoS parameters */
#define QOS_PARAMS          22

/* Bit fields and masks in control registers  */
#define READ_QOS_SHIFT      (0)
#define READ_QOS_MASK       (0x0f)
#define WRITE_QOS_SHIFT     (0)
#define WRITE_QOS_MASK      (0x0f)

#define AW_MAX_OTF_SHIFT    (0)
#define AW_MAX_OTI_SHIFT    (8)
#define AR_MAX_OTF_SHIFT    (16)
#define AR_MAX_OTI_SHIFT    (24)
#define AW_MAX_OTF_MASK     (0xff)
#define AW_MAX_OTI_MASK     (0x3f)
#define AR_MAX_OTF_MASK     (0xff)
#define AR_MAX_OTI_MASK     (0x3f)

#define AWAR_MAX_OTF_SHIFT  (0)
#define AWAR_MAX_OTI_SHIFT  (8)
#define AWAR_MAX_OTF_MASK   (0xff)
#define AWAR_MAX_OTI_MASK   (0x7f)

#define AW_P_SHIFT          (24)
#define AW_B_SHIFT          (0)
#define AW_R_SHIFT          (20)
#define AW_P_MASK           (0xff)
#define AW_B_MASK           (0xffff)
#define AW_R_MASK           (0xfff)
	
#define AR_P_SHIFT          (24)
#define AR_B_SHIFT          (0)
#define AR_R_SHIFT          (20)
#define AR_P_MASK           (0xff)
#define AR_B_MASK           (0xffff)
#define AR_R_MASK           (0xfff)

#define AR_TGT_LAT_SHIFT    (16)
#define AW_TGT_LAT_SHIFT    (0)
#define AR_TGT_LAT_MASK     (0xfff)
#define AW_TGT_LAT_MASK     (0xfff)

#define AR_KI_SHIFT         (8)
#define AW_KI_SHIFT         (0)
#define AR_KI_MASK          (0x7)
#define AW_KI_MASK          (0x7)

#define AR_MAX_QOS_SHIFT    (24)
#define AR_MIN_QOS_SHIFT    (16)
#define AW_MAX_QOS_SHIFT    (8)
#define AW_MIN_QOS_SHIFT    (0)
#define AR_MAX_QOS_MASK     (0xf)
#define AR_MIN_QOS_MASK     (0xf)
#define AW_MAX_QOS_MASK     (0xf)
#define AW_MIN_QOS_MASK     (0xf)

/* Return the address of the configuration register to be used to set
 * the desired paramter for the selected device */
#define QOS_PAR(dev, param)						\
	((void *)(nic_base + (__u64)dev->base + (__u64)param->reg))

#define QOS_REG(dev, reg)					\
	((void *)(nic_base + (__u64)dev->base + (__u64)reg))

/* Mapped NIC device */
static void * nic_base = NULL;

#include <asm/qos-plat.h>

static const struct qos_param params [QOS_PARAMS] = {
	{
		.name = "read_qos",
		.reg = READ_QOS,
		.enable = EN_NO_ENABLE,
		.shift = READ_QOS_SHIFT,
		.mask = READ_QOS_MASK,
	},
	{
		.name = "write_qos",
		.reg = WRITE_QOS,
		.enable = EN_NO_ENABLE,
		.shift = WRITE_QOS_SHIFT,
		.mask = WRITE_QOS_MASK,
	},
	{
		.name = "aw_max_otf",
		.reg = MAX_OT,
		.enable = EN_AW_OT_SHIFT,
		.shift = AW_MAX_OTF_SHIFT,
		.mask = AW_MAX_OTF_MASK,
	},
	{
		.name = "aw_max_oti",
		.reg = MAX_OT,
		.enable = EN_AW_OT_SHIFT,
		.shift = AW_MAX_OTI_SHIFT,
		.mask = AW_MAX_OTI_MASK,
	},
	{
		.name = "ar_max_otf",
		.reg = MAX_OT,
		.enable = EN_AR_OT_SHIFT,
		.shift = AR_MAX_OTF_SHIFT,
		.mask = AR_MAX_OTF_MASK,
	},
	{
		.name = "ar_max_oti",
		.reg = MAX_OT,
		.enable = EN_AR_OT_SHIFT,
		.shift = AR_MAX_OTI_SHIFT,
		.mask = AR_MAX_OTI_MASK,
	},	
	{
		.name = "awar_max_otf",
		.reg = MAX_COMB_OT,
		.enable = EN_AWAR_OT_SHIFT,
		.shift = AWAR_MAX_OTF_SHIFT,
		.mask = AWAR_MAX_OTF_MASK,
	},
	{
		.name = "awar_max_oti",
		.reg = MAX_COMB_OT,
		.enable = EN_AWAR_OT_SHIFT,
		.shift = AWAR_MAX_OTI_SHIFT,
		.mask = AWAR_MAX_OTI_MASK,
	},
	{
		.name = "aw_p",
		.reg = AW_P,
		.enable = EN_AW_RATE_SHIFT,
		.shift = AW_P_SHIFT,
		.mask = AW_P_MASK,
	},
	{
		.name = "aw_b",
		.reg = AW_B,
		.enable = EN_AW_RATE_SHIFT,
		.shift = AW_B_SHIFT,
		.mask = AW_B_MASK,
	},
	{
		.name = "aw_r",
		.reg = AW_R,
		.enable = EN_AW_RATE_SHIFT,
		.shift = AW_R_SHIFT,
		.mask = AW_R_MASK,
	},
	{
		.name = "ar_p",
		.reg = AR_P,
		.enable = EN_AR_RATE_SHIFT,
		.shift = AR_P_SHIFT,
		.mask = AR_P_MASK,
	},
	{
		.name = "ar_b",
		.reg = AR_B,
		.enable = EN_AR_RATE_SHIFT,
		.shift = AR_B_SHIFT,
		.mask = AR_B_MASK,
	},
	{
		.name = "ar_r",
		.reg = AR_R,
		.enable = EN_AR_RATE_SHIFT,
		.shift = AR_R_SHIFT,
		.mask = AR_R_MASK,
	},
	{
		.name = "ar_tgt_latency",
		.reg = TGT_LATENCY,
		.enable = EN_AR_LATENCY_SHIFT,
		.shift = AR_TGT_LAT_SHIFT,
		.mask = AR_TGT_LAT_MASK,
	},
	{
		.name = "aw_tgt_latency",
		.reg = TGT_LATENCY,
		.enable = EN_AW_LATENCY_SHIFT,
		.shift = AW_TGT_LAT_SHIFT,
		.mask = AW_TGT_LAT_MASK,
	},
	{
		.name = "ar_ki",
		.reg = KI,
		.enable = EN_AR_LATENCY_SHIFT,
		.shift = AR_KI_SHIFT,
		.mask = AR_KI_MASK,
	},
	{
		.name = "aw_ki",
		.reg = KI,
		.enable = EN_AW_LATENCY_SHIFT,
		.shift = AW_KI_SHIFT,
		.mask = AW_KI_MASK,
	},
	{
		.name = "ar_max_qos",
		.reg = QOS_RANGE,
		.enable = EN_AW_LATENCY_SHIFT,
		.shift = AR_MAX_QOS_SHIFT,
		.mask = AR_MAX_QOS_MASK,
	},
	{
		.name = "ar_min_qos",
		.reg = QOS_RANGE,
		.enable = EN_AW_LATENCY_SHIFT,
		.shift = AR_MIN_QOS_SHIFT,
		.mask = AR_MIN_QOS_MASK,
	},
	{
		.name = "aw_max_qos",
		.reg = QOS_RANGE,
		.enable = EN_AW_LATENCY_SHIFT,
		.shift = AW_MAX_QOS_SHIFT,
		.mask = AW_MAX_QOS_MASK,
	},
	{
		.name = "aw_min_qos",
		.reg = QOS_RANGE,
		.enable = EN_AW_LATENCY_SHIFT,
		.shift = AW_MIN_QOS_SHIFT,
		.mask = AW_MIN_QOS_MASK,
	},
	
};

/* Find QoS-enabled device by name */
static const struct qos_device * qos_dev_find_by_name(char * name)
{
	int i;
	for (i = 0; i < QOS_DEVICES; ++i)
		if(strncmp(name, devices[i].name, QOS_DEV_NAMELEN) == 0)
			return &devices[i];

	return NULL;
}


/* Find QoS parameter by name */
static const struct qos_param * qos_param_find_by_name(char * name)
{
	int i;
	for (i = 0; i < QOS_PARAMS; ++i)
		if(strncmp(name, params[i].name, QOS_PARAM_NAMELEN) == 0)
			return &params[i];

	return NULL;
}

/* Low-level functions to configure different aspects of the QoS
 * infrastructure */

/* This function sets a given parameter to the desired value. It does
 * not enable the corresponding interface */
static int qos_set_param (const struct qos_device * dev, const struct qos_param * param,
	       unsigned long value)
{
	/* TODO check that device supports this parameter */
	int ret = 0;

	qos_print("QoS: Dev [%s], Param [%s] = 0x%08lx (reg off: +0x%08llx)\n",
	       dev->name, param->name, value, (__u64)(QOS_PAR(dev, param) - nic_base));
	
	__u32 regval = qos_read32(QOS_PAR(dev, param));
	regval &= ~(param->mask << param->shift);
	regval |= ((value & param->mask) << param->shift);
	qos_write32(QOS_PAR(dev, param), regval);
	
	return ret;
}

/* Once we are done setting all the parameters, enable all the affected interfaces */
static void qos_set_enable(const struct qos_device * dev, __u32 value)
{
	/* Mask away the no-enable bit */
	value &= ~(1 << EN_NO_ENABLE);
	
	/* Set the enable bit in the corresponding device */
	qos_write32(QOS_REG(dev, QOS_CNTL), value);
}

/* This function returns 1 if the selected device supports setting the
 * considered parameter */
static int qos_dev_is_capable(const struct qos_device * dev, const struct qos_param * param)
{
	/* TODO to be implemented */
	return 1;
}

/* Main function to apply a set of QoS paramters passed via the array
 * settings. The length of the array is specified in the second
 * parameter. */
static int qos_apply_settings(struct qos_setting * settings, int count)
{
	int ret = 0;
	int i;

	const struct qos_device * cur_dev = NULL;
	__u32 enable_val = 0;
	
	for (i = 0; i < count; ++i) {
		char * dev_name = settings[i].dev_name;

		/* We are about to change device. Set the enable
		 * register for the current device */
		if (dev_name[0] && cur_dev) {
			qos_set_enable(cur_dev, enable_val);
			enable_val = 0;
		}
				
		if (dev_name[0]) 
			cur_dev = qos_dev_find_by_name(dev_name);
		
		
		/* At this point, the cur_dev should not be NULL */
		if(!cur_dev)
			return -ENODEV;
		
		const struct qos_param * param = qos_param_find_by_name(settings[i].param_name);
		
		if(!param)
			return -EINVAL;

		/* Check that this device implements this QoS interface */
		if(!qos_dev_is_capable(cur_dev, param))
			return -ENOSYS;
		
		enable_val |= 1 << param->enable;
		qos_set_param(cur_dev, param, settings[i].value);		
	}

	/* Apply settings for the last device */
	qos_set_enable(cur_dev, enable_val);
				
	return ret;
}

/* Clear the QOS_CNTL register for all the devices */
static int qos_disable_all(void)
{
	int i;
	for (i = 0; i < QOS_DEVICES; ++i)
		qos_set_enable(&devices[i], 0);

	return 0;
}


/* Main entry point for QoS management call */
int qos_call(unsigned long count, unsigned long settings_ptr)
{
	unsigned long sett_page_offs = settings_ptr & ~PAGE_MASK;
	unsigned int sett_pages;
	void *sett_mapping;
	int ret;

	/* Check if the NIC needs to be mapped */
	if(!nic_base) {
		nic_base = qos_map_device(NIC_BASE, NIC_SIZE);
		
		if(!nic_base)
			return -ENOSYS;
	}

	/* The settings currently reside in kernel memory. Use
	 * temporary mapping to make the settings readable by the
	 * hypervisor. No need to clean up the mapping because this is
	 * only temporary by design. */
	sett_pages = PAGES(sett_page_offs + sizeof(struct qos_setting) * count);
	sett_mapping = paging_get_guest_pages(NULL, settings_ptr, sett_pages,
					     PAGE_READONLY_FLAGS);

	if (!sett_mapping) {
		ret = -ENOMEM;
		goto exit_err;
	}

	struct qos_setting * settings = (struct qos_setting *)(sett_mapping + sett_page_offs);

	/* Check if the user has requestes QoS control to be disabled */
	if(count > 0 && strncmp("disable", settings[0].dev_name, 8) == 0) {
		ret = qos_disable_all();
	} else {
		/* Otherwise, just apply the parameters */
		ret = qos_apply_settings(settings, count);
	}
	
	
exit_err:
	return ret;

}
