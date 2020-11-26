/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <logmsg.h>
#include <boot.h>
#include <udelay.h>
#include <board_info.h>
#include <x86/lib/bits.h>
#include <x86/per_cpu.h>
#include <x86/cpuid.h>
#include <x86/lapic.h>
#include <x86/cpu_caps.h>
#include <x86/irq.h>
#include <x86/mmu.h>
#include <x86/vmx.h>
#include <x86/msr.h>
#include <x86/page.h>
#include <x86/trampoline.h>

#define CPU_UP_TIMEOUT		100U /* millisecond */
#define CPU_DOWN_TIMEOUT	100U /* millisecond */

struct per_cpu_region per_cpu_data[MAX_PCPU_NUM] __aligned(PAGE_SIZE);
static uint16_t phys_cpu_num = 0U;
static uint64_t pcpu_sync = 0UL;
static uint64_t startup_paddr = 0UL;

/* physical cpu active bitmap, support up to 64 cpus */
static volatile uint64_t pcpu_active_bitmap = 0UL;

static void set_current_pcpu_id(uint16_t pcpu_id);

static void pcpu_set_current_state(uint16_t pcpu_id, enum pcpu_boot_state state)
{
	/* Check if state is initializing */
	if (state == PCPU_STATE_INITIALIZING) {

		/* Save this CPU's logical ID to the TSC AUX MSR */
		set_current_pcpu_id(pcpu_id);
	}

	/* Set state for the specified CPU */
	per_cpu(boot_state, pcpu_id) = state;
}

/**
 * @pre num <= MAX_PCPU_NUM
 */
void set_pcpu_nums(uint16_t num)
{
	phys_cpu_num = num;
}

/*
 * @post return <= MAX_PCPU_NUM
 */
uint16_t get_pcpu_nums(void)
{
	return phys_cpu_num;
}

static void set_active_pcpu_bitmap(uint16_t pcpu_id)
{
	bitmap_set_lock(pcpu_id, &pcpu_active_bitmap);

}

bool is_pcpu_active(uint16_t pcpu_id)
{
	return bitmap_test(pcpu_id, &pcpu_active_bitmap);
}

uint64_t get_active_pcpu_bitmap(void)
{
	return pcpu_active_bitmap;
}

void init_pcpu_state(uint16_t pcpu_id)
{
	set_active_pcpu_bitmap(pcpu_id);

	/* Set state for this CPU to initializing */
	pcpu_set_current_state(pcpu_id, PCPU_STATE_INITIALIZING);
}

static void start_pcpu(uint16_t pcpu_id)
{
	uint32_t timeout;

	if (startup_paddr == 0UL) {
		startup_paddr = prepare_trampoline();
	}

	/* Update the stack for pcpu */
	stac();
	write_trampoline_stack_sym(pcpu_id);
	clac();

	send_startup_ipi(pcpu_id, startup_paddr);

	/* Wait until the pcpu with pcpu_id is running and set the active bitmap or
	 * configured time-out has expired
	 */
	timeout = CPU_UP_TIMEOUT * 1000U;
	while (!is_pcpu_active(pcpu_id) && (timeout != 0U)) {
		/* Delay 10us */
		udelay(10U);

		/* Decrement timeout value */
		timeout -= 10U;
	}

	/* Check to see if expected CPU is actually up */
	if (!is_pcpu_active(pcpu_id)) {
		pr_fatal("Secondary CPU%hu failed to come up", pcpu_id);
		pcpu_set_current_state(pcpu_id, PCPU_STATE_DEAD);
	}
}


/**
 * @brief Start all cpus if the bit is set in mask except itself
 *
 * @param[in] mask bits mask of cpus which should be started
 *
 * @return true if all cpus set in mask are started
 * @return false if there are any cpus set in mask aren't started
 */
bool start_pcpus(uint64_t mask)
{
	uint16_t i;
	uint16_t pcpu_id = get_pcpu_id();
	uint64_t expected_start_mask = mask;

	/* secondary cpu start up will wait for pcpu_sync -> 0UL */
	pcpu_sync = 1UL;
	cpu_write_memory_barrier();

	i = ffs64(expected_start_mask);
	while (i != INVALID_BIT_INDEX) {
		bitmap_clear_nolock(i, &expected_start_mask);

		if (pcpu_id == i) {
			continue; /* Avoid start itself */
		}

		start_pcpu(i);
		i = ffs64(expected_start_mask);
	}

	/* Trigger event to allow secondary CPUs to continue */
	pcpu_sync = 0UL;

	return ((pcpu_active_bitmap & mask) == mask);
}

void wait_all_pcpus_run(void)
{
	wait_sync_change(&pcpu_sync, 0UL);
}

void make_pcpu_offline(uint16_t pcpu_id)
{
	bitmap_set_lock(NEED_OFFLINE, &per_cpu(pcpu_flag, pcpu_id));
	if (get_pcpu_id() != pcpu_id) {
		send_single_ipi(pcpu_id, NOTIFY_VCPU_VECTOR);
	}
}

bool need_offline(uint16_t pcpu_id)
{
	return bitmap_test_and_clear_lock(NEED_OFFLINE, &per_cpu(pcpu_flag, pcpu_id));
}

void wait_pcpus_offline(uint64_t mask)
{
	uint32_t timeout;

	timeout = CPU_DOWN_TIMEOUT * 1000U;
	while (((pcpu_active_bitmap & mask) != 0UL) && (timeout != 0U)) {
		udelay(10U);
		timeout -= 10U;
	}
}

