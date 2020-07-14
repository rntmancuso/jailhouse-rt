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

#include <jailhouse/types.h>

struct uart_chip {
	/* must be set by the caller */
	void *virt_base;
	struct jailhouse_console *debug_console;

	/* driver selects defaults, if used */
	void (*reg_out)(struct uart_chip *chip, unsigned int reg, u32 value);
	u32 (*reg_in)(struct uart_chip *chip, unsigned int reg);

	/* set by the driver */
	void (*init)(struct uart_chip *chip);
	bool (*is_busy)(struct uart_chip *chip);
	void (*write_char)(struct uart_chip *chip, char c);

	/* Additional driver functions if mode switch requried between
	 * linux and jailhouse */
	void (*hyp_mode_enter)(struct uart_chip *chip);
	void (*hyp_mode_leave)(struct uart_chip *chip);
};

void uart_write(const char *msg);

extern struct uart_chip *uart;
extern struct uart_chip uart_8250_ops;
