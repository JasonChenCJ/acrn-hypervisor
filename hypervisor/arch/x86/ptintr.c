/*
 * Copyright (C) 2018-2020 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <ptirq.h>
#include <x86/vtd.h>
#include <x86/guest/vm.h>
#include <x86/per_cpu.h>
#include <x86/ioapic.h>
#include <x86/pgtable.h>
#include <x86/ptintr.h>

#define DBG_LEVEL_PTINTR		6U

union irte_index {
	uint16_t index;
	struct {
		uint16_t index_low:15;
		uint16_t index_high:1;
	} bits __packed;
};

static struct ptintr *(*find_intr)(uint32_t, const union source_id *,
		const struct acrn_vm *);

/*
 * Check if the IRQ is single-destination and return the destination vCPU if so.
 *
 * VT-d PI (posted mode) cannot support multicast/broadcast IRQs.
 * If returns NULL, this means it is multicast/broadcast IRQ and
 * we can only handle it in remapped mode.
 * If returns non-NULL, the destination vCPU is returned, which means it is
 * single-destination IRQ and we can handle it in posted mode.
 *
 * @pre (vm != NULL) && (info != NULL)
 */
static struct acrn_vcpu *is_single_destination(struct acrn_vm *vm, const struct msi_info *info)
{
	uint64_t vdmask;
	uint16_t vid;
	struct acrn_vcpu *vcpu = NULL;

	vlapic_calc_dest(vm, &vdmask, false, (uint32_t)(info->addr.bits.dest_field),
		(bool)(info->addr.bits.dest_mode == MSI_ADDR_DESTMODE_PHYS),
		(bool)(info->data.bits.delivery_mode == MSI_DATA_DELMODE_LOPRI));

	/* Can only post fixed and Lowpri IRQs */
	if ((info->data.bits.delivery_mode == MSI_DATA_DELMODE_FIXED)
		|| (info->data.bits.delivery_mode == MSI_DATA_DELMODE_LOPRI)) {
		vid = ffs64(vdmask);

		/* Can only post single-destination IRQs */
		if (vdmask == (1UL << vid)) {
			vcpu = vcpu_from_vid(vm, vid);
		}
	}

	return vcpu;
}

static uint32_t calculate_logical_dest_mask(uint64_t pdmask)
{
	uint32_t dest_mask = 0UL;
	uint64_t pcpu_mask = pdmask;
	uint16_t pcpu_id;

	pcpu_id = ffs64(pcpu_mask);
	while (pcpu_id < MAX_PCPU_NUM) {
		bitmap_clear_nolock(pcpu_id, &pcpu_mask);
		dest_mask |= per_cpu(lapic_ldr, pcpu_id);
		pcpu_id = ffs64(pcpu_mask);
	}
	return dest_mask;
}

/*
 * pid_paddr = 0: invalid address, indicate that remapped mode shall be used
 *
 * pid_paddr != 0: physical address of posted interrupt descriptor, indicate
 * that posted mode shall be used
 */
