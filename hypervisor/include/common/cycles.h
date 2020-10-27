/*
 * Copyright (C) 2020 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CYCLES_H
#define CYCLES_H

#include <types.h>

#define CYCLES_PER_MS	us_to_cycles(1000U)

/**
 * @brief  Get cpu frequency.
 *
 * @return frequency(KHz)
 */
uint32_t get_cpu_freq(void);

/**
 * @brief Get cpu cycles.
 *
 * @return cycles
 */
uint64_t get_cpu_cycles(void);

/**
 * @brief convert us to cycles.
 *
 * @return cycles
 */
uint64_t us_to_cycles(uint32_t us);

/**
 * @brief convert cycles to us.
 *
 * @return microsecond
 */
uint64_t cycles_to_us(uint64_t ticks);

/**
 * @brief convert cycles to ms.
 *
 * @return millisecond
 */
uint64_t cycles_to_ms(uint64_t ticks);
#endif
