/*
 * Copyright (C) 2020 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <errno.h>
#include <x86/lib/spinlock.h>
#include <x86/ioapic.h>
#include <util.h>
#include <interrupt/include/irq.h>
#include <interrupt/sub/x86/include/irq.h>
#include <logmsg.h>

#define DBG_LEVEL_IOAPIC		6U

#define NR_MAX_GSI		(CONFIG_MAX_IOAPIC_NUM * CONFIG_MAX_IOAPIC_LINES)

#define DEFAULT_DEST_MODE	IOAPIC_RTE_DESTMODE_LOGICAL
#define DEFAULT_DELIVERY_MODE	IOAPIC_RTE_DELMODE_LOPRI


static struct gsi_table gsi_table_data[NR_MAX_GSI];
static uint32_t ioapic_max_nr_gsi;

static const uint32_t legacy_irq_trigger_mode[NR_LEGACY_PIN] = {
	IOAPIC_RTE_TRGRMODE_EDGE, /* IRQ2*/
	IOAPIC_RTE_TRGRMODE_EDGE, /* IRQ1*/
	IOAPIC_RTE_TRGRMODE_EDGE, /* IRQ0*/
	IOAPIC_RTE_TRGRMODE_EDGE, /* IRQ3*/
	IOAPIC_RTE_TRGRMODE_EDGE, /* IRQ4*/
	IOAPIC_RTE_TRGRMODE_EDGE, /* IRQ5*/
	IOAPIC_RTE_TRGRMODE_EDGE, /* IRQ6*/
	IOAPIC_RTE_TRGRMODE_EDGE, /* IRQ7*/
	IOAPIC_RTE_TRGRMODE_EDGE, /* IRQ8*/
	IOAPIC_RTE_TRGRMODE_LEVEL, /* IRQ9*/
	IOAPIC_RTE_TRGRMODE_EDGE, /* IRQ10*/
	IOAPIC_RTE_TRGRMODE_EDGE, /* IRQ11*/
	IOAPIC_RTE_TRGRMODE_EDGE, /* IRQ12*/
	IOAPIC_RTE_TRGRMODE_EDGE, /* IRQ13*/
	IOAPIC_RTE_TRGRMODE_EDGE, /* IRQ14*/
	IOAPIC_RTE_TRGRMODE_EDGE, /* IRQ15*/
};

static const uint32_t pic_ioapic_pin_map[NR_LEGACY_PIN] = {
	2U, /* pin0*/
	1U, /* pin1*/
	0U, /* pin2*/
	3U, /* pin3*/
	4U, /* pin4*/
	5U, /* pin5*/
	6U, /* pin6*/
	7U, /* pin7*/
	8U, /* pin8*/
	9U, /* pin9*/
	10U, /* pin10*/
	11U, /* pin11*/
	12U, /* pin12*/
	13U, /* pin13*/
	14U, /* pin14*/
	15U, /* pin15*/
};

uint32_t get_pic_pin_from_ioapic_pin(uint32_t pin_index)
{
	uint32_t pin_id = INVALID_INTERRUPT_PIN;
	if (pin_index < NR_LEGACY_PIN) {
		pin_id = pic_ioapic_pin_map[pin_index];
	}
	return pin_id;
}

uint8_t get_gsi_to_ioapic_index(uint32_t gsi)
{
	return gsi_table_data[gsi].ioapic_info.index;
}

/*
 * @pre gsi < NR_MAX_GSI
 */
void *gsi_to_ioapic_base(uint32_t gsi)
{

	return gsi_table_data[gsi].ioapic_info.base_addr;
}

uint32_t get_max_nr_gsi(void)
{
	return ioapic_max_nr_gsi;
}

