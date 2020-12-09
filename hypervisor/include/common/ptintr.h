/*
 * Copyright (C) 2018-2020 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PTINTR_H
#define PTINTR_H

#include <list.h>

enum intx_ctlr {
	INTX_CTLR_IOAPIC	= 0U,
	INTX_CTLR_PIC,
	INTX_CTLR_MAX		= UINT32_MAX
} __packed;

#define PTDEV_INTR_MSI		(1U << 0U)
#define PTDEV_INTR_INTX		(1U << 1U)

#define INVALID_PTDEV_ENTRY_ID 0xffffU

#define DEFINE_MSI_SID(name, a, b)	\
union source_id (name) = {.msi_id = {.bdf = (a), .entry_nr = (b)} }

#define DEFINE_INTX_SID(name, a, b)	\
union source_id (name) = {.intx_id = {.gsi = (a), .ctlr = (b)} }

union source_id {
	uint64_t value;
	struct {
		uint16_t bdf;
		uint16_t entry_nr;
		uint32_t reserved;
	} msi_id;
	/*
	 * ctlr indicates if the source of interrupt is IO-APIC or PIC
	 * pin indicates the pin number of interrupt controller determined by ctlr
	 */
	struct {
		uint32_t ctlr;
		uint32_t gsi;
	} intx_id;
};

/*
 * Macros for bits in union msi_addr_reg
 */
#define	MSI_ADDR_BASE			0xfeeUL	/* Base address for MSI messages */
#define	MSI_ADDR_RH			0x1U	/* Redirection Hint */
#define	MSI_ADDR_DESTMODE_LOGICAL	0x1U	/* Destination Mode: Logical*/
#define	MSI_ADDR_DESTMODE_PHYS		0x0U	/* Destination Mode: Physical*/

union msi_addr_reg {
	uint64_t full;
	struct {
		uint32_t rsvd_1:2;
		uint32_t dest_mode:1;
		uint32_t rh:1;
		uint32_t rsvd_2:8;
		uint32_t dest_field:8;
		uint32_t addr_base:12;
		uint32_t hi_32;
	} bits __packed;
	struct {
		uint32_t rsvd_1:2;
		uint32_t intr_index_high:1;
		uint32_t shv:1;
		uint32_t intr_format:1;
		uint32_t intr_index_low:15;
		uint32_t constant:12;
		uint32_t hi_32;
	} ir_bits __packed;

};

/*
 * Macros for bits in union msi_data_reg
 */
#define MSI_DATA_DELMODE_FIXED		0x0U	/* Delivery Mode: Fixed */
#define MSI_DATA_DELMODE_LOPRI		0x1U	/* Delivery Mode: Low Priority */
#define MSI_DATA_TRGRMODE_EDGE		0x0U	/* Trigger Mode: Edge */
#define MSI_DATA_TRGRMODE_LEVEL		0x1U	/* Trigger Mode: Level */

union msi_data_reg {
	uint32_t full;
	struct {
		uint32_t vector:8;
		uint32_t delivery_mode:3;
		uint32_t rsvd_1:3;
		uint32_t level:1;
		uint32_t trigger_mode:1;
		uint32_t rsvd_2:16;
	} bits __packed;
};

struct msi_info {
	union msi_addr_reg addr;
	union msi_data_reg data;
};

struct ptintr_add_args {
	uint32_t intr_type;
	union {
		struct ptintr_add_msix {
			uint16_t virt_bdf;
			uint16_t phys_bdf;
			uint16_t entry_nr;
		} msix;
		struct ptintr_add_intx {
			uint32_t virt_gsi;
			uint32_t virt_ctlr;
			uint32_t phys_gsi;
			uint32_t phys_ctlr;
		} intx;
	};
};

struct ptintr_remap_args {
	uint32_t intr_type;
	union {
		struct ptintr_remap_msix {
			uint16_t virt_bdf;
			uint16_t entry_nr;
			uint16_t irte_idx;
			struct msi_info *info;
			void *remap_arg;
			int32_t (*remap_cb)(void *);
		} msix;
		struct ptintr_remap_intx {
			uint32_t virt_gsi;
			uint32_t virt_ctlr;
		} intx;
	};
};

struct ptintr_rmv_args {
	uint32_t intr_type;
	union {
		struct ptintr_rmv_msix {
			uint16_t phys_bdf;
			uint32_t entry_nr;
		} msix;
		struct ptintr_rmv_intx {
			uint32_t virt_gsi;
			uint32_t virt_ctlr;
		} intx;
	};
};

struct ptirq;

/*
 * one entry per allocated irq/vector
 * it represents a pass-thru device's remapping data entry which collecting
 * information related with its vm and msi/intx mapping & interaction nodes
 * with interrupt handler and softirq.
 */
struct ptintr {
	struct hlist_node phys_link;
	struct hlist_node virt_link;
	uint16_t id;
	uint32_t intr_type;
	bool active; /* true=active, false=inactive */
	union source_id phys_sid;
	union source_id virt_sid;
	struct acrn_vm *vm;
	struct msi_info pmsi;
	uint16_t irte_idx;
	struct ptirq *irq;
};

extern struct ptintr ptintr_entries[CONFIG_MAX_PT_IRQ_ENTRIES];

int32_t ptintr_add(struct acrn_vm *vm, struct ptintr_add_args *args);
int32_t ptintr_remap(struct acrn_vm *vm, struct ptintr_remap_args *args);
void ptintr_remove_and_unmap(struct acrn_vm *vm, struct ptintr_rmv_args *args);
void ptintr_remove_and_unmap_vm(const struct acrn_vm *vm);
void ptintr_intx_ack(struct acrn_vm *vm, uint32_t virt_gsi, enum intx_ctlr vgsi_ctlr);
uint32_t ptintr_get_intr_data(const struct acrn_vm *target_vm, uint64_t *buffer, uint32_t buffer_cnt);
void ptintr_init(void);
#endif /* PTINTR_H */
