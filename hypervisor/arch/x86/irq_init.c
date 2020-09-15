/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <x86/io.h>
#include <x86/irq.h>
#include <x86/idt.h>
#include <x86/ioapic.h>
#include <x86/lapic.h>
#include <logmsg.h>

static void disable_pic_irqs(void)
{
	pio_write8(0xffU, 0xA1U);
	pio_write8(0xffU, 0x21U);
}

static inline void fixup_idt(const struct host_idt_descriptor *idtd)
{
	uint32_t i;
	struct idt_64_descriptor *idt_desc = idtd->idt->host_idt_descriptors;
	uint32_t entry_hi_32, entry_lo_32;

	for (i = 0U; i < HOST_IDT_ENTRIES; i++) {
		entry_lo_32 = idt_desc[i].offset_63_32;
		entry_hi_32 = idt_desc[i].rsvd;
		idt_desc[i].rsvd = 0U;
		idt_desc[i].offset_63_32 = entry_hi_32;
		idt_desc[i].high32.bits.offset_31_16 = entry_lo_32 >> 16U;
		idt_desc[i].low32.bits.offset_15_0 = entry_lo_32 & 0xffffUL;
	}
}

static inline void set_idt(struct host_idt_descriptor *idtd)
{
	asm volatile ("   lidtq %[idtd]\n" :	/* no output parameters */
		      :		/* input parameters */
		      [idtd] "m"(*idtd));
}

void init_interrupt_arch(uint16_t pcpu_id)
{
	struct host_idt_descriptor *idtd = &HOST_IDTR;

	if (pcpu_id == BSP_CPU_ID) {
		fixup_idt(idtd);
	}
	set_idt(idtd);
	init_lapic(pcpu_id);

	if (pcpu_id == BSP_CPU_ID) {
		/* we use ioapic only, disable legacy PIC */
		disable_pic_irqs();
		ioapic_setup_irqs();
	}
}