static inline union ioapic_rte
create_rte_for_legacy_irq(uint32_t irq, uint32_t vr)
{
	union ioapic_rte rte;

	/* Legacy IRQ 0-15 setup, default masked
	 * are actually defined in either MPTable or ACPI MADT table
	 * before we have ACPI table parsing in HV we use common hardcode
	 */

	rte.full = 0UL;
	rte.bits.intr_mask  = IOAPIC_RTE_MASK_SET;
	rte.bits.trigger_mode = legacy_irq_trigger_mode[irq];
	rte.bits.dest_mode = DEFAULT_DEST_MODE;
	rte.bits.delivery_mode = DEFAULT_DELIVERY_MODE;
	rte.bits.vector = vr;

	/* Fixed to active high */
	rte.bits.intr_polarity = IOAPIC_RTE_INTPOL_AHI;

	/* Dest field: legacy irq fixed to CPU0 */
	rte.bits.dest_field = 1U;

	return rte;
}

static inline union ioapic_rte
create_rte_for_gsi_irq(uint32_t irq, uint32_t vr)
{
	union ioapic_rte rte;

	rte.full = 0UL;

	if (irq < NR_LEGACY_PIN) {
		rte = create_rte_for_legacy_irq(irq, vr);
	} else {
		/* irq default masked, level trig */
		rte.bits.intr_mask  = IOAPIC_RTE_MASK_SET;
		rte.bits.trigger_mode = IOAPIC_RTE_TRGRMODE_LEVEL;
		rte.bits.dest_mode = DEFAULT_DEST_MODE;
		rte.bits.delivery_mode = DEFAULT_DELIVERY_MODE;
		rte.bits.vector = vr;

		/* Fixed to active high */
		rte.bits.intr_polarity = IOAPIC_RTE_INTPOL_AHI;

		/* Dest field */
		rte.bits.dest_field = (uint8_t) ALL_CPUS_MASK;
	}

	return rte;
}

static void ioapic_set_routing(uint32_t gsi, uint32_t vr)
{
	void *ioapic_base;
	union ioapic_rte rte;

	ioapic_base = gsi_to_ioapic_base(gsi);
	rte = create_rte_for_gsi_irq(gsi, vr);
	ioapic_set_rte_entry(ioapic_base, gsi_table_data[gsi].ioapic_info.pin, rte);

	if (rte.bits.trigger_mode == IOAPIC_RTE_TRGRMODE_LEVEL) {
		set_irq_trigger_mode(gsi, true);
	} else {
		set_irq_trigger_mode(gsi, false);
	}

	dev_dbg(DBG_LEVEL_IOAPIC, "GSI: irq:%d pin:%hhu rte:%lx",
		gsi, gsi_table_data[gsi].ioapic_info.pin,
		rte.full);
}

/*
 * @pre rte != NULL
 * @pre is_ioapic_irq(irq) == true
 */
void ioapic_get_rte(uint32_t irq, union ioapic_rte *rte)
{
	void *addr;

	addr = gsi_to_ioapic_base(irq);
	ioapic_get_rte_entry(addr, gsi_table_data[irq].ioapic_info.pin, rte);
}

/*
 * @pre is_ioapic_irq(irq) == true
 */
void ioapic_set_rte(uint32_t irq, union ioapic_rte rte)
{
	void *addr;

	addr = gsi_to_ioapic_base(irq);
	ioapic_set_rte_entry(addr, gsi_table_data[irq].ioapic_info.pin, rte);

	if (rte.bits.trigger_mode == IOAPIC_RTE_TRGRMODE_LEVEL) {
		set_irq_trigger_mode(irq, true);
	} else {
		set_irq_trigger_mode(irq, false);
	}

	dev_dbg(DBG_LEVEL_IOAPIC, "GSI: irq:%d pin:%hhu rte:%lx",
		irq, gsi_table_data[irq].ioapic_info.pin,
		rte.full);
}

/*
 * Checks if the gsi is valid
 * 1) gsi < NR_MAX_GSI
 * 2) gsi is valid on the platform according to ACPI MADT info
 */
bool is_gsi_valid(uint32_t gsi)
{

	return (gsi < NR_MAX_GSI) && (gsi_table_data[gsi].is_valid);
}

