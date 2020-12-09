/*
 * Copyright (C) 2020 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ARCH_PTIRQ_H
#define ARCH_PTIRQ_H

#include <ptirq.h>

void ptirq_softirq_arch(struct ptirq *irq);
uint32_t ptirq_get_irq_arch(uint32_t intr_type, union source_id *phys_sid);
void ptirq_intx_ack_arch(struct ptirq *irq);
#endif /* ARCH_PTIRQ_H */
