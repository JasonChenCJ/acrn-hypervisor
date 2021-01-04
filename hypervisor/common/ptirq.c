/*
 * Copyright (C) 2018-2020 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <x86/per_cpu.h>
#include <x86/guest/vm.h>
#include <x86/ptirq.h>
#include <interrupt/sub/softirq/include/softirq.h>
#include <util.h>
#include <interrupt/include/irq.h>
#include <logmsg.h>

#define PTIRQ_BITMAP_ARRAY_SIZE	INT_DIV_ROUNDUP(CONFIG_MAX_PT_IRQ_ENTRIES, 64U)
#define PTIRQ_BITMAP_SIZE	(PTIRQ_BITMAP_ARRAY_SIZE << 6U)

static struct ptirq ptirq_entries[CONFIG_MAX_PT_IRQ_ENTRIES];
static uint64_t ptirq_entry_bitmaps[PTIRQ_BITMAP_ARRAY_SIZE];

static inline uint16_t alloc_ptirq_id(void)
{
	uint16_t id = (uint16_t)ffz64_ex(ptirq_entry_bitmaps, PTIRQ_BITMAP_SIZE);

	if (id < ARRAY_SIZE(ptirq_entries)) {
		bitmap_set_nolock(id & 0x3FU, &ptirq_entry_bitmaps[id >> 6U]);
	} else {
		id = INVALID_PTDEV_ENTRY_ID;
	}

	return id;
}

static inline void free_ptirq_id(uint16_t id)
{
	if (id < ARRAY_SIZE(ptirq_entries)) {
		bitmap_clear_nolock(id & 0x3FU, &ptirq_entry_bitmaps[id >> 6U]);
	}
}

static void enq_softirq(struct ptirq *irq)
{
	uint64_t rflags;

	/* enqueue request in order, SOFTIRQ_PTDEV will pick up */
	CPU_INT_ALL_DISABLE(&rflags);

	/* avoid adding recursively */
	list_del(&irq->softirq_node);
	/* TODO: assert if irq already in list */
	list_add_tail(&irq->softirq_node, &get_cpu_var(softirq_dev_entry_list));
	CPU_INT_ALL_RESTORE(rflags);
	fire_softirq(SOFTIRQ_PTDEV);
}

static void ptirq_intr_delay_callback(void *data)
{
	struct ptirq *irq = (struct ptirq *)data;

	enq_softirq(irq);
}

/* interrupt context */
static void ptirq_interrupt_handler(__unused uint32_t irqn, void *data)
{
	struct ptirq *irq = (struct ptirq *)data;
	bool to_enqueue = true;

	/*
	 * "interrupt storm" detection & delay intr injection just for UOS
	 * pass-thru devices, collect its data and delay injection if needed
	 */
	if (!is_sos_vm(irq->vm)) {
		irq->intr_count++;

		/* if delta > 0, set the delay TSC, dequeue to handle */
		if (irq->vm->intr_inject_delay_delta > 0UL) {

			/* if the timer started (irq is in timer-list), not need enqueue again */
			if (timer_is_started(&irq->intr_delay_timer)) {
				to_enqueue = false;
			} else {
				irq->intr_delay_timer.timeout =
					get_cpu_cycles() + irq->vm->intr_inject_delay_delta;
			}
		} else {
			irq->intr_delay_timer.timeout = 0UL;
		}
	}

	if (to_enqueue) {
		enq_softirq(irq);
	}
}

static struct ptirq *deq_softirq(uint16_t pcpu_id)
{
	uint64_t rflags;
	struct ptirq *irq = NULL;

	CPU_INT_ALL_DISABLE(&rflags);

	while (!list_empty(&get_cpu_var(softirq_dev_entry_list))) {
		irq = get_first_item(&per_cpu(softirq_dev_entry_list, pcpu_id),
				struct ptirq, softirq_node);

		list_del_init(&irq->softirq_node);

		/* if sos vm, just dequeue, if uos, check delay timer */
		if (is_sos_vm(irq->vm) ||
			timer_expired(&irq->intr_delay_timer, get_cpu_cycles(), NULL)) {
			break;
		} else {
			/* add it into timer list; dequeue next one */
			(void)add_timer(&irq->intr_delay_timer);
			irq = NULL;
		}
	}