static void build_physical_msi(struct ptintr *intr, struct msi_info *vmsi,
		uint32_t vector, uint64_t pid_paddr, uint16_t irte_idx)
{
	struct acrn_vm *vm = intr->vm;
	struct msi_info *pmsi = &intr->pmsi;
	uint64_t vdmask, pdmask;
	uint32_t dest, delmode, dest_mask;
	int32_t ret;
	bool phys;
	union dmar_ir_entry irte;
	union irte_index ir_index;
	struct intr_source intr_src;

	/* get physical destination cpu mask */
	dest = vmsi->addr.bits.dest_field;
	phys = (vmsi->addr.bits.dest_mode == MSI_ADDR_DESTMODE_PHYS);

	vlapic_calc_dest(vm, &vdmask, false, dest, phys, false);
	pdmask = vcpumask2pcpumask(vm, vdmask);

	/* get physical delivery mode */
	delmode = vmsi->data.bits.delivery_mode;
	if ((delmode != MSI_DATA_DELMODE_FIXED) && (delmode != MSI_DATA_DELMODE_LOPRI)) {
		delmode = MSI_DATA_DELMODE_LOPRI;
	}

	dest_mask = calculate_logical_dest_mask(pdmask);

	irte.value.lo_64 = 0UL;
	irte.value.hi_64 = 0UL;
	irte.bits.remap.vector = vector;
	irte.bits.remap.delivery_mode = delmode;
	irte.bits.remap.dest_mode = MSI_ADDR_DESTMODE_LOGICAL;
	irte.bits.remap.rh = MSI_ADDR_RH;
	irte.bits.remap.dest = dest_mask;

	intr_src.is_msi = true;
	intr_src.pid_paddr = pid_paddr;
	intr_src.src.msi.value = intr->phys_sid.msi_id.bdf;
	ret = dmar_assign_irte(&intr_src, &irte, irte_idx, &ir_index.index);

	if (ret == 0) {
		/*
		 * Update the MSI interrupt source to point to the IRTE
		 * SHV is set to 0 as ACRN disables MMC (Multi-Message Capable
		 * for MSI devices.
		 */
		pmsi->data.full = 0U;
		pmsi->addr.full = 0UL;

		intr->irte_idx = ir_index.index;
		if (ir_index.index != INVALID_IRTE_ID) {
			pmsi->addr.ir_bits.intr_index_high = ir_index.bits.index_high;
			pmsi->addr.ir_bits.shv = 0U;
			pmsi->addr.ir_bits.intr_format = 0x1U;
			pmsi->addr.ir_bits.intr_index_low = ir_index.bits.index_low;
			pmsi->addr.ir_bits.constant = 0xFEEU;
		}
	} else {
		/*
		 * In case there is no corresponding IOMMU, for example, if the
		 * IOMMU is ignored, pass the MSI info in Compatibility Format
		 */
		pmsi->data = vmsi->data;
		pmsi->data.bits.delivery_mode = delmode;
		pmsi->data.bits.vector = vector;

		pmsi->addr = vmsi->addr;
		pmsi->addr.bits.dest_field = dest_mask;
		pmsi->addr.bits.rh = MSI_ADDR_RH;
		pmsi->addr.bits.dest_mode = MSI_ADDR_DESTMODE_LOGICAL;
	}
	dev_dbg(DBG_LEVEL_PTINTR, "MSI %s addr:data = 0x%lx:%x(V) -> 0x%lx:%x(P)",
		(pmsi->addr.ir_bits.intr_format != 0U) ?
		"Remappable Format" : "Compatibility Format",
		vmsi->addr.full, vmsi->data.full,
		pmsi->addr.full, pmsi->data.full);
}

static union ioapic_rte build_physical_rte(struct acrn_vm *vm, struct ptintr *intr)
{
	union ioapic_rte rte;
	uint32_t phys_irq = ptirq_get_irq(intr->irq);
	union source_id *virt_sid = &intr->virt_sid;
	union irte_index ir_index;
	union dmar_ir_entry irte;
	struct intr_source intr_src;
	int32_t ret;

