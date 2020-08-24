/*
 * Jailhouse AArch64 support
 *
 * Copyright (C) 2016 Huawei Technologies Duesseldorf GmbH
 * Copyright (c) 2016 Siemens AG
 *
 * Authors:
 *  Antonios Motakis <antonios.motakis@huawei.com>
 *  Jan Kiszka <jan.kiszka@siemens.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Implementation derived from Linux source files:
 *   - arch/arm64/mm/cache.S
 *   - arch/arm64/mm/proc-macros.S
 */

#include <asm/asm-defines.h>
	
/*
 * dcache_line_size - get the minimum D-cache line size from the CTR register.

.macro	dcache_line_size, reg, tmp
	mrs	\tmp, ctr_el0			// read CTR
	ubfm	\tmp, \tmp, #16, #19		// cache line size encoding
	mov	\reg, #4			// bytes per word
	lsl	\reg, \reg, \tmp		// actual cache line size
.endm
*/
	
.macro	dcache_line_size  reg, tmp
	mrs	\tmp, ctr_el0
	ubfx	\tmp, \tmp, #16, #4
	mov	\reg, #4
	lsl	\reg, \reg, \tmp
.endm
	
/*
 *	arm_dcaches_flush(addr, size, flush)
 *
 *	Ensure that the data held in the page addr is written back to the
 *	page in question.
 *
 *	- addr    - address
 *	- size    - size in question
 *	- flush   - type of flush (see enum dcache_flush)
 */
	.global arm_dcaches_flush
arm_dcaches_flush:
	dcache_line_size x3, x4
	add	x1, x0, x1
	sub	x4, x3, #1
	bic	x0, x0, x4

1:	cmp	x2, #DCACHE_CLEAN_ASM
	b.ne	2f
	dc	cvac, x0
	b	4f

2:	cmp	x2, #DCACHE_INVALIDATE_ASM
	b.ne	3f
	dc	ivac, x0
	b	4f

3:	dc	civac, x0			// DCACHE_CLEAN_AND_INVALIDATE

4:	add	x0, x0, x3
	cmp	x0, x1
	b.lo	1b

	dsb	sy
	ret

#define LEVEL_SHIFT		1
#define LOUIS_SHIFT		21
#define CLIDR_FIELD_WIDTH	3	
#define LOC_SHIFT		24


.macro func _name, _align=5
	.align \_align
	.global \_name
\_name:
.endm

.macro endfunc _name
.endm
	
/*
 * This macro can be used for implementing various data cache operations `op`
 */
.macro do_dcache_maintenance_by_mva op
	/* Exit early if size is zero */
	cbz	x1, exit_loop_\op
	dcache_line_size x2, x3
	add	x1, x0, x1
	sub	x3, x2, #1
	bic	x0, x0, x3
loop_\op:
	dc	\op, x0
	add	x0, x0, x2
	cmp	x0, x1
	b.lo	loop_\op
	dsb	sy
exit_loop_\op:
	ret
.endm
	/* ------------------------------------------
	 * Clean+Invalidate from base address till
	 * size. 'x0' = addr, 'x1' = size
	 * ------------------------------------------
	 */
func flush_dcache_range
	do_dcache_maintenance_by_mva civac
endfunc flush_dcache_range

	/* ------------------------------------------
	 * Clean from base address till size.
	 * 'x0' = addr, 'x1' = size
	 * ------------------------------------------
	 */
func clean_dcache_range
	do_dcache_maintenance_by_mva cvac
endfunc clean_dcache_range

	/* ------------------------------------------
	 * Invalidate from base address till
	 * size. 'x0' = addr, 'x1' = size
	 * ------------------------------------------
	 */
func inv_dcache_range
	do_dcache_maintenance_by_mva ivac
endfunc inv_dcache_range


	/* ---------------------------------------------------------------
	 * Data cache operations by set/way to the level specified
	 *
	 * The main function, do_dcsw_op requires:
	 * x0: The operation type (0-2), as defined in arch.h
	 * x3: The last cache level to operate on
	 * x9: clidr_el1
	 * x10: The cache level to begin operation from
	 * and will carry out the operation on each data cache from level 0
	 * to the level in x3 in sequence
	 *
	 * The dcsw_op macro sets up the x3 and x9 parameters based on
	 * clidr_el1 cache information before invoking the main function
	 * ---------------------------------------------------------------
	 */

.macro	dcsw_op shift, fw, ls
	mrs	x9, clidr_el1
	ubfx	x3, x9, \shift, \fw
	lsl	x3, x3, \ls
	mov	x10, xzr
	b	do_dcsw_op
.endm

