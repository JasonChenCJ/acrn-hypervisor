/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <hypervisor.h>
#include <acpi.h>
#include <schedule.h>
#include <version.h>
#include <trampoline.h>
#include <e820.h>
#include <cpu_caps.h>

spinlock_t trampoline_spinlock = {
	.head = 0U,
	.tail = 0U
};

struct per_cpu_region per_cpu_data[CONFIG_MAX_PCPU_NUM] __aligned(PAGE_SIZE);
uint16_t phys_cpu_num = 0U;
static uint64_t pcpu_sync = 0UL;
static uint16_t up_count = 0U;
static uint64_t startup_paddr = 0UL;

/* physical cpu active bitmap, support up to 64 cpus */
uint64_t pcpu_active_bitmap = 0UL;

static void cpu_xsave_init(void);
static void set_current_cpu_id(uint16_t pcpu_id);
static void print_hv_banner(void);
static uint16_t get_cpu_id_from_lapic_id(uint32_t lapic_id);
static uint64_t start_tsc __attribute__((__section__(".bss_noinit")));

static void init_percpu_lapic_id(void)
{
	uint16_t i;
	uint16_t pcpu_num = 0U;
	uint32_t lapic_id_array[CONFIG_MAX_PCPU_NUM];

	/* Save all lapic_id detected via parse_mdt in lapic_id_array */
	pcpu_num = parse_madt(lapic_id_array);
	if (pcpu_num == 0U) {
		/* failed to get the physcial cpu number */
		ASSERT(false);
	}

	phys_cpu_num = pcpu_num;

	for (i = 0U; (i < pcpu_num) && (i < CONFIG_MAX_PCPU_NUM); i++) {
		per_cpu(lapic_id, i) = lapic_id_array[i];
	}
}

static void cpu_set_current_state(uint16_t pcpu_id, enum pcpu_boot_state state)
{
	/* Check if state is initializing */
	if (state == PCPU_STATE_INITIALIZING) {
		/* Increment CPU up count */
		atomic_inc16(&up_count);

		/* Save this CPU's logical ID to the TSC AUX MSR */
		set_current_cpu_id(pcpu_id);
	}

	/* If cpu is dead, decrement CPU up count */
	if (state == PCPU_STATE_DEAD) {
		atomic_dec16(&up_count);
	}

	/* Set state for the specified CPU */
	per_cpu(boot_state, pcpu_id) = state;
}

#ifdef STACK_PROTECTOR
static uint64_t get_random_value(void)
{
	uint64_t random = 0UL;

	asm volatile ("1: rdrand %%rax\n"
			"jnc 1b\n"
			"mov %%rax, %0\n"
			: "=r"(random)
			:
			:"%rax");
	return random;
}

static void set_fs_base(void)
{
	struct stack_canary *psc = &get_cpu_var(stk_canary);

	psc->canary = get_random_value();
	msr_write(MSR_IA32_FS_BASE, (uint64_t)psc);
}
#endif

void init_cpu_pre(uint16_t pcpu_id)
{
	if (pcpu_id == BOOT_CPU_ID) {
		start_tsc = rdtsc();

		/* Clear BSS */
		(void)memset(&ld_bss_start, 0U,
				(size_t)(&ld_bss_end - &ld_bss_start));

		/* Get CPU capabilities thru CPUID, including the physical address bit
		 * limit which is required for initializing paging.
		 */
		get_cpu_capabilities();

		get_cpu_name();

		load_cpu_state_data();

		/* Initialize the hypervisor paging */
		init_e820();
		init_paging();

		if (!cpu_has_cap(X86_FEATURE_X2APIC)) {
			panic("x2APIC is not present!");
		}

		cpu_cap_detect();

		early_init_lapic();

		init_percpu_lapic_id();
	} else {
		/* Switch this CPU to use the same page tables set-up by the
		 * primary/boot CPU
		 */
		enable_paging();

		early_init_lapic();

		pcpu_id = get_cpu_id_from_lapic_id(get_cur_lapic_id());
		if (pcpu_id >= CONFIG_MAX_PCPU_NUM) {
			panic("Invalid pCPU ID!");
		}
	}

	bitmap_set_nolock(pcpu_id, &pcpu_active_bitmap);

	/* Set state for this CPU to initializing */
	cpu_set_current_state(pcpu_id, PCPU_STATE_INITIALIZING);
}

