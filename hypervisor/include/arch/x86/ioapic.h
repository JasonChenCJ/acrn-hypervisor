/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef IOAPIC_H
#define IOAPIC_H

#include <x86/apicreg.h>
#include <types.h>
#include <x86/lib/spinlock.h>
#include <x86/ioapic.h>
#include <x86/io.h>
#include <logmsg.h>

extern uint8_t ioapic_num;
extern spinlock_t ioapic_lock;

#define NR_LEGACY_IRQ		16U
#define NR_LEGACY_PIN		NR_LEGACY_IRQ

struct ioapic_info {
	uint8_t id;		/* IOAPIC ID as indicated in ACPI MADT */
	uint32_t addr;		/* IOAPIC Register address */
	uint32_t gsi_base;	/* Global System Interrupt where this IO-APIC's interrupt input start */
	uint32_t nr_pins;	/* Number of Interrupt inputs as determined by Max. Redir Entry Register */
};

extern struct ioapic_info ioapic_array[CONFIG_MAX_IOAPIC_NUM];

void ioapic_setup_irqs(void);
void init_ioapic();

bool is_ioapic_irq(uint32_t irq);
uint32_t gsi_to_ioapic_pin(uint32_t gsi);
int32_t init_ioapic_id_info(void);
uint8_t ioapic_irq_to_ioapic_id(uint32_t irq);

uint8_t get_platform_ioapic_info (struct ioapic_info **plat_ioapic_info);
void *map_ioapic(uint64_t ioapic_paddr);
void ioapic_get_rte_entry(void *ioapic_base, uint32_t pin, union ioapic_rte *rte);

static inline uint32_t
ioapic_read_reg32(void *ioapic_base, const uint32_t offset)
{
	uint32_t v;
	uint64_t rflags;

	spinlock_irqsave_obtain(&ioapic_lock, &rflags);

	/* Write IOREGSEL */
	mmio_write32(offset, ioapic_base + IOAPIC_REGSEL);
	/* Read  IOWIN */
	v = mmio_read32(ioapic_base + IOAPIC_WINDOW);

	spinlock_irqrestore_release(&ioapic_lock, rflags);
	return v;
}

static inline void
ioapic_write_reg32(void *ioapic_base, const uint32_t offset, const uint32_t value)
{
	uint64_t rflags;

	spinlock_irqsave_obtain(&ioapic_lock, &rflags);

	/* Write IOREGSEL */
	mmio_write32(offset, ioapic_base + IOAPIC_REGSEL);
	/* Write IOWIN */
	mmio_write32(value, ioapic_base + IOAPIC_WINDOW);

	spinlock_irqrestore_release(&ioapic_lock, rflags);
}

static inline void
ioapic_set_rte_entry(void *ioapic_base,
		uint32_t pin, union ioapic_rte rte)
{
	uint32_t rte_addr = (pin * 2U) + 0x10U;
	ioapic_write_reg32(ioapic_base, rte_addr, rte.u.lo_32);
	ioapic_write_reg32(ioapic_base, rte_addr + 1U, rte.u.hi_32);
}

/**
 * @defgroup ioapic_ext_apis IOAPIC External Interfaces
 *
 * This is a group that includes IOAPIC External Interfaces.
 *
 * @{
 */

/**
 * @brief Get irq num from gsi num
 *
 * @param[in]	gsi The gsi number
 *
 * @return irq number
 */
uint32_t ioapic_gsi_to_irq(uint32_t gsi);
/**
 * @brief Set the redirection table entry
 *
 * Set the redirection table entry of an interrupt
 *
 * @param[in]	irq The number of irq to set
 * @param[in]	rte Union of ioapic_rte to set
 */
void ioapic_set_rte(uint32_t irq, union ioapic_rte rte);

/**
 * @brief Get the redirection table entry
 *
 * Get the redirection table entry of an interrupt
 *
 * @param[in]	irq The number of irq to fetch RTE
 * @param[inout]	rte Pointer to union ioapic_rte to return result RTE
 *
 * @pre rte != NULL
 */
void ioapic_get_rte(uint32_t irq, union ioapic_rte *rte);

/**
 * @brief Suspend ioapic
 *
 * Suspend ioapic, mainly save the RTEs.
 */
void suspend_ioapic(void);

/**
 * @brief Resume ioapic
 *
 * Resume ioapic, mainly restore the RTEs.
 */
void resume_ioapic(void);

/**
 * @}
 */
/* End of ioapic_ext_apis */

void ioapic_gsi_mask_irq(uint32_t irq);
void ioapic_gsi_unmask_irq(uint32_t irq);

void ioapic_get_rte_entry(void *ioapic_base, uint32_t pin, union ioapic_rte *rte);

/*
 * is_valid is by default false when all the
 * static variables, part of .bss, are initialized to 0s
 * It is set to true, if the corresponding
 * gsi falls in ranges identified by IOAPIC data
 * in ACPI MADT in ioapic_setup_irqs.
 */

struct gsi_table {
	bool is_valid;
	struct {
		uint8_t acpi_id;
		uint8_t index;
		uint32_t pin;
		void  *base_addr;
	} ioapic_info;
};

void *gsi_to_ioapic_base(uint32_t gsi);
uint32_t get_max_nr_gsi(void);
uint8_t get_gsi_to_ioapic_index(uint32_t gsi);
uint32_t get_pic_pin_from_ioapic_pin(uint32_t pin_index);
bool is_gsi_valid(uint32_t gsi);
#endif /* IOAPIC_H */