func do_dcsw_op
	cbz	x3, exit
	adr	x14, dcsw_loop_table	// compute inner loop address
	add	x14, x14, x0, lsl #5	// inner loop is 8x32-bit instructions
	mov	x0, x9
	mov	w8, #1
loop1:
	add	x2, x10, x10, lsr #1	// work out 3x current cache level
	lsr	x1, x0, x2		// extract cache type bits from clidr
	and	x1, x1, #7		// mask the bits for current cache only
	cmp	x1, #2			// see what cache we have at this level
	b.lo	level_done		// nothing to do if no cache or icache

	msr	csselr_el1, x10		// select current cache level in csselr
	isb				// isb to sych the new cssr&csidr
	mrs	x1, ccsidr_el1		// read the new ccsidr
	and	x2, x1, #7		// extract the length of the cache lines
	add	x2, x2, #4		// add 4 (line length offset)
	ubfx	x4, x1, #3, #10		// maximum way number
	clz	w5, w4			// bit position of way size increment
	lsl	w9, w4, w5		// w9 = aligned max way number
	lsl	w16, w8, w5		// w16 = way number loop decrement
	orr	w9, w10, w9		// w9 = combine way and cache number
	ubfx	w6, w1, #13, #15	// w6 = max set number
	lsl	w17, w8, w2		// w17 = set number loop decrement
	dsb	sy			// barrier before we start this level
	br	x14			// jump to DC operation specific loop

.macro	dcsw_loop _op
loop2_\_op:
	lsl	w7, w6, w2		// w7 = aligned max set number

loop3_\_op:
	orr	w11, w9, w7		// combine cache, way and set number
	dc	\_op, x11
	subs	w7, w7, w17		// decrement set number
	b.hs	loop3_\_op

	subs	x9, x9, x16		// decrement way number
	b.hs	loop2_\_op

	b	level_done
.endm

level_done:
	add	x10, x10, #2		// increment cache number
	cmp	x3, x10
	b.hi	loop1
	msr	csselr_el1, xzr		// select cache level 0 in csselr
	dsb	sy			// barrier to complete final cache operation
	isb
exit:
	ret
endfunc do_dcsw_op

dcsw_loop_table:
	dcsw_loop isw
	dcsw_loop cisw
	dcsw_loop csw


func dcsw_op_louis
	dcsw_op #LOUIS_SHIFT, #CLIDR_FIELD_WIDTH, #LEVEL_SHIFT
endfunc dcsw_op_louis


func dcsw_op_all
	dcsw_op #LOC_SHIFT, #CLIDR_FIELD_WIDTH, #LEVEL_SHIFT
endfunc dcsw_op_all

	/* ---------------------------------------------------------------
	 *  Helper macro for data cache operations by set/way for the
	 *  level specified
	 * ---------------------------------------------------------------
	 */
.macro dcsw_op_level level
	mrs	x9, clidr_el1
	mov	x3, \level
	sub	x10, x3, #2
	b	do_dcsw_op
.endm

func dcsw_op_level0
	dcsw_op_level #(0 << LEVEL_SHIFT)
endfunc dcsw_op_level0
	
	/* ---------------------------------------------------------------
	 * Data cache operations by set/way for level 1 cache
	 *
	 * The main function, do_dcsw_op requires:
	 * x0: The operation type (0-2), as defined in arch.h
	 * ---------------------------------------------------------------
	 */
func dcsw_op_level1
	dcsw_op_level #(1 << LEVEL_SHIFT)
endfunc dcsw_op_level1

	/* ---------------------------------------------------------------
	 * Data cache operations by set/way for level 2 cache
	 *
	 * The main function, do_dcsw_op requires:
	 * x0: The operation type (0-2), as defined in arch.h
	 * ---------------------------------------------------------------
	 */
func dcsw_op_level2
	dcsw_op_level #(2 << LEVEL_SHIFT)
endfunc dcsw_op_level2

	/* ---------------------------------------------------------------
	 * Data cache operations by set/way for level 3 cache
	 *
	 * The main function, do_dcsw_op requires:
	 * x0: The operation type (0-2), as defined in arch.h
	 * ---------------------------------------------------------------
	 */
func dcsw_op_level3
	dcsw_op_level #(3 << LEVEL_SHIFT)
endfunc dcsw_op_level3

	
	.global arm_l1l2_caches_flush
arm_l1l2_caches_flush:
        mov     x18, x30
	
	mov	x0, #1 // DCCISW
	bl	dcsw_op_level1

	mov	x0, #1 // DCCISW
	bl	dcsw_op_level2

	mov     x30, x18
	ret
