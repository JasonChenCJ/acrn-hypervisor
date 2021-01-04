/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <console.h>
#include <version.h>
#include <shell.h>
#include <ptintr.h>
#include <logmsg.h>
#include <cycles.h>
#include <acpi.h>
#include <uart16550.h>
#include <vpci.h>
#include <ivshmem.h>
#include <x86/init.h>
#include <x86/cpu_caps.h>
#include <x86/per_cpu.h>
#include <x86/lapic.h>
#include <x86/e820.h>
#include <x86/page.h>
#include <x86/mmu.h>
#include <x86/ioapic.h>
#include <x86/vmx.h>
#include <x86/vtd.h>
#include <x86/rdt.h>
#include <x86/sgx.h>
#include <x86/rtcm.h>
#include <x86/guest/vm.h>
#include <x86/seed.h>
#include <x86/tsc.h>
#include <x86/boot/ld_sym.h>
#include <multiboot.h>

/* boot_regs store the multiboot info magic and address, defined in
   arch/x86/boot/cpu_primary.S.
   */
extern uint32_t boot_regs[2];

/* Push sp magic to top of stack for call trace */
#define SWITCH_TO(rsp, to)                                              \
{                                                                       \
	asm volatile ("movq %0, %%rsp\n"                                \
			"pushq %1\n"                                    \
			"jmpq *%2\n"                                    \
			 :                                              \
			 : "r"(rsp), "rm"(SP_BOTTOM_MAGIC), "a"(to));   \
}

static uint64_t start_cycle __attribute__((__section__(".bss_noinit")));

/*TODO: move into debug module */
static void init_debug_pre(void)
{
	/* Initialize console */
	console_init();

	/* Enable logging */
	init_logmsg(CONFIG_LOG_DESTINATION);
}

/*TODO: move into debug module */
static void init_debug_post(uint16_t pcpu_id)
{
	if (pcpu_id == BSP_CPU_ID) {
		/* Initialize the shell */
		shell_init();
		console_setup_timer();
	}

	profiling_setup();
}

/*TODO: move into guest-vcpu module */
static void init_guest_mode(uint16_t pcpu_id)
{
	vmx_on();

	launch_vms(pcpu_id);
}

static void print_hv_banner(void)
{
	const char *boot_msg = "ACRN Hypervisor\n\r";

	/* Print the boot message */
	printf(boot_msg);
}

static void enable_ac_for_splitlock(void)
{
#ifndef CONFIG_ENFORCE_TURNOFF_AC
	uint64_t test_ctl;

	if (has_core_cap(1U << 5U)) {
		test_ctl = msr_read(MSR_TEST_CTL);
		test_ctl |= (1U << 29U);
		msr_write(MSR_TEST_CTL, test_ctl);
	}
#endif /*CONFIG_ENFORCE_TURNOFF_AC*/
}

static void init_keylocker(void)
{
	uint64_t val64;

	/* Enable host CR4.KL if keylocker feature is supported */
	if (pcpu_has_cap(X86_FEATURE_KEYLOCKER)) {
		CPU_CR_READ(cr4, &val64);
		val64 |= CR4_KL;
		CPU_CR_WRITE(cr4, val64);
	}
}

static void init_pcpu_comm_post(void)
{
	uint16_t pcpu_id;

	pcpu_id = get_pcpu_id();

#ifdef STACK_PROTECTOR
	set_fs_base();
#endif
	load_gdtr_and_tr();

	enable_ac_for_splitlock();

	init_pcpu_xsave();

	if (pcpu_id == BSP_CPU_ID) {
		/* Print Hypervisor Banner */
		print_hv_banner();

		/* Calibrate TSC Frequency */
		calibrate_tsc();

		pr_acrnlog("HV version %s-%s-%s %s (daily tag:%s) %s@%s build by %s%s, start time %luus",
				HV_FULL_VERSION,
				HV_BUILD_TIME, HV_BUILD_VERSION, HV_BUILD_TYPE,
				HV_DAILY_TAG, HV_BUILD_SCENARIO, HV_BUILD_BOARD,
				HV_BUILD_USER, HV_CONFIG_TOOL, cycles_to_us(start_cycle));

		pr_acrnlog("API version %u.%u",	HV_API_MAJOR_VERSION, HV_API_MINOR_VERSION);

		pr_acrnlog("Detect processor: %s", pcpu_model_name());

		pr_dbg("Core %hu is up", BSP_CPU_ID);

		/* Warn for security feature not ready */
		if (!check_cpu_security_cap()) {
			pr_fatal("SECURITY WARNING!!!!!!");
			pr_fatal("Please apply the latest CPU uCode patch!");
		}

		/* Initialize interrupts */
		init_interrupt(BSP_CPU_ID);

		timer_init();
		setup_notification();
		setup_pi_notification();

		if (init_iommu() != 0) {
			panic("failed to initialize iommu!");
		}

#ifdef CONFIG_IVSHMEM_ENABLED
		init_ivshmem_shared_memory();
#endif
		init_pci_pdev_list(); /* init_iommu must come before this */
		ptintr_init();

		if (init_sgx() != 0) {
			panic("failed to initialize sgx!");
		}

		/*
		 * Reserve memory from platform E820 for EPT 4K pages for all VMs
		 */
#ifdef CONFIG_LAST_LEVEL_EPT_AT_BOOT
		reserve_buffer_for_ept_pages();
#endif
		/* Start all secondary cores */
		if (!start_pcpus(AP_MASK)) {
			panic("Failed to start all secondary cores!");
		}

		ASSERT(get_pcpu_id() == BSP_CPU_ID, "");

		init_software_sram(true);
	} else {
		pr_dbg("Core %hu is up", pcpu_id);

		pr_warn("Skipping VM configuration check which should be done before building HV binary.");

		init_software_sram(false);

		/* Initialize secondary processor interrupts. */
		init_interrupt(pcpu_id);

		timer_init();
		ptintr_init();

		/* Wait for boot processor to signal all secondary cores to continue */
		wait_all_pcpus_run();
	}

	init_sched(pcpu_id);

#ifdef CONFIG_RDT_ENABLED
	setup_clos(pcpu_id);
#endif

	enable_smep();
	enable_smap();
	init_keylocker();

	init_debug_post(pcpu_id);
	init_guest_mode(pcpu_id);
	run_idle_thread();
}