void init_cpu_post(uint16_t pcpu_id)
{
#ifdef STACK_PROTECTOR
	set_fs_base();
#endif
	load_gdtr_and_tr();

	enable_smep();

	enable_smap();

	/* Make sure rdtsc is enabled */
	check_tsc();

	cpu_xsave_init();

	if (pcpu_id == BOOT_CPU_ID) {
		/* Print Hypervisor Banner */
		print_hv_banner();

		/* Calibrate TSC Frequency */
		calibrate_tsc();

		pr_acrnlog("HV version %s-%s-%s %s (daily tag:%s) build by %s, start time %lluus",
				HV_FULL_VERSION,
				HV_BUILD_TIME, HV_BUILD_VERSION, HV_BUILD_TYPE,
				HV_DAILY_TAG,
				HV_BUILD_USER, ticks_to_us(start_tsc));

		pr_acrnlog("API version %u.%u",
				HV_API_MAJOR_VERSION, HV_API_MINOR_VERSION);

		pr_acrnlog("Detect processor: %s", boot_cpu_data.model_name);

		pr_dbg("Core %hu is up", BOOT_CPU_ID);

		if (hardware_detect_support() != 0) {
			panic("hardware not support!");
		}

		/* Warn for security feature not ready */
		if (!check_cpu_security_config()) {
			pr_fatal("SECURITY WARNING!!!!!!");
			pr_fatal("Please apply the latest CPU uCode patch!");
		}

		/* Initialize interrupts */
		interrupt_init(BOOT_CPU_ID);

		timer_init();
		setup_notification();
		setup_posted_intr_notification();

		/* Start all secondary cores */
		startup_paddr = prepare_trampoline();
		start_cpus();

		ASSERT(get_cpu_id() == BOOT_CPU_ID, "");
	} else {
		pr_dbg("Core %hu is up", pcpu_id);

		/* Initialize secondary processor interrupts. */
		interrupt_init(pcpu_id);

		timer_init();

		/* Wait for boot processor to signal all secondary cores to continue */
		wait_sync_change(&pcpu_sync, 0UL);
	}
}

static uint16_t get_cpu_id_from_lapic_id(uint32_t lapic_id)
{
	uint16_t i;

	for (i = 0U; (i < phys_cpu_num) && (i < CONFIG_MAX_PCPU_NUM); i++) {
		if (per_cpu(lapic_id, i) == lapic_id) {
			return i;
		}
	}

	return INVALID_CPU_ID;
}

/*
 * Start all secondary CPUs.
 */
void start_cpus(void)
{
	uint32_t timeout;
	uint16_t expected_up;

	/* secondary cpu start up will wait for pcpu_sync -> 0UL */
	atomic_store64(&pcpu_sync, 1UL);

	/* Set flag showing number of CPUs expected to be up to all
	 * cpus
	 */
	expected_up = phys_cpu_num;

	/* Broadcast IPIs to all other CPUs,
	 * In this case, INTR_CPU_STARTUP_ALL_EX_SELF decides broadcasting
	 * IPIs, INVALID_CPU_ID is parameter value to destination pcpu_id.
	 */
	send_startup_ipi(INTR_CPU_STARTUP_ALL_EX_SELF,
			INVALID_CPU_ID, startup_paddr);

	/* Wait until global count is equal to expected CPU up count or
	 * configured time-out has expired
	 */
	timeout = (uint32_t)CONFIG_CPU_UP_TIMEOUT * 1000U;
	while ((atomic_load16(&up_count) != expected_up) && (timeout != 0U)) {
		/* Delay 10us */
		udelay(10U);

		/* Decrement timeout value */
		timeout -= 10U;
	}

	/* Check to see if all expected CPUs are actually up */
	if (atomic_load16(&up_count) != expected_up) {
		/* Print error */
		pr_fatal("Secondary CPUs failed to come up");

		/* Error condition - loop endlessly for now */
		do {
		} while (1);
	}

	/* Trigger event to allow secondary CPUs to continue */
	atomic_store64(&pcpu_sync, 0UL);
}