	if (virt_sid->intx_id.ctlr == INTX_CTLR_IOAPIC) {
		uint64_t vdmask, pdmask;
		uint32_t dest, delmode, dest_mask, vector;
		union ioapic_rte virt_rte;
		bool phys;

		vioapic_get_rte(vm, virt_sid->intx_id.gsi, &virt_rte);
		rte = virt_rte;

		/* init polarity & pin state */
		if (rte.bits.intr_polarity == IOAPIC_RTE_INTPOL_ALO) {
			if (ptirq_get_polarity(intr->irq) == 0U) {
				vioapic_set_irqline_nolock(vm, virt_sid->intx_id.gsi, GSI_SET_HIGH);
			}
			ptirq_set_polarity(intr->irq, 1U);
		} else {
			if (ptirq_get_polarity(intr->irq) == 1U) {
				vioapic_set_irqline_nolock(vm, virt_sid->intx_id.gsi, GSI_SET_LOW);
			}
			ptirq_set_polarity(intr->irq, 0U);
		}

		/* physical destination cpu mask */
		phys = (virt_rte.bits.dest_mode == IOAPIC_RTE_DESTMODE_PHY);
		dest = (uint32_t)virt_rte.bits.dest_field;
		vlapic_calc_dest(vm, &vdmask, false, dest, phys, false);
		pdmask = vcpumask2pcpumask(vm, vdmask);

		/* physical delivery mode */
		delmode = virt_rte.bits.delivery_mode;
		if ((delmode != IOAPIC_RTE_DELMODE_FIXED) &&
			(delmode != IOAPIC_RTE_DELMODE_LOPRI)) {
			delmode = IOAPIC_RTE_DELMODE_LOPRI;
		}

		/* update physical delivery mode, dest mode(logical) & vector */
		vector = irq_to_vector(phys_irq);
		dest_mask = calculate_logical_dest_mask(pdmask);

		irte.value.lo_64 = 0UL;
		irte.value.hi_64 = 0UL;
		irte.bits.remap.vector = vector;
		irte.bits.remap.delivery_mode = delmode;
		irte.bits.remap.dest_mode = IOAPIC_RTE_DESTMODE_LOGICAL;
		irte.bits.remap.dest = dest_mask;
		irte.bits.remap.trigger_mode = rte.bits.trigger_mode;

		intr_src.is_msi = false;
		intr_src.pid_paddr = 0UL;
		intr_src.src.ioapic_id = ioapic_irq_to_ioapic_id(phys_irq);
		ret = dmar_assign_irte(&intr_src, &irte, INVALID_IRTE_ID, &ir_index.index);

		if (ret == 0) {
			intr->irte_idx = ir_index.index;
			if (ir_index.index != INVALID_IRTE_ID) {
				rte.ir_bits.vector = vector;
				rte.ir_bits.constant = 0U;
				rte.ir_bits.intr_index_high = ir_index.bits.index_high;
				rte.ir_bits.intr_format = 1U;
				rte.ir_bits.intr_index_low = ir_index.bits.index_low;
			} else {
				rte.bits.intr_mask = 1;
			}
		} else {
			rte.bits.dest_mode = IOAPIC_RTE_DESTMODE_LOGICAL;
			rte.bits.delivery_mode = delmode;
			rte.bits.vector = vector;
			rte.bits.dest_field = dest_mask;
		}

		dev_dbg(DBG_LEVEL_PTINTR, "IOAPIC RTE %s = 0x%x:%x(V) -> 0x%x:%x(P)",
			(rte.ir_bits.intr_format != 0U) ? "Remappable Format" : "Compatibility Format",
			virt_rte.u.hi_32, virt_rte.u.lo_32,
			rte.u.hi_32, rte.u.lo_32);
	} else {
		enum vpic_trigger trigger;
		union ioapic_rte phys_rte;

		/* just update trigger mode */
		ioapic_get_rte(phys_irq, &phys_rte);
		rte = phys_rte;
		rte.bits.trigger_mode = IOAPIC_RTE_TRGRMODE_EDGE;
		vpic_get_irqline_trigger_mode(vm_pic(vm), (uint32_t)virt_sid->intx_id.gsi, &trigger);
		if (trigger == LEVEL_TRIGGER) {
			rte.bits.trigger_mode = IOAPIC_RTE_TRGRMODE_LEVEL;
		}

		dev_dbg(DBG_LEVEL_PTINTR, "IOAPIC RTE %s = 0x%x:%x(P) -> 0x%x:%x(P)",
			(rte.ir_bits.intr_format != 0U) ? "Remappable Format" : "Compatibility Format",
			phys_rte.u.hi_32, phys_rte.u.lo_32,
			rte.u.hi_32, rte.u.lo_32);
	}

	return rte;
}

