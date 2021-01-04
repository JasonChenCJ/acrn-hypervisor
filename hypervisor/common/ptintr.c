/*
 * Copyright (C) 2018-2020 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <hash.h>
#include <errno.h>
#include <logmsg.h>
#include <util.h>
#include <x86/lib/spinlock.h>
#include <irq.h>
#include <x86/guest/vm.h>
#include <x86/ptirq.h>
#include <x86/ptintr.h>

#define DBG_LEVEL_PTINTR		6U

#define PTINTR_BITMAP_ARRAY_SIZE	INT_DIV_ROUNDUP(CONFIG_MAX_PT_IRQ_ENTRIES, 64U)
#define PTINTR_BITMAP_SIZE		(PTINTR_BITMAP_ARRAY_SIZE << 6U)
#define PTINTR_ENTRY_HASHBITS		9U
#define PTINTR_ENTRY_HASHSIZE		(1U << PTINTR_ENTRY_HASHBITS)

struct ptintr ptintr_entries[CONFIG_MAX_PT_IRQ_ENTRIES];
static uint64_t ptintr_entry_bitmaps[PTINTR_BITMAP_ARRAY_SIZE];
static spinlock_t ptintr_lock;

static struct ptintr_entry_head {
	struct hlist_head list;
} ptintr_entry_heads[PTINTR_ENTRY_HASHSIZE];

static inline uint16_t alloc_ptintr_id(void)
{
	uint16_t id = (uint16_t)ffz64_ex(ptintr_entry_bitmaps, PTINTR_BITMAP_SIZE);

	if (id < ARRAY_SIZE(ptintr_entries)) {
		bitmap_set_nolock(id & 0x3FU, &ptintr_entry_bitmaps[id >> 6U]);
	} else {
		id = INVALID_PTDEV_ENTRY_ID;
	}

	return id;
}

static void ptintr_free(struct ptintr *intr)
{
	intr->active = false;
	ptirq_free(intr->irq);
	hlist_del(&intr->phys_link);
	hlist_del(&intr->virt_link);
	bitmap_clear_nolock(intr->id & 0x3FU,
			&ptintr_entry_bitmaps[intr->id >> 6U]);
}

static struct ptintr *ptintr_alloc(struct acrn_vm *vm, uint32_t intr_type,
		union source_id *phys_sid, union source_id *virt_sid)
{
	struct ptintr *intr = NULL;
	uint16_t id = alloc_ptintr_id();
	uint64_t key;

	if (id < ARRAY_SIZE(ptintr_entries)) {
		intr = &ptintr_entries[id];
		(void)memset((void *)intr, 0U, sizeof(*intr));
		intr->id = id;
		intr->vm = vm;
		intr->intr_type = intr_type;
		intr->phys_sid = *phys_sid;
		intr->virt_sid = *virt_sid;

		key = hash64(intr->phys_sid.value, PTINTR_ENTRY_HASHBITS);
		hlist_add_head(&intr->phys_link, &(ptintr_entry_heads[key].list));
		key = hash64(intr->virt_sid.value, PTINTR_ENTRY_HASHBITS);
		hlist_add_head(&intr->virt_link, &(ptintr_entry_heads[key].list));

		if (ptirq_request(&intr->irq, vm, intr_type, phys_sid, virt_sid) < 0) {
			ptintr_free(intr);
		} else {
			intr->active = true;
		}
	} else {
		pr_err("ptintr alloc failed");
	}

	return intr;
}

/* ptintr_lock must be held */
static struct ptintr *ptintr_find(uint32_t intr_type,
		const union source_id *sid, const struct acrn_vm *vm)
{
	struct hlist_node *p;
	struct ptintr *n, *intr = NULL;
	uint64_t key = hash64(sid->value, PTINTR_ENTRY_HASHBITS);
	struct ptintr_entry_head *b = &ptintr_entry_heads[key];

	hlist_for_each(p, &b->list) {
		/* FIXME bug */
		if (vm == NULL) {
			n = hlist_entry(p, struct ptintr, phys_link);
		} else {
			n = hlist_entry(p, struct ptintr, virt_link);
		}

		if (n->active) {
			if ((intr_type == n->intr_type) &&
				((vm == NULL) ?
				(sid->value == n->phys_sid.value) :
				((vm == n->vm) && (sid->value == n->virt_sid.value)))) {
				intr = n;
				break;
			}
		}
	}

	return intr;
}

