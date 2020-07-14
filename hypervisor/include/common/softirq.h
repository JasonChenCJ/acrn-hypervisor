/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SOFTIRQ_H
#define SOFTIRQ_H

#define SOFTIRQ_TIMER		0U
#define SOFTIRQ_PTDEV		1U
#define NR_SOFTIRQS		2U

typedef void (*softirq_handler)(uint16_t cpu_id);

#ifdef CONFIG_SOFTIRQ
void init_softirq(void);
void register_softirq(uint16_t nr, softirq_handler handler);
void fire_softirq(uint16_t nr);
void do_softirq(void);
#else
#define init_softirq() do{} while(0)
#define register_softirq(x, y) do{ (void)&y;} while(0)
#define fire_softirq(x) do { } while(0)
#define do_softirq() do { } while(0)
#endif

#endif /* SOFTIRQ_H */