static void init_misc(void)
{
	init_cr0_cr4_flexible_bits();
	if (!sanitize_cr0_cr4_pattern()) {
		panic("%s Sanitize pattern of CR0 or CR4 failed.\n", __func__);
	}
}

static bool init_percpu_lapic_id(void)
{
	uint16_t i, pcpu_num;
	uint32_t lapic_id_array[MAX_PCPU_NUM];
	bool success = false;

	/* Save all lapic_id detected via parse_mdt in lapic_id_array */
	pcpu_num = parse_madt(lapic_id_array);

	if ((pcpu_num != 0U) && (pcpu_num <= MAX_PCPU_NUM)) {
		set_pcpu_nums(pcpu_num);
		for (i = 0U; i < pcpu_num; i++) {
			per_cpu(lapic_id, i) = lapic_id_array[i];
		}
		success = true;
	}

	return success;
}

static uint16_t get_pcpu_id_from_lapic_id(uint32_t lapic_id)
{
	uint16_t i;
	uint16_t pcpu_id = INVALID_CPU_ID;

	for (i = 0U; i < get_pcpu_nums(); i++) {
		if (per_cpu(lapic_id, i) == lapic_id) {
			pcpu_id = i;
			break;
		}
	}

	return pcpu_id;
}

/* NOTE: this function is using temp stack, and after SWITCH_TO(runtime_sp, to)
 * it will switch to runtime stack.
 */
void init_primary_pcpu(void)
{
	uint64_t rsp;
	int32_t	ret;

	/* Clear BSS */
	(void)memset(&ld_bss_start, 0U, (size_t)(&ld_bss_end - &ld_bss_start));

	init_acrn_multiboot_info(boot_regs[0], boot_regs[1]);

	init_debug_pre();

	if (sanitize_acrn_multiboot_info(boot_regs[0], boot_regs[1]) != 0) {
		panic("Multiboot info error!");
	}

	start_cycle = get_cpu_cycles();

	/* Get CPU capabilities thru CPUID, including the physical address bit
	 * limit which is required for initializing paging.
	 */
	init_pcpu_capabilities();

	if (detect_hardware_support() != 0) {
		panic("hardware not support!");
	}

	load_pcpu_state_data();

	/* Initialize the hypervisor paging */
	init_e820();
	init_paging();

	/*
	 * Need update uart_base_address here for vaddr2paddr mapping may changed
	 * WARNNING: DO NOT CALL PRINTF BETWEEN ENABLE PAGING IN init_paging AND HERE!
	 */
	uart16550_init(false);

	early_init_lapic();

	init_acpi();
#ifdef CONFIG_ACPI_PARSE_ENABLED
	ret = acpi_fixup();
	if (ret != 0) {
		panic("failed to parse/fix up ACPI table!");
	}
#endif

	if (!init_percpu_lapic_id()) {
		panic("failed to init_percpu_lapic_id!");
	}

	ret = init_ioapic_id_info();
	if (ret != 0) {
		panic("System IOAPIC info is incorrect!");
	}

#ifdef CONFIG_RDT_ENABLED
	init_rdt_info();
#endif

	/* NOTE: this must call after MMCONFIG is parsed in acpi_fixup() and before APs are INIT.
	 * We only support platform with MMIO based CFG space access.
	 * IO port access only support in debug version.
	 */
	pci_switch_to_mmio_cfg_ops();

	init_pcpu_state(BSP_CPU_ID);

	init_seed();
	init_misc();

	/* Switch to run-time stack */
	rsp = (uint64_t)(&get_cpu_var(stack)[CONFIG_STACK_SIZE - 1]);
	rsp &= ~(CPU_STACK_ALIGN - 1UL);
	SWITCH_TO(rsp, init_pcpu_comm_post);
}

void init_secondary_pcpu(void)
{
	uint16_t pcpu_id;

	/* Switch this CPU to use the same page tables set-up by the
	 * primary/boot CPU
	 */
	enable_paging();

	early_init_lapic();

	pcpu_id = get_pcpu_id_from_lapic_id(get_cur_lapic_id());
	if (pcpu_id >= MAX_PCPU_NUM) {
		panic("Invalid pCPU ID!");
	}

	init_pcpu_state(pcpu_id);

	init_pcpu_comm_post();
}