int32_t ptintr_add_intx_arch(struct acrn_vm *vm, union source_id *virt_sid)
{
	int32_t ret = -ENODEV;
	struct ptintr *intr = NULL;
	uint32_t virt_gsi = virt_sid->intx_id.gsi;
	enum intx_ctlr virt_ctlr = virt_sid->intx_id.ctlr;
	DEFINE_INTX_SID(alt_virt_sid, virt_gsi, virt_ctlr);

	/*
	 * virt pin could come from vpic master, vpic slave or vioapic
	 * while phys pin is always means for physical IOAPIC.
	 *
	 * For SOS(sos_vm), it adds the mapping entries at runtime, if the
	 * entry already be held by others, return error.
	 */
	if (is_sos_vm(vm) && (virt_gsi < NR_LEGACY_PIN)) {
		/*
		 * For sos_vm, there is chance of vpin source switch
		 * between vPIC & vIOAPIC for one legacy phys_pin.
		 *
		 * Here we check if there is already a mapping entry
		 * from the other vpin source for a legacy pin. If
		 * yes, then switching the vpin source is needed.
		 */
		alt_virt_sid.intx_id.ctlr = (virt_ctlr == INTX_CTLR_PIC) ?
			INTX_CTLR_IOAPIC : INTX_CTLR_PIC;

		intr = find_intr(PTDEV_INTR_INTX, &alt_virt_sid, vm);

		if (intr != NULL) {
			intr->virt_sid = *virt_sid;
			/* FIXME re-insert */
			ret = -EACCES;
			dev_dbg(DBG_LEVEL_PTINTR,
				"IOAPIC gsi=%hhu pirq=%u vgsi=%d switch from %s to %s for vm%d",
				intr->phys_sid.intx_id.gsi,
				ptirq_get_irq(intr->irq), intr->virt_sid.intx_id.gsi,
				(virt_ctlr == INTX_CTLR_IOAPIC) ? "vPIC" : "vIOAPIC",
				(virt_ctlr == INTX_CTLR_IOAPIC) ? "vIOPIC" : "vPIC",
				intr->vm->vm_id);
		}
	}

	return ret;
}

/*
 * Main entry for PCI device assignment with MSI and MSI-X
 * MSI can up to 8 vectors and MSI-X can up to 1024 Vectors
 * We use entry_nr to indicate coming vectors
 * entry_nr = 0 means first vector
 * user must provide bdf and entry_nr
 */
int32_t ptintr_remap_msix_arch(struct ptintr *intr, struct ptintr_remap_msix *args)
{
	struct acrn_vm *vm = intr->vm;
	struct msi_info *vmsi = args->info;
	union pci_bdf vbdf;
	bool is_ptirq = false;
	uint32_t ptirq_vr = 0;
	uint16_t irte_idx = args->irte_idx;
	int32_t ret = 0;

	/* build physical config MSI, update to intr->pmsi */
	if (is_lapic_pt_configured(vm)) {
		enum vm_vlapic_mode vlapic_mode = check_vm_vlapic_mode(vm);

		if (vlapic_mode == VM_VLAPIC_X2APIC) {
			/*
			 * All the vCPUs are in x2APIC mode and LAPIC is Pass-through
			 * Use guest vector to program the interrupt source
			 */
			build_physical_msi(intr, vmsi,
					(uint32_t)vmsi->data.bits.vector, 0UL, irte_idx);
		} else if (vlapic_mode == VM_VLAPIC_XAPIC) {
			/*
			 * All the vCPUs are in xAPIC mode and LAPIC is emulated
			 * Use host vector to program the interrupt source
			 */
			is_ptirq = true;
			ptirq_vr = irq_to_vector(ptirq_get_irq(intr->irq));
			build_physical_msi(intr, vmsi, ptirq_vr, 0UL, irte_idx);
		} else if (vlapic_mode == VM_VLAPIC_TRANSITION) {
			/*
			 * vCPUs are in middle of transition, so do not program interrupt source
			 * TODO: Devices programmed during transistion do not work after transition
			 * as device is not programmed with interrupt info. Need to implement a
			 * method to get interrupts working after transition.
			 */
			ret = -EFAULT;
		} else {
			/* Do nothing for VM_VLAPIC_DISABLED */
			ret = -EFAULT;
		}
	} else {
		struct acrn_vcpu *vcpu = is_single_destination(vm, vmsi);

		if (is_pi_capable(vm) && (vcpu != NULL)) {
			build_physical_msi(intr, vmsi,
					(uint32_t)vmsi->data.bits.vector,
					hva2hpa(get_pi_desc(vcpu)), irte_idx);
		} else {
			/*
			 * Go with remapped mode if we cannot handle it in posted mode
			 */
			is_ptirq = true;
			ptirq_vr = irq_to_vector(ptirq_get_irq(intr->irq));
			build_physical_msi(intr, vmsi, ptirq_vr, 0UL, irte_idx);
		}
	}

	if (ret == 0) {
		vbdf.value = intr->virt_sid.msi_id.bdf;
		dev_dbg(DBG_LEVEL_PTINTR, "PCI %x:%x.%x MSI VR[%d] 0x%x->0x%x assigned to vm%u",
			vbdf.bits.b, vbdf.bits.d, vbdf.bits.f, intr->virt_sid.msi_id.entry_nr,
			vmsi->data.bits.vector,
			is_ptirq ? ptirq_vr : (uint32_t)vmsi->data.bits.vector,
			vm->vm_id);
	}

	return ret;
}

