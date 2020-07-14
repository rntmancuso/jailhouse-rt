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

#ifndef _JAILHOUSE_QOS_COMMON_H
#define _JAILHOUSE_QOS_COMMON_H

#define QOS_DEV_NAMELEN    15
#define QOS_PARAM_NAMELEN  16

struct qos_setting {
	char dev_name [QOS_DEV_NAMELEN];
	char param_name [QOS_PARAM_NAMELEN];
	__u32 value;
};

#endif /* _JAILHOUSE_QOS_COMMON_H */
