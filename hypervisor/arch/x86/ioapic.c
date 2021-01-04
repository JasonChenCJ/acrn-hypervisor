/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
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
#include <x86/mmu.h>
#include <x86/pgtable.h>
#include <acpi.h>
#include <logmsg.h>

#define DBG_LEVEL_IOAPIC		6U

static union ioapic_rte saved_rte[CONFIG_MAX_IOAPIC_NUM][CONFIG_MAX_IOAPIC_LINES];

struct ioapic_info ioapic_array[CONFIG_MAX_IOAPIC_NUM];

uint8_t ioapic_num;
spinlock_t ioapic_lock;

uint8_t get_platform_ioapic_info (struct ioapic_info **plat_ioapic_info)
{
	*plat_ioapic_info = ioapic_array;
	return ioapic_num;
}

/**
 * @pre apic_id < 2
 */
static inline uint64_t
get_ioapic_base(uint8_t apic_id)
{
	/* the ioapic base should be extracted from ACPI MADT table */
	return ioapic_array[apic_id].addr;
}

static uint32_t
ioapic_nr_pins(void *ioapic_base)
{
	uint32_t version;
	uint32_t nr_pins;

	version = ioapic_read_reg32(ioapic_base, IOAPIC_VER);
	dev_dbg(DBG_LEVEL_IOAPIC, "IOAPIC version: %x", version);

	/* The 23:16 bits in the version register is the highest entry in the
	 * I/O redirection table, which is 1 smaller than the number of
	 * interrupt input pins. */
	nr_pins = (((version & IOAPIC_MAX_RTE_MASK) >> MAX_RTE_SHIFT) + 1U);


	return nr_pins;
}

void *map_ioapic(uint64_t ioapic_paddr)
{
	/* At some point we may need to translate this paddr to a vaddr.
	 * 1:1 mapping for now.
	 */
	return hpa2hva(ioapic_paddr);
}

void ioapic_get_rte_entry(void *ioapic_base, uint32_t pin, union ioapic_rte *rte)
{
	uint32_t rte_addr = (pin * 2U) + 0x10U;
	rte->u.lo_32 = ioapic_read_reg32(ioapic_base, rte_addr);
	rte->u.hi_32 = ioapic_read_reg32(ioapic_base, rte_addr + 1U);
}

int32_t init_ioapic_id_info(void)
{
	int32_t ret = 0;
	uint8_t ioapic_id;
	void *addr;
	uint32_t nr_pins, gsi;

	ioapic_num = parse_madt_ioapic(&ioapic_array[0]);
	if (ioapic_num <= (uint8_t)CONFIG_MAX_IOAPIC_NUM) {
		/*
		 * Iterate thru all the IO-APICs on the platform
		 * Check the number of pins available on each IOAPIC is less
		 * than the CONFIG_MAX_IOAPIC_LINES
		 */

		gsi = 0U;
		for (ioapic_id = 0U; ioapic_id < ioapic_num; ioapic_id++) {
			addr = map_ioapic(ioapic_array[ioapic_id].addr);
			ppt_clear_user_bit((uint64_t)addr, PAGE_SIZE);

			nr_pins = ioapic_nr_pins(addr);
			if (nr_pins <= (uint32_t) CONFIG_MAX_IOAPIC_LINES) {
				gsi += nr_pins;
				ioapic_array[ioapic_id].nr_pins = nr_pins;
			} else {
				pr_err ("Pin count %x of IOAPIC with %x > CONFIG_MAX_IOAPIC_LINES, bump up CONFIG_MAX_IOAPIC_LINES!",
							nr_pins, ioapic_array[ioapic_id].id);
				ret = -EINVAL;
				break;
			}
		}

		/*
		 * Check if total pin count, can be inferred by GSI, is
		 * atleast same as the number of Legacy IRQs, NR_LEGACY_IRQ
		 */

		if (ret == 0) {
			if (gsi < (uint32_t) NR_LEGACY_PIN) {
				pr_err ("Total pin count (%x) is less than NR_LEGACY_IRQ!", gsi);
				ret = -EINVAL;
			}
		}
	} else {
		pr_err ("Number of IOAPIC on platform %x > CONFIG_MAX_IOAPIC_NUM, try bumping up CONFIG_MAX_IOAPIC_NUM!",
						ioapic_num);
		ret = -EINVAL;
	}


	return ret;
}

void init_ioapic(void)
{
	uint8_t ioapic_id;
	union ioapic_rte rte;

	rte.full = 0UL;
	rte.bits.intr_mask  = IOAPIC_RTE_MASK_SET;

	spinlock_init(&ioapic_lock);

	for (ioapic_id = 0U;
	     ioapic_id < ioapic_num; ioapic_id++) {
		void *addr;
		uint32_t pin, nr_pins;

		addr = map_ioapic(ioapic_array[ioapic_id].addr);

		nr_pins = ioapic_array[ioapic_id].nr_pins;
		for (pin = 0U; pin < nr_pins; pin++) {
			ioapic_set_rte_entry(addr, pin, rte);
		}
	}
}

void suspend_ioapic(void)
{
	uint8_t ioapic_id;
	uint32_t ioapic_pin;

	for (ioapic_id = 0U; ioapic_id < ioapic_num; ioapic_id++) {
		void *addr;

		addr = map_ioapic(get_ioapic_base(ioapic_id));
		for (ioapic_pin = 0U; ioapic_pin < ioapic_array[ioapic_id].nr_pins; ioapic_pin++) {
			ioapic_get_rte_entry(addr, ioapic_pin,
				&saved_rte[ioapic_id][ioapic_pin]);
		}
	}
}

void resume_ioapic(void)
{
	uint8_t ioapic_id;
	uint32_t ioapic_pin;

	for (ioapic_id = 0U; ioapic_id < ioapic_num; ioapic_id++) {
		void *addr;

		addr = map_ioapic(get_ioapic_base(ioapic_id));
		for (ioapic_pin = 0U; ioapic_pin < ioapic_array[ioapic_id].nr_pins; ioapic_pin++) {
			ioapic_set_rte_entry(addr, ioapic_pin,
				saved_rte[ioapic_id][ioapic_pin]);
		}
	}
}
