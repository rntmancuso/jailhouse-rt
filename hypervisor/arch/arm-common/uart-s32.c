/*
 * Jailhouse, a Linux-based partitioning hypervisor
 * 
 * Copyright 2017 NXP
 *
 * Description: UART driver for NXP S32 LinflexD/UART console
 * 
 * Authors:
 *  Renato Mancuso (BU) <rmancuso@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <jailhouse/mmio.h>
#include <jailhouse/uart.h>

/* All registers are 32-bit width */

#define LINCR1	0x0000	/* LIN control register				*/
#define LINIER	0x0004	/* LIN interrupt enable register		*/
#define LINSR	0x0008	/* LIN status register				*/
#define LINESR	0x000C	/* LIN error status register			*/
#define UARTCR	0x0010	/* UART mode control register			*/
#define UARTSR	0x0014	/* UART mode status register			*/
#define LINTCSR	0x0018	/* LIN timeout control status register		*/
#define LINOCR	0x001C	/* LIN output compare register			*/
#define LINTOCR	0x0020	/* LIN timeout control register			*/
#define LINFBRR	0x0024	/* LIN fractional baud rate register		*/
#define LINIBRR	0x0028	/* LIN integer baud rate register		*/
#define LINCFR	0x002C	/* LIN checksum field register			*/
#define LINCR2	0x0030	/* LIN control register 2			*/
#define BIDR	0x0034	/* Buffer identifier register			*/
#define BDRL	0x0038	/* Buffer data register least significant	*/
#define BDRM	0x003C	/* Buffer data register most significant	*/
#define IFER	0x0040	/* Identifier filter enable register		*/
#define IFMI	0x0044	/* Identifier filter match index		*/
#define IFMR	0x0048	/* Identifier filter mode register		*/
#define GCR	0x004C	/* Global control register			*/
#define UARTPTO	0x0050	/* UART preset timeout register			*/
#define UARTCTO	0x0054	/* UART current timeout register		*/
/* The offsets for DMARXE/DMATXE in master mode only			*/
#define DMATXE	0x0058	/* DMA Tx enable register			*/
#define DMARXE	0x005C	/* DMA Rx enable register			*/

#define LINFLEXD_LINCR1_INIT		(1 << 0)

#define LINFLEXD_UARTCR_RXEN		(1 << 5)
#define LINFLEXD_UARTCR_TXEN		(1 << 4)
#define LINFLEXD_UARTCR_PC0		(1 << 3)

#define LINFLEXD_UARTCR_RFBM		(1 << 9)
#define LINFLEXD_UARTCR_TFBM		(1 << 8)
#define LINFLEXD_UARTCR_WL1		(1 << 7)
#define LINFLEXD_UARTCR_PC1		(1 << 6)

#define LINFLEXD_UARTSR_DRFRFE		(1 << 2)
#define LINFLEXD_UARTSR_DTFTFF		(1 << 1)

static void uart_init(struct uart_chip *chip)
{
    	unsigned long cr;

	return;
	
	/* Enable UART mode */
	cr = mmio_read32(chip->virt_base + UARTCR);
	cr |= (LINFLEXD_UARTCR_TXEN);
	mmio_write32(chip->virt_base + UARTCR, cr);

}

static bool uart_is_busy(struct uart_chip *chip)
{

	unsigned long cr;
	bool busy;
	
	cr = mmio_read32(chip->virt_base + UARTCR);
    
    	if (!(cr & LINFLEXD_UARTCR_TFBM))
		busy = ((mmio_read32(chip->virt_base + UARTSR) & LINFLEXD_UARTSR_DTFTFF)
			!= LINFLEXD_UARTSR_DTFTFF);
	else
		busy = (mmio_read32(chip->virt_base + UARTSR) & LINFLEXD_UARTSR_DTFTFF);

	if (!busy && !(cr & LINFLEXD_UARTCR_TFBM)) {
		mmio_write32(chip->virt_base + UARTSR,
			     (mmio_read32(chip->virt_base + UARTSR) | LINFLEXD_UARTSR_DTFTFF));
	}

	return busy;
}

static void uart_write_char(struct uart_chip *chip, char c)
{
    	mmio_write32(chip->virt_base + BDRL, c);
}

static void uart_hyp_enter(struct uart_chip *chip)
{
    	unsigned long lincr, uartcr;

	/* Put the device in init mode */
	lincr = mmio_read32(chip->virt_base + LINCR1);	
	lincr |= (LINFLEXD_LINCR1_INIT);
	mmio_write32(chip->virt_base + LINCR1, lincr);

	while(!(mmio_read32(chip->virt_base + LINCR1) & LINFLEXD_LINCR1_INIT));

	/* Switch to FIFO mode */
	uartcr = mmio_read32(chip->virt_base + UARTCR);
	uartcr &= ~(LINFLEXD_UARTCR_RFBM | LINFLEXD_UARTCR_TFBM);
	mmio_write32(chip->virt_base + UARTCR, uartcr);

	/* Bring back the device from init mode */
	lincr &= ~(LINFLEXD_LINCR1_INIT);
	mmio_write32(chip->virt_base + LINCR1, lincr);

	while(mmio_read32(chip->virt_base + LINCR1) & LINFLEXD_LINCR1_INIT);

}

static void uart_hyp_leave(struct uart_chip *chip)
{
    	unsigned long lincr, uartcr;

	/* Put the device in init mode */
	lincr = mmio_read32(chip->virt_base + LINCR1);	
	lincr |= (LINFLEXD_LINCR1_INIT);
	mmio_write32(chip->virt_base + LINCR1, lincr);

	while(!(mmio_read32(chip->virt_base + LINCR1) & LINFLEXD_LINCR1_INIT));

	/* Switch to beffer/DMA mode */
	uartcr = mmio_read32(chip->virt_base + UARTCR);
	uartcr |= LINFLEXD_UARTCR_RFBM | LINFLEXD_UARTCR_TFBM;
	mmio_write32(chip->virt_base + UARTCR, uartcr);

	/* Bring back the device from init mode */
	lincr &= ~(LINFLEXD_LINCR1_INIT);
	mmio_write32(chip->virt_base + LINCR1, lincr);

	while(mmio_read32(chip->virt_base + LINCR1) & LINFLEXD_LINCR1_INIT);

}

struct uart_chip uart_s32_ops = {
	.init = uart_init,
	.is_busy = uart_is_busy,
	.write_char = uart_write_char,
	.hyp_mode_enter = uart_hyp_enter,
	.hyp_mode_leave = uart_hyp_leave,
};

