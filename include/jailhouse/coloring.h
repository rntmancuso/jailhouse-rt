/*
 * Jailhouse, a Linux-based partitioning hypervisor
 *
 * Copyright (c) UniversitÃ  di Modena e Reggio Emilia, 2019
 *
 * Authors:
 *  Luca Miccio <lucmiccio@gmail.com>
 *  Marco Solieri <ms@xt3.it>
 *  Renato Mancuso <rntmancuso@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef _JAILHOUSE_COLORING_H
#define _JAILHOUSE_COLORING_H

/*
 * The HV_PAGE_* macros must be kept coherent with
 * hypervisor/arch/arm64/include/asm/paging.h
 */
#ifndef HV_PAGE_SHIFT
#define HV_PAGE_SHIFT 12
#endif

#ifndef HV_PAGE_SIZE
#define HV_PAGE_SIZE (1 << HV_PAGE_SHIFT)
#endif

#ifndef HV_PAGE_MASK
#define HV_PAGE_MASK ~(HV_PAGE_SIZE - 1)
#endif

/**
 * Return the binary logarithm of the @param n
 */
static unsigned int log_two(unsigned int n)
{
	unsigned int i = 1;
	unsigned int j = 1;

	while (i < n) {
		j++;
		i = 1 << j;
	}

	return j;
}

/**
 * Return the coloring mask based on the value of @param llc_way_size.
 * This mask represents the bits in the address that can be used
 * for defining available colors.
 *
 * @param llc_way_size		Last level cache way size.
 * @return unsigned long	The coloring bitmask.
 */
static unsigned long calculate_addr_col_mask(unsigned int llc_way_size)
{
	unsigned long addr_col_mask = 0;
	unsigned int low_idx, high_idx, i;

	low_idx = log_two(HV_PAGE_SIZE);
	high_idx = log_two(llc_way_size) - 1;

	for (i = low_idx; i <= high_idx; i++)
		addr_col_mask |= (1 << i);

	return addr_col_mask;
}

/**
 * Return physical page address that conforms to the colors selection
 * given in col_val
 *
 * @param phys		Physical address start
 * @param addr_col_mask	Mask specifying the bits available for coloring
 * @param col_val	Mask asserting the color bits to be used.
 * Mask length is the number of available colors. Must not be 0.
 *
 * @return The lowest physical page address being greater or equal than
 * 'phys' and belonging to 'col_val'
 */
static unsigned long next_colored(unsigned long phys,
		unsigned long addr_col_mask, unsigned long long col_val)
{
	unsigned long retval = phys;
	unsigned int i, k;
	unsigned int cur_col_mask_conf, cur_col_bit;
	unsigned int low_idx, high_idx;
	unsigned long reset_mask;
	unsigned long long col_reset_mask, max_col_val;

	if (!col_val)
		return phys;

	high_idx = log_two(addr_col_mask);
	low_idx = HV_PAGE_SHIFT;
	cur_col_mask_conf = 0;

	/*
	 * Sanitize col_val
	 * The only control we need here is to handle the scenario where
	 * col_val is greater than the maximum possible value based on the
	 * addr_col_mask.
	 * Although this check is performed in the driver module, we cannot
	 * assume that this is always done.
	 * In the above scenario we "sanitize" the value and basically we
	 * will use only the bits useful for coloring.
	 */
	max_col_val = (1 << ((addr_col_mask >> HV_PAGE_SHIFT)+1)) - 1;

	if (col_val > max_col_val) {
		col_reset_mask = (((1 << high_idx) - 1));
		col_val &= col_reset_mask;
	}

	// Get the current color value
	for (k = 0, i = low_idx; i < high_idx; i++, k++) {
		if (retval & (1UL << i))
			cur_col_mask_conf |= (1UL << k);
	}

	cur_col_bit = 1 << cur_col_mask_conf;

	/*
	 * Loop over all possible colors starting from `cur_col_mask_conf`
	 * until the color value in `cur_col_bit` belongs to the user selection
	 * in `col_val`.
	 */
	while (!(cur_col_bit & col_val)) {
		// If we go out of col_val bounds, restart from 0
		// and carry 1 outside addr_col_mask
		if (cur_col_bit > col_val) {
			cur_col_mask_conf = 0;
			cur_col_bit = 1 << cur_col_mask_conf;
			retval += 1UL << high_idx;
		} else {
			cur_col_bit = cur_col_bit << 1;
			cur_col_mask_conf++;
		}
	}

	reset_mask = ~(((1 << high_idx) - 1));
	retval &= reset_mask;
	retval |= (cur_col_mask_conf << HV_PAGE_SHIFT);

	return retval;
}

#endif /* !_JAILHOUSE_COLORING_H */
