/*
 * Copyright (C) 2018-2020 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <logmsg.h>
#include <x86/guest/vm.h>
#include <util.h>
#include <irq.h>
#include <x86/irq.h>
#include <x86/ptirq.h>

#define DBG_LEVEL_PTIRQ		6U

static void ptirq_handle_intx(const struct ptirq *irq)
{
	struct acrn_vm *vm = irq->vm;
	const union source_id *virt_sid = &irq->virt_sid;

	switch (virt_sid->intx_id.ctlr) {
	case INTX_CTLR_IOAPIC: {
		union ioapic_rte rte;
		bool trigger_lvl = false;

		/* INTX_CTLR_IOAPIC means we have vioapic enabled */
		vioapic_get_rte(vm, virt_sid->intx_id.gsi, &rte);

		if (rte.bits.trigger_mode == IOAPIC_RTE_TRGRMODE_LEVEL) {
			trigger_lvl = true;
		}

		if (trigger_lvl) {
			if (irq->polarity != 0U) {
				vioapic_set_irqline_lock(vm, virt_sid->intx_id.gsi, GSI_SET_LOW);
			} else {
				vioapic_set_irqline_lock(vm, virt_sid->intx_id.gsi, GSI_SET_HIGH);
			}
		} else {
			if (irq->polarity != 0U) {
				vioapic_set_irqline_lock(vm, virt_sid->intx_id.gsi, GSI_FALLING_PULSE);
			} else {
				vioapic_set_irqline_lock(vm, virt_sid->intx_id.gsi, GSI_RAISING_PULSE);
			}
		}

		dev_dbg(DBG_LEVEL_PTIRQ,
			"ptirq: irq=0x%x assert vr: 0x%x vRTE=0x%lx",
			irq->allocated_pirq, irq_to_vector(irq->allocated_pirq),
			rte.full);
		break;
	}
	case INTX_CTLR_PIC: {
		enum vpic_trigger trigger;

		/* INTX_CTLR_PIC means we have vpic enabled */
		vpic_get_irqline_trigger_mode(vm_pic(vm), virt_sid->intx_id.gsi, &trigger);

		if (trigger == LEVEL_TRIGGER) {
			vpic_set_irqline_lock(vm_pic(vm), virt_sid->intx_id.gsi, GSI_SET_HIGH);
		} else {
			vpic_set_irqline_lock(vm_pic(vm), virt_sid->intx_id.gsi, GSI_RAISING_PULSE);
		}
		break;
	}
	default:
		break;
	}
}

void ptirq_softirq_arch(struct ptirq *irq)
{
	struct msi_info *vmsi;

	/* handle real request */
	if (irq->intr_type == PTDEV_INTR_INTX) {
		ptirq_handle_intx(irq);
	} else {
		vmsi = &irq->vmsi;

		/* TODO: vmsi destmode check required */
		(void)vlapic_inject_msi(irq->vm, vmsi->addr.full, vmsi->data.full);

		dev_dbg(DBG_LEVEL_PTIRQ, "ptirq: irq=0x%x MSI VR: 0x%x-0x%x",
				irq->allocated_pirq, vmsi->data.bits.vector,
				irq_to_vector(irq->allocated_pirq));
		dev_dbg(DBG_LEVEL_PTIRQ, " vmsi_addr: 0x%lx vmsi_data: 0x%x",
				vmsi->addr.full, vmsi->data.full);
	}
}

uint32_t ptirq_get_irq_arch(uint32_t intr_type, union source_id *phys_sid)
{
	uint32_t phys_irq = IRQ_INVALID;

	if (intr_type == PTDEV_INTR_INTX) {
		phys_irq = ioapic_gsi_to_irq(phys_sid->intx_id.gsi);
	}

	return phys_irq;
}

void ptirq_intx_ack_arch(struct ptirq *irq)
{
	uint32_t phys_irq = irq->allocated_pirq;

	if (irq->active) {
		/*
		 * NOTE: only Level trigger will process EOI/ACK and if we got here
		 * means we have this vioapic or vpic or both enabled
		 */
		switch (irq->virt_sid.intx_id.ctlr) {
		case INTX_CTLR_IOAPIC:
			vioapic_set_irqline_nolock(irq->vm, irq->virt_sid.intx_id.gsi,
					(irq->polarity == 0U) ? GSI_SET_LOW : GSI_SET_HIGH);
			break;
		case INTX_CTLR_PIC:
			vpic_set_irqline_nolock(vm_pic(irq->vm), irq->virt_sid.intx_id.gsi,
					GSI_SET_LOW);
			break;
		default:
			break;
		}

		dev_dbg(DBG_LEVEL_PTIRQ, "ptirq: irq=0x%x acked vr: 0x%x",
				phys_irq, irq_to_vector(phys_irq));
		ioapic_gsi_unmask_irq(phys_irq);
	}
}
