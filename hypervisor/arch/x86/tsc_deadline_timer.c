/*
 * Copyright (C) 2020 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <x86/msr.h>
#include <softirq.h>
#include <x86/page.h>
#include <x86/apicreg.h>
#include <util.h>
#include <x86/lib/spinlock.h>
#include <x86/cpu.h>
#include <irq.h>
#include <x86/irq.h>
#include <trace.h>

/* run in interrupt context */
static void timer_expired_handler(__unused uint32_t irq, __unused void *data)
{
	fire_softirq(SOFTIRQ_TIMER);
}

void set_hw_timeout(uint64_t timeout)
{
	msr_write(MSR_IA32_TSC_DEADLINE, timeout);
}

void init_hw_timer(void)
{
	uint32_t val;
	int32_t retval = 0;

	if (get_pcpu_id() == BSP_CPU_ID) {
		retval = request_irq(TIMER_IRQ, (irq_action_t)timer_expired_handler, NULL, IRQF_NONE);
		if (retval < 0) {
			pr_err("Timer setup failed");
		}
	}

	if (retval >= 0) {
		val = TIMER_VECTOR;
		val |= APIC_LVTT_TM_TSCDLT; /* TSC deadline and unmask */
		msr_write(MSR_IA32_EXT_APIC_LVT_TIMER, val);
		cpu_memory_barrier();

		/* disarm timer */
		msr_write(MSR_IA32_TSC_DEADLINE, 0UL);
	}
}
