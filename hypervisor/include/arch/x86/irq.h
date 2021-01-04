/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ARCH_IRQ_H
#define ARCH_IRQ_H

/**
 * @file arch/x86/irq.h
 *
 * @brief public APIs for arch IRQ
 */

#define NR_MAX_VECTOR		0xFFU
#define VECTOR_INVALID		(NR_MAX_VECTOR + 1U)

#define HYPERVISOR_CALLBACK_VHM_VECTOR	0xF3U

#define TIMER_IRQ		(NR_IRQS - 1U)
#define NOTIFY_VCPU_IRQ		(NR_IRQS - 2U)
#define PMI_IRQ			(NR_IRQS - 3U)

/* # of NR_STATIC_MAPPINGS_1 entries for timer, vcpu notify, and PMI */
#define NR_STATIC_MAPPINGS_1	3U

/*
 * The static IRQ/Vector mapping table in irq.c consists of the following entries:
 * # of NR_STATIC_MAPPINGS_1 entries for timer, vcpu notify, and PMI
 *
 * # of CONFIG_MAX_VM_NUM entries for posted interrupt notification, platform
 * specific but known at build time:
 * Allocate unique Activation Notification Vectors (ANV) for each vCPU that belongs
 * to the same pCPU, the ANVs need only be unique within each pCPU, not across all
 * vCPUs. The max numbers of vCPUs may be running on top of a pCPU is CONFIG_MAX_VM_NUM,
 * since ACRN does not support 2 vCPUs of same VM running on top of same pCPU.
 * This reduces # of pre-allocated ANVs for posted interrupts to CONFIG_MAX_VM_NUM,
 * and enables ACRN to avoid switching between active and wake-up vector values
 * in the posted interrupt descriptor on vCPU scheduling state changes.
 */
#define NR_STATIC_MAPPINGS	(NR_STATIC_MAPPINGS_1 + CONFIG_MAX_VM_NUM)

/* vectors range for dynamic allocation, usually for devices */
#define VECTOR_DYNAMIC_START	0x20U
#define VECTOR_DYNAMIC_END	0xDFU

/* vectors range for fixed vectors, usually for HV service */
#define VECTOR_FIXED_START	0xE0U
#define VECTOR_FIXED_END	0xFFU

#define TIMER_VECTOR		(VECTOR_FIXED_START)
#define NOTIFY_VCPU_VECTOR	(VECTOR_FIXED_START + 1U)
#define PMI_VECTOR		(VECTOR_FIXED_START + 2U)
/*
 * Starting vector for posted interrupts
 * # of CONFIG_MAX_VM_NUM (POSTED_INTR_VECTOR ~ (POSTED_INTR_VECTOR + CONFIG_MAX_VM_NUM - 1U))
 * consecutive vectors reserved for posted interrupts
 */
#define POSTED_INTR_VECTOR	(VECTOR_FIXED_START + NR_STATIC_MAPPINGS_1)

/*
 * Starting IRQ for posted interrupts
 * # of CONFIG_MAX_VM_NUM (POSTED_INTR_IRQ ~ (POSTED_INTR_IRQ + CONFIG_MAX_VM_NUM - 1U))
 * consecutive IRQs reserved for posted interrupts
 */
#define POSTED_INTR_IRQ	(NR_IRQS - NR_STATIC_MAPPINGS_1 - CONFIG_MAX_VM_NUM)

struct x86_irq_data {
	uint32_t vector;	/**< assigned vector */
#ifdef PROFILING_ON
	uint64_t ctx_rip;
	uint64_t ctx_rflags;
	uint64_t ctx_cs;
#endif
};

/**
 * @defgroup phys_int_ext_apis Physical Interrupt External Interfaces
 *
 * This is a group that includes Physical Interrupt External Interfaces.
 *
 * @{
 */

uint32_t alloc_irq_vector(uint32_t irq);

/**
 * @brief Get vector number of an interrupt from irq number
 *
 * @param[in]	irq	The irq_num to convert
 *
 * @return vector number
 */
uint32_t irq_to_vector(uint32_t irq);

/**
 * @brief Request an interrupt
 */
bool request_irq_arch(uint32_t irq);

/**
 * @brief Free an interrupt
 *
 * Free irq num and unregister the irq action.
 *
 * @param[in]	irq	irq_num to be freed
 */
void free_irq_arch(uint32_t irq);

void pre_irq_arch(const struct irq_desc *desc);

void eoi_irq_arch(const struct irq_desc *desc);

void post_irq_arch(const struct irq_desc *desc);

/**
 * @brief Dispatch interrupt
 *
 * To dispatch an interrupt, an action callback will be called if registered.
 *
 * @param ctx Pointer to interrupt exception context
 */
void dispatch_interrupt(const struct intr_excp_ctx *ctx);

bool irq_allocated_arch(struct irq_desc *desc);
void init_irq_descs_arch(struct irq_desc descs[]);

/**
 * @brief Initialize interrupt
 *
 * To do interrupt initialization for a cpu, will be called for each physical cpu.
 *
 * @param[in]	pcpu_id The id of physical cpu to initialize
 */
void init_interrupt_arch(uint16_t pcpu_id);

void setup_irqs_arch(void);

/**
 * @}
 */
/* End of phys_int_ext_apis */

/**
 * @}
 */
/* End of acrn_x86_irq */
#endif /* ARCH_IRQ_H */