static void activate_physical_ioapic(struct ptintr *intr)
{
	struct acrn_vm *vm = intr->vm;
	union ioapic_rte rte;
	uint32_t phys_irq = ptirq_get_irq(intr->irq);
	uint64_t intr_mask;
	bool is_lvl_trigger = false;

	/* disable interrupt */
	ioapic_gsi_mask_irq(phys_irq);

	/* build physical IOAPIC RTE */
	rte = build_physical_rte(vm, intr);
	intr_mask = rte.bits.intr_mask;

	/* update irq trigger mode according to info in guest */
	if (rte.bits.trigger_mode == IOAPIC_RTE_TRGRMODE_LEVEL) {
		is_lvl_trigger = true;
	}
	set_irq_trigger_mode(phys_irq, is_lvl_trigger);

	/* set rte entry when masked */
	rte.bits.intr_mask = IOAPIC_RTE_MASK_SET;
	ioapic_set_rte(phys_irq, rte);

	if (intr_mask == IOAPIC_RTE_MASK_CLR) {
		ioapic_gsi_unmask_irq(phys_irq);
	}
}

int32_t ptintr_remap_intx_arch(struct ptintr *intr, __unused struct ptintr_remap_intx *args)
{
	activate_physical_ioapic(intr);
	return 0;
}

static void remove_remapping(const struct ptintr *intr)
{
	uint32_t phys_irq = ptirq_get_irq(intr->irq);
	struct intr_source intr_src;

	if (intr->intr_type == PTDEV_INTR_MSI) {
		intr_src.is_msi = true;
		intr_src.src.msi.value = intr->phys_sid.msi_id.bdf;
	} else {
		intr_src.is_msi = false;
		intr_src.src.ioapic_id = ioapic_irq_to_ioapic_id(phys_irq);
	}

	dmar_free_irte(&intr_src, intr->irte_idx);
}

void ptintr_remove_msix_arch(struct ptintr *intr)
{
	remove_remapping(intr);
}

void ptintr_remove_intx_arch(struct ptintr *intr)
{
	/* disable interrupt */
	ioapic_gsi_mask_irq(ptirq_get_irq(intr->irq));
	remove_remapping(intr);
}

void ptintr_init_arch(struct ptintr *(*find)(uint32_t, const union source_id *,
			const struct acrn_vm *))
{
	if (get_pcpu_id() == BSP_CPU_ID) {
		find_intr = find;
	}
}
