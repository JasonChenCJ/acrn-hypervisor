/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <cycles.h>

void udelay(uint32_t us)
{
	uint64_t dest_cycles, delta_cycles;

	/* Calculate number of ticks to wait */
	delta_cycles = us_to_cycles(us);
	dest_cycles = get_cpu_cycles() + delta_cycles;

	/* Loop until time expired */
	while (get_cpu_cycles() < dest_cycles) {
	}
}