/*
 * add msix entry for a vm, based on msi id (phys_bdf+msix_index)
 * - if the entry not be added by any vm, allocate it
 * - if the entry already be added by sos_vm, then change the owner to current vm
 * - if the entry already be added by other vm, return NULL
 */
static struct ptintr *add_msix_entry(struct acrn_vm *vm,
		uint16_t virt_bdf, uint16_t phys_bdf, uint32_t entry_nr)
{
	struct ptintr *intr;
	DEFINE_MSI_SID(phys_sid, phys_bdf, entry_nr);
	DEFINE_MSI_SID(virt_sid, virt_bdf, entry_nr);

	intr = ptintr_find(PTDEV_INTR_MSI, &phys_sid, NULL);

	if (intr == NULL) {
		if (ptintr_find(PTDEV_INTR_MSI, &virt_sid, vm) == NULL) {
			intr = ptintr_alloc(vm, PTDEV_INTR_MSI,
					&phys_sid, &virt_sid);
		} else {
			pr_err("MSIX re-add VM%u vbdf%x", vm->vm_id, virt_bdf);
		}

		if (intr != NULL) {
			dev_dbg(DBG_LEVEL_PTINTR,
				"VM%u MSIX add vector mapping vbdf%x:pbdf%x idx=%d",
				vm->vm_id, virt_bdf, phys_bdf, entry_nr);
		}
	}

	return intr;
}

/*
 * Main entry for PCI device assignment with MSI and MSI-X
 * MSI can up to 8 vectors and MSI-X can up to 1024 Vectors
 * We use entry_nr to indicate coming vectors
 * entry_nr = 0 means first vector
 * user must provide bdf and entry_nr
 */
static int32_t add_msix_remapping(struct acrn_vm *vm, struct ptintr_add_msix *args)
{
	struct ptintr *intr;
	int32_t ret = 0;

	/*
	 * adds the mapping entries at runtime, if the
	 * entry already be held by others, return error.
	 */
	spinlock_obtain(&ptintr_lock);
	intr = add_msix_entry(vm, args->virt_bdf, args->phys_bdf, args->entry_nr);
	spinlock_release(&ptintr_lock);

	if (intr == NULL) {
		pr_err("%s: add msix remapping failed", __func__);
		ret = -ENODEV;
	}

	return ret;
}

/*
 * add intx entry for a vm, based on intx id (phys_pin)
 * - if the entry not be added by any vm, allocate it
 * - if the entry already be added by sos_vm, then change the owner to current vm
 * - if the entry already be added by other vm, return NULL
 */
static struct ptintr *add_intx_entry(struct acrn_vm *vm, uint32_t virt_gsi,
		uint32_t virt_ctlr, uint32_t phys_gsi, uint32_t phys_ctlr)
{
	struct ptintr *intr = NULL;
	DEFINE_INTX_SID(phys_sid, phys_gsi, phys_ctlr);
	DEFINE_INTX_SID(virt_sid, virt_gsi, virt_ctlr);

	intr = ptintr_find(PTDEV_INTR_INTX, &phys_sid, NULL);

	if (intr == NULL) {
		if (ptintr_find(PTDEV_INTR_INTX, &virt_sid, vm) == NULL) {
			intr = ptintr_alloc(vm, PTDEV_INTR_INTX,
					&phys_sid, &virt_sid);
		} else {
			pr_err("INTx re-add VM%u vpin %d", vm->vm_id, virt_gsi);
		}
	} else if (intr->vm != vm) {
		if (is_sos_vm(intr->vm)) {
			intr->vm = vm;
			intr->virt_sid = virt_sid;
			/* FIXME re-insert */
			ptirq_set_polarity(intr->irq, 0U);
		} else {
			pr_err("INTx gsi%d already in vm%u with vgsi%d, "
				"not able to add into vm%u with vgsi%d",
				phys_gsi, intr->vm->vm_id, intr->virt_sid.intx_id.gsi,
				vm->vm_id, virt_gsi);
			intr = NULL;
		}
	} else {
		/*
		 * The mapping has already been added to the VM. No action
		 * required.
		 */
	}

	/*
	 * intr is either created or transferred from SOS VM to Post-launched VM
	 */
	if (intr != NULL) {
		dev_dbg(DBG_LEVEL_PTINTR, "VM%u INTX add pin mapping vgsi%d:pgsi%d",
			intr->vm->vm_id, virt_gsi, phys_gsi);
	}

	return intr;
}

