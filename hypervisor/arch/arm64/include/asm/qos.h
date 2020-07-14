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

#ifndef _JAILHOUSE_ASM_QOS_H
#define _JAILHOUSE_ASM_QOS_H

#include <jailhouse/qos-common.h>

/* Main entry point for QoS management call */
int qos_call(unsigned long count, unsigned long settings_ptr);

#endif /* _JAILHOUSE_ASM_QOS_H  */