	CPU_INT_ALL_RESTORE(rflags);
	return irq;
}

static void ptirq_softirq(uint16_t pcpu_id)
{
	struct ptirq *irq;

	for (irq = deq_softirq(pcpu_id); irq != NULL; irq = deq_softirq(pcpu_id)) {
		/* only service active irqs */
		if (irq->active) {
			ptirq_softirq_arch(irq);
		}
	}
}

void ptirq_set_vmsi(struct ptirq *irq, struct msi_info *vmsi)
{
	if (irq->active && (irq->intr_type == PTDEV_INTR_MSI)) {
		irq->vmsi = *vmsi;
	}
}

void ptirq_set_polarity(struct ptirq *irq, uint32_t polarity)
{
	if (irq->active) {
		irq->polarity = polarity;
	}
}

uint32_t ptirq_get_polarity(struct ptirq *irq)
{
	return irq->active ? irq->polarity : 0U;
}

uint32_t ptirq_get_irq(struct ptirq *irq)
{
	return irq->active ? irq->allocated_pirq : IRQ_INVALID;
}

/* activate intr with irq registering */
int32_t ptirq_request(struct ptirq **irq, struct acrn_vm *vm, uint32_t intr_type,
		union source_id *phys_sid, union source_id *virt_sid)
{
	int32_t retval = -EINVAL;
	uint16_t id = alloc_ptirq_id();
	uint32_t phys_irq;
	struct ptirq *entry;

	if (id < ARRAY_SIZE(ptirq_entries)) {
		entry = &ptirq_entries[id];
		(void)memset((void *)entry, 0U, sizeof(*entry));
		entry->id = id;
		entry->vm = vm;
		entry->intr_type = intr_type;
		entry->virt_sid = *virt_sid;

		phys_irq = ptirq_get_irq_arch(intr_type, phys_sid);

		INIT_LIST_HEAD(&entry->softirq_node);
		initialize_timer(&entry->intr_delay_timer, ptirq_intr_delay_callback,
				entry, 0UL, 0UL);

		/* register and allocate host irq */
		retval = request_irq(phys_irq, ptirq_interrupt_handler,
				(void *)entry, IRQF_PT);

		if (retval < 0) {
			free_ptirq_id(id);
			pr_err("request irq failed, please check!, phys-irq=%u", phys_irq);
		} else {
			entry->allocated_pirq = (uint32_t)retval;
			entry->active = true;
			*irq = entry;
		}
	} else {
		pr_err("ptirq alloc failed");
	}

	return retval;
}

void ptirq_free(struct ptirq *irq)
{
	uint64_t rflags;

	if ((irq != NULL) && irq->active) {
		irq->active = false;
		free_irq(irq->allocated_pirq);

		CPU_INT_ALL_DISABLE(&rflags);
		list_del(&irq->softirq_node);
		del_timer(&irq->intr_delay_timer);
		CPU_INT_ALL_RESTORE(rflags);
		free_ptirq_id(irq->id);
	}
}

int32_t ptirq_get_intr_data(struct ptirq *irq, uint64_t *buffer,
		uint32_t *pos, uint32_t buffer_cnt)
{
	int32_t written = 0U;

	if (irq->active) {
		if ((*pos + 2U) > buffer_cnt) {
			written = -1;
		} else {
			buffer[*pos] = irq->allocated_pirq;
			buffer[*pos + 1U] = irq->intr_count;
			*pos += 2U;
			written += 2U;
		}
	}

	return written;
}

void ptirq_init(void)
{
	if (get_pcpu_id() == BSP_CPU_ID) {
		register_softirq(SOFTIRQ_PTDEV, ptirq_softirq);
	}
	INIT_LIST_HEAD(&get_cpu_var(softirq_dev_entry_list));
}
