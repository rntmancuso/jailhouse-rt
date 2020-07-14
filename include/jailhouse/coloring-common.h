/*
 * Jailhouse Cache coloring for ARM64
 *
 * Copyright (C) 2018 Università di Modena e Reggio Emilia
 *
 * Authors:
 *   Luca Miccio <lucmiccio@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */
#ifndef _JAILHOUSE_COLORING_COMMON_H
#define _JAILHOUSE_COLORING_COMMON_H
#include <jailhouse/cell-config.h>
/**
 * Find all the ranges (i,j) s.t (i<=k<=j and mask[k]=1) in mask
*/
static void ranges_in_mask(bool* mask, int size, int* values)
{
	int i = 0, j = 0, k = 0, current_value = 0;

  /* Setup all values to -1 */
	for(i = 0;i < size*2; i++)
	  values[i] = -1;
	for (k = 0; k < size; k++) {
	  if(mask[k]){
		i = k;
		j = i;
		while(mask[++j] && j < size);
		values[current_value] = i;
		values[++current_value] = j-1;
		current_value++;
		k = j;
	  }
	}
}

#endif /* _JAILHOUSE_COLORING_COMMON_H */