/*
 * IO-APIC gsi and irq are identity mapped in ioapic_setup_irqs
 * So #gsi = #irq for ACRN
 */

bool is_ioapic_irq(uint32_t irq)
{

	return is_gsi_valid(irq);
}

/*
 *@pre gsi < NR_MAX_GSI
 *@pre is_gsi_valid(gsi) == true
 */

uint32_t gsi_to_ioapic_pin(uint32_t gsi)
{
	return gsi_table_data[gsi].ioapic_info.pin;
}

/*
 *@pre is_gsi_valid(gsi) == true
 */
uint32_t ioapic_gsi_to_irq(uint32_t gsi)
{
	return gsi;
}

static void
ioapic_irq_gsi_mask_unmask(uint32_t irq, bool mask)
{
	void *addr = NULL;
	uint32_t pin;
	union ioapic_rte rte;

	addr = gsi_to_ioapic_base(irq);
	pin = gsi_table_data[irq].ioapic_info.pin;

	if (addr != NULL) {
		ioapic_get_rte_entry(addr, pin, &rte);
		if (mask) {
			rte.bits.intr_mask = IOAPIC_RTE_MASK_SET;
		} else {
			rte.bits.intr_mask = IOAPIC_RTE_MASK_CLR;
		}
		ioapic_set_rte_entry(addr, pin, rte);
		dev_dbg(DBG_LEVEL_IOAPIC, "update: irq:%d pin:%hhu rte:%lx",
			irq, pin, rte.full);
	} else {
		dev_dbg(DBG_LEVEL_IOAPIC, "NULL Address returned from gsi_table_data");
	}
}

void ioapic_gsi_mask_irq(uint32_t irq)
{
	ioapic_irq_gsi_mask_unmask(irq, true);
}

void ioapic_gsi_unmask_irq(uint32_t irq)
{
	ioapic_irq_gsi_mask_unmask(irq, false);
}

/*
 * @pre is_ioapic_irq(irq) == true
 */
uint8_t ioapic_irq_to_ioapic_id(uint32_t irq)
{

	return gsi_table_data[irq].ioapic_info.acpi_id;
}

void ioapic_setup_irqs(void)
{
	uint8_t ioapic_id;
	uint32_t gsi = 0U;
	uint32_t vr;

	for (ioapic_id = 0U;
	     ioapic_id < ioapic_num; ioapic_id++) {
		void *addr;
		uint32_t pin, nr_pins;

		addr = map_ioapic(ioapic_array[ioapic_id].addr);

		nr_pins = ioapic_array[ioapic_id].nr_pins;
		gsi = ioapic_array[ioapic_id].gsi_base;
		for (pin = 0U; pin < nr_pins; pin++) {
			gsi_table_data[gsi].is_valid = true;
			gsi_table_data[gsi].ioapic_info.acpi_id = ioapic_array[ioapic_id].id;
			gsi_table_data[gsi].ioapic_info.base_addr = addr;
			gsi_table_data[gsi].ioapic_info.pin = pin;
			gsi_table_data[gsi].ioapic_info.index = ioapic_id;

			/* pinned irq before use it */
			if (reserve_irq_num(gsi) == IRQ_INVALID) {
				pr_err("failed to alloc IRQ[%d]", gsi);
				gsi++;
				continue;
			}

			/* assign vector for this GSI
			 * for legacy irq, reserved vector and never free
			 */
			if (gsi < NR_LEGACY_PIN) {
				vr = alloc_irq_vector(gsi);
				if (vr == VECTOR_INVALID) {
					pr_err("failed to alloc VR");
					gsi++;
					continue;
				}
			} else {
				vr = 0U; /* not to allocate VR right now */
			}

			ioapic_set_routing(gsi, vr);
			gsi++;
		}
	}

	/* system max gsi numbers */
	ioapic_max_nr_gsi = gsi;
}