/*
 * Main entry for PCI/Legacy device assignment with INTx
 */
static int32_t add_intx_remapping(struct acrn_vm *vm, struct ptintr_add_intx *args)
{
	int32_t ret = 0;
	struct ptintr *intr = NULL;
	DEFINE_INTX_SID(virt_sid, args->virt_gsi, args->virt_ctlr);

	/*
	 * Device Model should pre-hold the mapping entries by calling
	 * ptintr_add for UOS.
	 */

	spinlock_obtain(&ptintr_lock);

	/* no remap for vuart intx */
	if (!is_vuart_intx(vm, virt_sid.intx_id.gsi)) {
		/* query if we have virt to phys mapping */
		if (ptintr_find(PTDEV_INTR_INTX, &virt_sid, vm) == NULL) {
			ret = ptintr_add_intx_arch(vm, &virt_sid);
		}
	} else {
		ret = -EINVAL;
	}

	if (ret == -ENODEV) {
		intr = add_intx_entry(vm, args->virt_gsi, args->virt_ctlr,
				args->phys_gsi, args->phys_ctlr);

		if (intr == NULL) {
			pr_err("%s: add intx remapping failed", __func__);
		} else {
			ret = 0;
		}
	} else if (ret == -EACCES) {
		/* FIXME re-insert */
		ret = 0;
	}
	spinlock_release(&ptintr_lock);

	return ret;
}

int32_t ptintr_add(struct acrn_vm *vm, struct ptintr_add_args *args)
{
	int32_t ret = -EINVAL;

	switch (args->intr_type) {
	case PTDEV_INTR_MSI:
		ret = add_msix_remapping(vm, &args->msix);
		break;
	case PTDEV_INTR_INTX:
		ret = add_intx_remapping(vm, &args->intx);
		break;
	default:
		pr_fatal("Unsupported intr_type %u", args->intr_type);
		break;
	}

	return ret;
}

static int32_t remap_msix(struct acrn_vm *vm, struct ptintr_remap_msix *args)
{
	int32_t ret = -EINVAL;
	struct ptintr *intr;
	DEFINE_MSI_SID(virt_sid, args->virt_bdf, args->entry_nr);

	spinlock_obtain(&ptintr_lock);
	intr = ptintr_find(PTDEV_INTR_MSI, &virt_sid, vm);

	if (intr != NULL) {
		ptirq_set_vmsi(intr->irq, args->info);
		ret = ptintr_remap_msix_arch(intr, args); /* pmsi is handled by arch */

		if (ret == 0) {
			*args->info = intr->pmsi;

			if (args->remap_cb != NULL) {
				ret = args->remap_cb(args->remap_arg);
			}
		}
	}
	spinlock_release(&ptintr_lock);

	return ret;
}

static int32_t remap_intx(struct acrn_vm *vm, struct ptintr_remap_intx *args)
{
	int32_t ret = -EINVAL;
	struct ptintr *intr;
	DEFINE_INTX_SID(virt_sid, args->virt_gsi, args->virt_ctlr);

	spinlock_obtain(&ptintr_lock);
	intr = ptintr_find(PTDEV_INTR_INTX, &virt_sid, vm);

	if (intr != NULL) {
		ret = ptintr_remap_intx_arch(intr, args);
	}
	spinlock_release(&ptintr_lock);

	return ret;
}

int32_t ptintr_remap(struct acrn_vm *vm, struct ptintr_remap_args *args)
{
	int32_t ret = -EINVAL;

	switch (args->intr_type) {
	case PTDEV_INTR_MSI:
		ret = remap_msix(vm, &args->msix);
		break;
	case PTDEV_INTR_INTX:
		ret = remap_intx(vm, &args->intx);
		break;
	default:
		pr_fatal("Unsupported intr_type %u", args->intr_type);
		break;
	}

	return ret;
}

static void remove_and_unmap_msix_entry(struct ptintr *intr)
{
	dev_dbg(DBG_LEVEL_PTINTR,
		"VM%u MSIX remove vector mapping vbdf-pbdf:0x%x-0x%x idx=%d",
		intr->vm->vm_id, intr->virt_sid.msi_id.bdf,
		intr->phys_sid.msi_id.bdf, intr->phys_sid.msi_id.entry_nr);

	ptintr_remove_msix_arch(intr);
	ptintr_free(intr);
}

