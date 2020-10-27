/*
 * Copyright (C) 2020 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <cycles.h>

uint64_t us_to_cycles(uint32_t us)
{
	return (((uint64_t)us * (uint64_t)get_cpu_freq()) / 1000UL);
}

uint64_t cycles_to_us(uint64_t ticks)
{
	uint64_t us = 0UL, cpu_freq_khz = (uint64_t)get_cpu_freq();

	if (cpu_freq_khz != 0U ) {
		us = (ticks * 1000UL) / cpu_freq_khz;
	}

	return us;
}

uint64_t cycles_to_ms(uint64_t ticks)
{
	return ticks / (uint64_t)get_cpu_freq();
}
