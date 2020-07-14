/*
 * Jailhouse, a Linux-based partitioning hypervisor
 *
 * Copyright (c) OTH Regensburg, 2017
 *
 * Authors:
 *  Ralf Ramsauer <ralf.ramsauer@oth-regensburg.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

extern struct uart_chip uart_pl011_ops, uart_xuartps_ops, uart_mvebu_ops,
	uart_hscif_ops, uart_scifa_ops, uart_imx_ops, uart_s32_ops;