void stop_cpus(void)
{
	uint16_t pcpu_id, expected_up;
	uint32_t timeout;

	timeout = (uint32_t)CONFIG_CPU_UP_TIMEOUT * 1000U;
	for (pcpu_id = 0U; pcpu_id < phys_cpu_num; pcpu_id++) {
		if (get_cpu_id() == pcpu_id) {	/* avoid offline itself */
			continue;
		}

		make_pcpu_offline(pcpu_id);
	}

	expected_up = 1U;
	while ((atomic_load16(&up_count) != expected_up) && (timeout != 0U)) {
		/* Delay 10us */
		udelay(10U);

		/* Decrement timeout value */
		timeout -= 10U;
	}

	if (atomic_load16(&up_count) != expected_up) {
		pr_fatal("Can't make all APs offline");

		/* if partial APs is down, it's not easy to recover
		 * per our current implementation (need make up dead
		 * APs one by one), just print error mesage and dead
		 * loop here.
		 *
		 * FIXME:
		 * We need to refine here to handle the AP offline
		 * failure for release/debug version. Ideally, we should
		 * define how to handle general unrecoverable error and
		 * follow it here.
		 */
		do {
		} while (1);
	}
}

void cpu_do_idle(void)
{
	__asm __volatile("pause" ::: "memory");
}

void cpu_dead(uint16_t pcpu_id)
{
	/* For debug purposes, using a stack variable in the while loop enables
	 * us to modify the value using a JTAG probe and resume if needed.
	 */
	int32_t halt = 1;

	if (bitmap_test_and_clear_lock(pcpu_id, &pcpu_active_bitmap)) {
		/* clean up native stuff */
		vmx_off();
		cache_flush_invalidate_all();

		/* Set state to show CPU is dead */
		cpu_set_current_state(pcpu_id, PCPU_STATE_DEAD);

		/* Halt the CPU */
		do {
			hlt_cpu();
		} while (halt != 0);
	} else {
		pr_err("pcpu%hu already dead", pcpu_id);
	}
}

static void set_current_cpu_id(uint16_t pcpu_id)
{
	/* Write TSC AUX register */
	msr_write(MSR_IA32_TSC_AUX, (uint64_t) pcpu_id);
}

static void print_hv_banner(void)
{
	const char *boot_msg = "ACRN Hypervisor\n\r";

	/* Print the boot message */
	printf(boot_msg);
}

/* wait until *sync == wake_sync */
void wait_sync_change(uint64_t *sync, uint64_t wake_sync)
{
	if (get_monitor_cap()) {
		/* Wait for the event to be set using monitor/mwait */
		asm volatile ("1: cmpq      %%rbx,(%%rax)\n"
			      "   je        2f\n"
			      "   monitor\n"
			      "   mwait\n"
			      "   jmp       1b\n"
			      "2:\n"
			      :
			      : "a" (sync), "d"(0), "c"(0),
			      "b"(wake_sync)
			      : "cc");
	} else {
		/* Wait for the event to be set using pause */
		asm volatile ("1: cmpq      %%rbx,(%%rax)\n"
			      "   je        2f\n"
			      "   pause\n"
			      "   jmp       1b\n"
			      "2:\n"
			      :
			      : "a" (sync), "d"(0), "c"(0),
			      "b"(wake_sync)
			      : "cc");
	}
}

static void cpu_xsave_init(void)
{
	uint64_t val64;

	if (cpu_has_cap(X86_FEATURE_XSAVE)) {
		CPU_CR_READ(cr4, &val64);
		val64 |= CR4_OSXSAVE;
		CPU_CR_WRITE(cr4, val64);

		if (get_cpu_id() == BOOT_CPU_ID) {
			uint32_t ecx, unused;
			cpuid(CPUID_FEATURES, &unused, &unused, &ecx, &unused);

			/* if set, update it */
			if ((ecx & CPUID_ECX_OSXSAVE) != 0U) {
				boot_cpu_data.cpuid_leaves[FEAT_1_ECX] |=
						CPUID_ECX_OSXSAVE;
			}
		}
	}
}