/* deactivate & remove mapping entry of vbdf:entry_nr for vm */
static void remove_msix_remapping(const struct acrn_vm *vm, struct ptintr_rmv_msix *args)
{
	struct ptintr *intr;
	DEFINE_MSI_SID(phys_sid, args->phys_bdf, args->entry_nr);

	spinlock_obtain(&ptintr_lock);
	intr = ptintr_find(PTDEV_INTR_MSI, &phys_sid, NULL);

	if ((intr != NULL) && (intr->vm == vm)) {
		remove_and_unmap_msix_entry(intr);
	}
	spinlock_release(&ptintr_lock);
}

static void remove_and_unmap_intx_entry(struct ptintr *intr)
{
	dev_dbg(DBG_LEVEL_PTINTR,
		"remove intx intr: vgsi_ctlr=%u vgsi=%u pgsi=%u from VM%u",
		intr->virt_sid.intx_id.ctlr, intr->virt_sid.intx_id.gsi,
		intr->phys_sid.intx_id.gsi, intr->vm->vm_id);

	ptintr_remove_intx_arch(intr);
	ptintr_free(intr);
}

/* deactivate & remove mapping entry of vpin for vm */
static void remove_intx_remapping(const struct acrn_vm *vm, struct ptintr_rmv_intx *args)
{
	struct ptintr *intr;
	DEFINE_INTX_SID(virt_sid, args->virt_gsi, args->virt_ctlr);

	spinlock_obtain(&ptintr_lock);
	intr = ptintr_find(PTDEV_INTR_INTX, &virt_sid, vm);

	if (intr != NULL) {
		remove_and_unmap_intx_entry(intr);
	}
	spinlock_release(&ptintr_lock);
}

void ptintr_remove_and_unmap(struct acrn_vm *vm, struct ptintr_rmv_args *args)
{
	switch (args->intr_type) {
	case PTDEV_INTR_MSI:
		remove_msix_remapping(vm, &args->msix);
		break;
	case PTDEV_INTR_INTX:
		remove_intx_remapping(vm, &args->intx);
		break;
	default:
		pr_fatal("Unsupported intr_type %u", args->intr_type);
		break;
	}
}

void ptintr_remove_and_unmap_vm(const struct acrn_vm *vm)
{
	uint16_t i;
	struct ptintr *intr;

	spinlock_obtain(&ptintr_lock);

	/* VM is already down */
	for (i = 0U; i < ARRAY_SIZE(ptintr_entries); i++) {
		intr = &ptintr_entries[i];

		if (intr->active && (intr->vm == vm)) {
			if (intr->intr_type == PTDEV_INTR_MSI) {
				remove_and_unmap_msix_entry(intr);
			} else {
				remove_and_unmap_intx_entry(intr);
			}
		}
	}
	spinlock_release(&ptintr_lock);
}

void ptintr_intx_ack(struct acrn_vm *vm, uint32_t virt_gsi, enum intx_ctlr vgsi_ctlr)
{
	struct ptintr *intr;
	DEFINE_INTX_SID(virt_sid, virt_gsi, vgsi_ctlr);

	spinlock_obtain(&ptintr_lock);
	intr = ptintr_find(PTDEV_INTR_INTX, &virt_sid, vm);

	if ((intr != NULL) && intr->active) {
		ptirq_intx_ack_arch(intr->irq);
	}
	spinlock_release(&ptintr_lock);
}

uint32_t ptintr_get_intr_data(const struct acrn_vm *target_vm, uint64_t *buffer, uint32_t buffer_cnt)
{
	uint16_t i;
	uint32_t pos = 0U;
	struct ptintr *intr;

	spinlock_obtain(&ptintr_lock);

	for (i = 0U; i < ARRAY_SIZE(ptintr_entries); i++) {
		intr = &ptintr_entries[i];

		if (intr->active && (intr->vm == target_vm)) {
			if (ptirq_get_intr_data(
				intr->irq, buffer, &pos, buffer_cnt) < 0) {
				break;
			}
		}
	}
	spinlock_release(&ptintr_lock);

	return pos;
}

void ptintr_init(void)
{
	if (get_pcpu_id() == BSP_CPU_ID) {
		spinlock_init(&ptintr_lock);
	}
	ptintr_init_arch(ptintr_find);
	ptirq_init();
}
