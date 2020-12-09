/*
 * Copyright (C) 2020 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PTIRQ_H
#define PTIRQ_H

#include <timer.h>
#include <ptintr.h>

struct ptirq {
	uint16_t id;
	struct acrn_vm *vm;
	uint32_t intr_type;
	union source_id virt_sid;
	bool active; /* true=active, false=inactive */
	uint32_t allocated_pirq;
	struct list_head softirq_node;
	uint32_t polarity; /* 0=active high, 1=active low */
	struct msi_info vmsi;
	uint64_t intr_count;
	struct hv_timer intr_delay_timer; /* used for delayed intr injection */
};

void ptirq_set_vmsi(struct ptirq *irq, struct msi_info *vmsi);
void ptirq_set_polarity(struct ptirq *irq, uint32_t polarity);
uint32_t ptirq_get_polarity(struct ptirq *irq);
uint32_t ptirq_get_irq(struct ptirq *irq);
int32_t ptirq_request(struct ptirq **irq, struct acrn_vm *vm, uint32_t intr_type,
		union source_id *phys_sid, union source_id *virt_sid);
void ptirq_free(struct ptirq *irq);
int32_t ptirq_get_intr_data(struct ptirq *irq, uint64_t *buffer,
		uint32_t *pos, uint32_t buffer_cnt);
void ptirq_init(void);
#endif /* PTIRQ_H */