void stop_pcpus(void)
{
	uint16_t pcpu_id;
	uint64_t mask = 0UL;

	for (pcpu_id = 0U; pcpu_id < phys_cpu_num; pcpu_id++) {
		if (get_pcpu_id() == pcpu_id) {	/* avoid offline itself */
			continue;
		}

		bitmap_set_nolock(pcpu_id, &mask);
		make_pcpu_offline(pcpu_id);
	}

	/**
	 * Timeout never occurs here:
	 *   If target cpu received a NMI and panic, it has called cpu_dead and make_pcpu_offline success.
	 *   If target cpu is running, an IPI will be delivered to it and then call cpu_dead.
	 */
	wait_pcpus_offline(mask);
}

void cpu_do_idle(void)
{
	asm_pause();
}

/**
 * only run on current pcpu
 */
void cpu_dead(void)
{
	/* For debug purposes, using a stack variable in the while loop enables
	 * us to modify the value using a JTAG probe and resume if needed.
	 */
	int32_t halt = 1;
	uint16_t pcpu_id = get_pcpu_id();

	deinit_sched(pcpu_id);
	if (bitmap_test(pcpu_id, &pcpu_active_bitmap)) {
		/* clean up native stuff */
		vmx_off();
		/* TODO: a cpu dead can't effect the RTVM which use pSRAM */
		cache_flush_invalidate_all();

		/* Set state to show CPU is dead */
		pcpu_set_current_state(pcpu_id, PCPU_STATE_DEAD);
		bitmap_clear_lock(pcpu_id, &pcpu_active_bitmap);

		/* Halt the CPU */
		do {
			asm_hlt();
		} while (halt != 0);
	} else {
		pr_err("pcpu%hu already dead", pcpu_id);
	}
}

static void set_current_pcpu_id(uint16_t pcpu_id)
{
	/* Write TSC AUX register */
	msr_write(MSR_IA32_TSC_AUX, (uint64_t) pcpu_id);
}

static
inline void asm_monitor(volatile const uint64_t *addr, uint64_t ecx, uint64_t edx)
{
	asm volatile("monitor\n" : : "a" (addr), "c" (ecx), "d" (edx));
}

static
inline void asm_mwait(uint64_t eax, uint64_t ecx)
{
	asm volatile("mwait\n" : : "a" (eax), "c" (ecx));
}

/* wait until *sync == wake_sync */
void wait_sync_change(volatile const uint64_t *sync, uint64_t wake_sync)
{
	if (has_monitor_cap()) {
		/* Wait for the event to be set using monitor/mwait */
		while ((*sync) != wake_sync) {
			asm_monitor(sync, 0UL, 0UL);
			if ((*sync) != wake_sync) {
				asm_mwait(0UL, 0UL);
			}
		}
	} else {
		while ((*sync) != wake_sync) {
			asm_pause();
		}
	}
}

void init_pcpu_xsave(void)
{
	uint64_t val64;
	uint64_t xcr0, xss;
	uint32_t eax, ebx, ecx, edx;

	CPU_CR_READ(cr4, &val64);
	val64 |= CR4_OSXSAVE;
	CPU_CR_WRITE(cr4, val64);

	if (get_pcpu_id() == BSP_CPU_ID) {
		cpuid_subleaf(CPUID_FEATURES, 0x0U, &eax, &ebx, &ecx, &edx);

		/* if set, update it */
		if ((ecx & CPUID_ECX_OSXSAVE) != 0U) {
			pcpu_set_cap(X86_FEATURE_OSXSAVE);

			/* set xcr0 and xss with the componets bitmap get from cpuid */
			cpuid_subleaf(CPUID_XSAVE_FEATURES, 0U, &eax, &ebx, &ecx, &edx);
			xcr0 = ((uint64_t)edx << 32U) + eax;
			cpuid_subleaf(CPUID_XSAVE_FEATURES, 1U, &eax, &ebx, &ecx, &edx);
			xss = ((uint64_t)edx << 32U) + ecx;
			write_xcr(0, xcr0);
			msr_write(MSR_IA32_XSS, xss);

			/* get xsave area size, containing all the state components
			 * corresponding to bits currently set in XCR0 | IA32_XSS */
			cpuid_subleaf(CPUID_XSAVE_FEATURES, 1U, &eax, &ebx, &ecx, &edx);
			if (ebx > XSAVE_STATE_AREA_SIZE) {
				panic("XSAVE area (%d bytes) exceeds the pre-allocated 4K region\n", ebx);
			}
		}
	}
}

static void smpcall_write_msr_func(void *data)
{
	struct msr_data_struct *msr = (struct msr_data_struct *)data;

	msr_write(msr->msr_index, msr->write_val);
}

void msr_write_pcpu(uint32_t msr_index, uint64_t value64, uint16_t pcpu_id)
{
	struct msr_data_struct msr = {0};
	uint64_t mask = 0UL;

	if (pcpu_id == get_pcpu_id()) {
		msr_write(msr_index, value64);
	} else {
		msr.msr_index = msr_index;
		msr.write_val = value64;
		bitmap_set_nolock(pcpu_id, &mask);
		smp_call_function(mask, smpcall_write_msr_func, &msr);
	}
}

static void smpcall_read_msr_func(void *data)
{
	struct msr_data_struct *msr = (struct msr_data_struct *)data;

	msr->read_val = msr_read(msr->msr_index);
}

uint64_t msr_read_pcpu(uint32_t msr_index, uint16_t pcpu_id)
{
	struct msr_data_struct msr = {0};
	uint64_t mask = 0UL;
	uint64_t ret = 0;

	if (pcpu_id == get_pcpu_id()) {
		ret = msr_read(msr_index);
	} else {
		msr.msr_index = msr_index;
		bitmap_set_nolock(pcpu_id, &mask);
		smp_call_function(mask, smpcall_read_msr_func, &msr);
		ret = msr.read_val;
	}

	return ret;
}
