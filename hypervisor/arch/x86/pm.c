/*
 * Copyright (C) <2018> Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <acrn_common.h>
#include <x86/default_acpi_info.h>
#include <platform_acpi_info.h>
#include <x86/per_cpu.h>
#include <x86/io.h>
#include <x86/msr.h>
#include <x86/pgtable.h>
#include <x86/pm.h>
#include <x86/cpu_caps.h>
#include <x86/board.h>
#include <x86/trampoline.h>
#include <x86/vmx.h>
#include <console.h>
#include <x86/ioapic.h>
#include <x86/vtd.h>
#include <x86/lapic.h>
#include <udelay.h>
#include <cycles.h>

struct cpu_context cpu_ctx;

static struct cpu_state_info cpu_pm_state_info;

/* The table includes cpu px info of Intel A3960 SoC */
static const struct cpu_px_data px_a3960[17] = {
	{0x960UL, 0UL, 0xAUL, 0xAUL, 0x1800UL, 0x1800UL}, /* P0 */
	{0x8FCUL, 0UL, 0xAUL, 0xAUL, 0x1700UL, 0x1700UL}, /* P1 */
	{0x898UL, 0UL, 0xAUL, 0xAUL, 0x1600UL, 0x1600UL}, /* P2 */
	{0x834UL, 0UL, 0xAUL, 0xAUL, 0x1500UL, 0x1500UL}, /* P3 */
	{0x7D0UL, 0UL, 0xAUL, 0xAUL, 0x1400UL, 0x1400UL}, /* P4 */
	{0x76CUL, 0UL, 0xAUL, 0xAUL, 0x1300UL, 0x1300UL}, /* P5 */
	{0x708UL, 0UL, 0xAUL, 0xAUL, 0x1200UL, 0x1200UL}, /* P6 */
	{0x6A4UL, 0UL, 0xAUL, 0xAUL, 0x1100UL, 0x1100UL}, /* P7 */
	{0x640UL, 0UL, 0xAUL, 0xAUL, 0x1000UL, 0x1000UL}, /* P8 */
	{0x5DCUL, 0UL, 0xAUL, 0xAUL, 0x0F00UL, 0x0F00UL}, /* P9 */
	{0x578UL, 0UL, 0xAUL, 0xAUL, 0x0E00UL, 0x0E00UL}, /* P10 */
	{0x514UL, 0UL, 0xAUL, 0xAUL, 0x0D00UL, 0x0D00UL}, /* P11 */
	{0x4B0UL, 0UL, 0xAUL, 0xAUL, 0x0C00UL, 0x0C00UL}, /* P12 */
	{0x44CUL, 0UL, 0xAUL, 0xAUL, 0x0B00UL, 0x0B00UL}, /* P13 */
	{0x3E8UL, 0UL, 0xAUL, 0xAUL, 0x0A00UL, 0x0A00UL}, /* P14 */
	{0x384UL, 0UL, 0xAUL, 0xAUL, 0x0900UL, 0x0900UL}, /* P15 */
	{0x320UL, 0UL, 0xAUL, 0xAUL, 0x0800UL, 0x0800UL}  /* P16 */
};

/* The table includes cpu cx info of Intel Broxton SoC such as A39x0, J3455, N3350 */
static const struct cpu_cx_data cx_bxt[3] = {
	{{SPACE_FFixedHW,  0x0U, 0U, 0U,     0UL}, 0x1U, 0x1U, 0x3E8UL}, /* C1 */
	{{SPACE_SYSTEM_IO, 0x8U, 0U, 0U, 0x415UL}, 0x2U, 0x32U, 0x0AUL}, /* C2 */
	{{SPACE_SYSTEM_IO, 0x8U, 0U, 0U, 0x419UL}, 0x3U, 0x96U, 0x0AUL}  /* C3 */
};

/* The table includes cpu px info of Intel A3950 SoC */
static const struct cpu_px_data px_a3950[13] = {
	{0x7D0UL, 0UL, 0xAUL, 0xAUL, 0x1400UL, 0x1400UL}, /* P0 */
	{0x76CUL, 0UL, 0xAUL, 0xAUL, 0x1300UL, 0x1300UL}, /* P1 */
	{0x708UL, 0UL, 0xAUL, 0xAUL, 0x1200UL, 0x1200UL}, /* P2 */
	{0x6A4UL, 0UL, 0xAUL, 0xAUL, 0x1100UL, 0x1100UL}, /* P3 */
	{0x640UL, 0UL, 0xAUL, 0xAUL, 0x1000UL, 0x1000UL}, /* P4 */
	{0x5DCUL, 0UL, 0xAUL, 0xAUL, 0x0F00UL, 0x0F00UL}, /* P5 */
	{0x578UL, 0UL, 0xAUL, 0xAUL, 0x0E00UL, 0x0E00UL}, /* P6 */
	{0x514UL, 0UL, 0xAUL, 0xAUL, 0x0D00UL, 0x0D00UL}, /* P7 */
	{0x4B0UL, 0UL, 0xAUL, 0xAUL, 0x0C00UL, 0x0C00UL}, /* P8 */
	{0x44CUL, 0UL, 0xAUL, 0xAUL, 0x0B00UL, 0x0B00UL}, /* P9 */
	{0x3E8UL, 0UL, 0xAUL, 0xAUL, 0x0A00UL, 0x0A00UL}, /* P10 */
	{0x384UL, 0UL, 0xAUL, 0xAUL, 0x0900UL, 0x0900UL}, /* P11 */
	{0x320UL, 0UL, 0xAUL, 0xAUL, 0x0800UL, 0x0800UL}  /* P12 */
};

/* The table includes cpu px info of Intel J3455 SoC */
static const struct cpu_px_data px_j3455[9] = {
	{0x5DDUL, 0UL, 0xAUL, 0xAUL, 0x1700UL, 0x1700UL}, /* P0 */
	{0x5DCUL, 0UL, 0xAUL, 0xAUL, 0x0F00UL, 0x0F00UL}, /* P1 */
	{0x578UL, 0UL, 0xAUL, 0xAUL, 0x0E00UL, 0x0E00UL}, /* P2 */
	{0x514UL, 0UL, 0xAUL, 0xAUL, 0x0D00UL, 0x0D00UL}, /* P3 */
	{0x4B0UL, 0UL, 0xAUL, 0xAUL, 0x0C00UL, 0x0C00UL}, /* P4 */
	{0x44CUL, 0UL, 0xAUL, 0xAUL, 0x0B00UL, 0x0B00UL}, /* P5 */
	{0x3E8UL, 0UL, 0xAUL, 0xAUL, 0x0A00UL, 0x0A00UL}, /* P6 */
	{0x384UL, 0UL, 0xAUL, 0xAUL, 0x0900UL, 0x0900UL}, /* P7 */
	{0x320UL, 0UL, 0xAUL, 0xAUL, 0x0800UL, 0x0800UL}  /* P8 */
};

/* The table includes cpu px info of Intel N3350 SoC */
static const struct cpu_px_data px_n3350[5] = {
	{0x44DUL, 0UL, 0xAUL, 0xAUL, 0x1800UL, 0x1800UL}, /* P0 */
	{0x44CUL, 0UL, 0xAUL, 0xAUL, 0x0B00UL, 0x0B00UL}, /* P1 */
	{0x3E8UL, 0UL, 0xAUL, 0xAUL, 0x0A00UL, 0x0A00UL}, /* P2 */
	{0x384UL, 0UL, 0xAUL, 0xAUL, 0x0900UL, 0x0900UL}, /* P3 */
	{0x320UL, 0UL, 0xAUL, 0xAUL, 0x0800UL, 0x0800UL}  /* P4 */
};

/* The table includes cpu cx info of Intel i7-8650U SoC */
static const struct cpu_px_data px_i78650[16] = {
	{0x835UL, 0x0UL, 0xAUL, 0xAUL, 0x2A00UL, 0x2A00UL}, /* P0 */
	{0x834UL, 0x0UL, 0xAUL, 0xAUL, 0x1500UL, 0x1500UL}, /* P1 */
	{0x76CUL, 0x0UL, 0xAUL, 0xAUL, 0x1300UL, 0x1300UL}, /* P2 */
	{0x708UL, 0x0UL, 0xAUL, 0xAUL, 0x1200UL, 0x1200UL}, /* P3 */
	{0x6A4UL, 0x0UL, 0xAUL, 0xAUL, 0x1100UL, 0x1100UL}, /* P4 */
	{0x640UL, 0x0UL, 0xAUL, 0xAUL, 0x1000UL, 0x1000UL}, /* P5 */
	{0x5DCUL, 0x0UL, 0xAUL, 0xAUL, 0x0F00UL, 0x0F00UL}, /* P6 */
	{0x578UL, 0x0UL, 0xAUL, 0xAUL, 0x0E00UL, 0x0E00UL}, /* P7 */
	{0x4B0UL, 0x0UL, 0xAUL, 0xAUL, 0x0C00UL, 0x0C00UL}, /* P8 */
	{0x44CUL, 0x0UL, 0xAUL, 0xAUL, 0x0B00UL, 0x0B00UL}, /* P9 */
	{0x3E8UL, 0x0UL, 0xAUL, 0xAUL, 0x0A00UL, 0x0A00UL}, /* P10 */
	{0x320UL, 0x0UL, 0xAUL, 0xAUL, 0x0800UL, 0x0800UL}, /* P11 */
	{0x2BCUL, 0x0UL, 0xAUL, 0xAUL, 0x0700UL, 0x0700UL}, /* P12 */
	{0x258UL, 0x0UL, 0xAUL, 0xAUL, 0x0600UL, 0x0600UL}, /* P13 */
	{0x1F4UL, 0x0UL, 0xAUL, 0xAUL, 0x0500UL, 0x0500UL}, /* P14 */
	{0x190UL, 0x0UL, 0xAUL, 0xAUL, 0x0400UL, 0x0400UL}  /* P15 */
};

/* The table includes cpu cx info of Intel i7-8650U SoC */
static const struct cpu_cx_data cx_i78650[3] = {
	{{SPACE_FFixedHW,  0x0U, 0U, 0U,      0UL}, 0x1U, 0x1U,   0UL}, /* C1 */
	{{SPACE_SYSTEM_IO, 0x8U, 0U, 0U, 0x1816UL}, 0x2U, 0x97U,  0UL}, /* C2 */
	{{SPACE_SYSTEM_IO, 0x8U, 0U, 0U, 0x1819UL}, 0x3U, 0x40AU, 0UL}  /* C3 */
};

static const struct cpu_state_table cpu_state_tbl[5] = {
	{"Intel(R) Atom(TM) Processor A3960 @ 1.90GHz",
		{(uint8_t)ARRAY_SIZE(px_a3960), px_a3960,
		 (uint8_t)ARRAY_SIZE(cx_bxt), cx_bxt}
	},
	{"Intel(R) Atom(TM) Processor A3950 @ 1.60GHz",
		{(uint8_t)ARRAY_SIZE(px_a3950), px_a3950,
		 (uint8_t)ARRAY_SIZE(cx_bxt), cx_bxt}
	},
	{"Intel(R) Celeron(R) CPU J3455 @ 1.50GHz",
		{(uint8_t)ARRAY_SIZE(px_j3455), px_j3455,
		 (uint8_t)ARRAY_SIZE(cx_bxt), cx_bxt}
	},
	{"Intel(R) Celeron(R) CPU N3350 @ 1.10GHz",
		{(uint8_t)ARRAY_SIZE(px_n3350), px_n3350,
		 (uint8_t)ARRAY_SIZE(cx_bxt), cx_bxt}
	},
	{"Intel(R) Core(TM) i7-8650U CPU @ 1.90GHz",
		{(uint8_t)ARRAY_SIZE(px_i78650), px_i78650,
		 (uint8_t)ARRAY_SIZE(cx_i78650), cx_i78650}
	}
};

static int32_t get_state_tbl_idx(const char *cpuname)
{
	int32_t i;
	int32_t count = ARRAY_SIZE(cpu_state_tbl);
	int32_t ret = -1;

	if (cpuname != NULL) {
		for (i = 0; i < count; i++) {
			if (strcmp((cpu_state_tbl[i].model_name), cpuname) == 0) {
				ret = i;
				break;
			}
		}
	}

	return ret;
}

struct cpu_state_info *get_cpu_pm_state_info(void)
{
	return &cpu_pm_state_info;
}

static void load_cpu_state_info(const struct cpu_state_info *state_info)
{
	if ((state_info->px_cnt != 0U) && (state_info->px_data != NULL)) {
		if (state_info->px_cnt > MAX_PSTATE) {
			cpu_pm_state_info.px_cnt = MAX_PSTATE;
		} else {
			cpu_pm_state_info.px_cnt = state_info->px_cnt;
		}

		cpu_pm_state_info.px_data = state_info->px_data;
	}

	if ((state_info->cx_cnt != 0U) && (state_info->cx_data != NULL)) {
		if (state_info->cx_cnt > MAX_CX_ENTRY) {
			cpu_pm_state_info.cx_cnt = MAX_CX_ENTRY;
		} else {
			cpu_pm_state_info.cx_cnt = state_info->cx_cnt;
		}

		cpu_pm_state_info.cx_data = state_info->cx_data;
	}
}

static void load_pcpu_state_data(void)
{
	int32_t tbl_idx;
	const struct cpu_state_info *state_info = NULL;

	(void)memset(&cpu_pm_state_info, 0U, sizeof(struct cpu_state_info));

	tbl_idx = get_state_tbl_idx(pcpu_model_name());

	if (tbl_idx >= 0) {
		/* The cpu state table is found at global cpu_state_tbl[]. */
		state_info = &(cpu_state_tbl + tbl_idx)->state_info;
	} else {
		/* check whether board.c has a valid cpu state table which generated by offline tool */
		if (strcmp((board_cpu_state_tbl.model_name), pcpu_model_name()) == 0) {
			state_info = &board_cpu_state_tbl.state_info;
		}
	}
	if (state_info != NULL) {
		load_cpu_state_info(state_info);
	}
}

/* The values in this structure should come from host ACPI table */
static struct pm_s_state_data host_pm_s_state = {
	.pm1a_evt = {
		.space_id = PM1A_EVT_SPACE_ID,
		.bit_width = PM1A_EVT_BIT_WIDTH,
		.bit_offset = PM1A_EVT_BIT_OFFSET,
		.access_size = PM1A_EVT_ACCESS_SIZE,
		.address = PM1A_EVT_ADDRESS
	},
	.pm1b_evt = {
		.space_id = PM1B_EVT_SPACE_ID,
		.bit_width = PM1B_EVT_BIT_WIDTH,
		.bit_offset = PM1B_EVT_BIT_OFFSET,
		.access_size = PM1B_EVT_ACCESS_SIZE,
		.address = PM1B_EVT_ADDRESS
	},
	.pm1a_cnt = {
		.space_id = PM1A_CNT_SPACE_ID,
		.bit_width = PM1A_CNT_BIT_WIDTH,
		.bit_offset = PM1A_CNT_BIT_OFFSET,
		.access_size = PM1A_CNT_ACCESS_SIZE,
		.address = PM1A_CNT_ADDRESS
	},
	.pm1b_cnt = {
		.space_id = PM1B_CNT_SPACE_ID,
		.bit_width = PM1B_CNT_BIT_WIDTH,
		.bit_offset = PM1B_CNT_BIT_OFFSET,
		.access_size = PM1B_CNT_ACCESS_SIZE,
		.address = PM1B_CNT_ADDRESS
	},
	.s3_pkg = {
		.val_pm1a = S3_PKG_VAL_PM1A,
		.val_pm1b = S3_PKG_VAL_PM1B,
		.reserved = S3_PKG_RESERVED
	},
	.s5_pkg = {
		.val_pm1a = S5_PKG_VAL_PM1A,
		.val_pm1b = S5_PKG_VAL_PM1B,
		.reserved = S5_PKG_RESERVED
	},
	.wake_vector_32 = (uint32_t *)WAKE_VECTOR_32,
	.wake_vector_64 = (uint64_t *)WAKE_VECTOR_64
};

/* host reset register defined in ACPI */
static struct acpi_reset_reg host_reset_reg = {
	.reg = {
		.space_id = RESET_REGISTER_SPACE_ID,
		.bit_width = RESET_REGISTER_BIT_WIDTH,
		.bit_offset = RESET_REGISTER_BIT_OFFSET,
		.access_size = RESET_REGISTER_ACCESS_SIZE,
		.address = RESET_REGISTER_ADDRESS,
	},
	.val = RESET_REGISTER_VALUE
};

struct pm_s_state_data *get_host_sstate_data(void)
{
	return &host_pm_s_state;
}

struct acpi_reset_reg *get_host_reset_reg_data(void)
{
	return &host_reset_reg;
}

void restore_msrs(void)
{
#ifdef STACK_PROTECTOR
	struct stack_canary *psc = &get_cpu_var(stk_canary);

	msr_write(MSR_IA32_FS_BASE, (uint64_t)psc);
#endif
}

static void acpi_gas_write(const struct acpi_generic_address *gas, uint32_t val)
{
	uint16_t val16 = (uint16_t)val;

	if (gas->space_id == SPACE_SYSTEM_MEMORY) {
		mmio_write16(val16, hpa2hva(gas->address));
	} else {
		pio_write16(val16, (uint16_t)gas->address);
	}
}

static uint32_t acpi_gas_read(const struct acpi_generic_address *gas)
{
	uint32_t ret = 0U;

	if (gas->space_id == SPACE_SYSTEM_MEMORY) {
		ret = mmio_read16(hpa2hva(gas->address));
	} else {
		ret = pio_read16((uint16_t)gas->address);
	}

	return ret;
}

/* This function supports enter S3 or S5 according to the value given to pm1a_cnt_val and pm1b_cnt_val */
void do_acpi_sx(const struct pm_s_state_data *sstate_data, uint32_t pm1a_cnt_val, uint32_t pm1b_cnt_val)
{
	uint32_t s1, s2;

	acpi_gas_write(&(sstate_data->pm1a_cnt), pm1a_cnt_val);

	if (sstate_data->pm1b_cnt.address != 0U) {
		acpi_gas_write(&(sstate_data->pm1b_cnt), pm1b_cnt_val);
	}

	do {
		/* polling PM1 state register to detect wether
		 * the Sx state enter is interrupted by wakeup event.
		 */
		s1 = acpi_gas_read(&(sstate_data->pm1a_evt));

		if (sstate_data->pm1b_evt.address != 0U) {
			s2 = acpi_gas_read(&(sstate_data->pm1b_evt));
			s1 |= s2;
		}

		/* According to ACPI spec 4.8.3.1.1 PM1 state register, the bit
		 * WAK_STS(bit 15) is set if system will transition to working
		 * state.
		 */
	} while ((s1 & (1U << BIT_WAK_STS)) == 0U);
}

static uint32_t system_pm1a_cnt_val, system_pm1b_cnt_val;
void save_s5_reg_val(uint32_t pm1a_cnt_val, uint32_t pm1b_cnt_val)
{
	system_pm1a_cnt_val = pm1a_cnt_val;
	system_pm1b_cnt_val = pm1b_cnt_val;
}

void shutdown_system(void)
{
	struct pm_s_state_data *sx_data = get_host_sstate_data();
	do_acpi_sx(sx_data, system_pm1a_cnt_val, system_pm1b_cnt_val);
}

static void suspend_tsc(__unused void *data)
{
	per_cpu(tsc_suspend, get_pcpu_id()) = get_cpu_cycles();
}

static void resume_tsc(__unused void *data)
{
	msr_write(MSR_IA32_TIME_STAMP_COUNTER, per_cpu(tsc_suspend, get_pcpu_id()));
}

void host_enter_s3(const struct pm_s_state_data *sstate_data, uint32_t pm1a_cnt_val, uint32_t pm1b_cnt_val)
{
	uint64_t pmain_entry_saved;

	stac();

	/* set ACRN wakeup vec instead */
	*(sstate_data->wake_vector_32) = (uint32_t)get_trampoline_start16_paddr();

	clac();

	/* Save TSC on all PCPU */
	smp_call_function(get_active_pcpu_bitmap(), suspend_tsc, NULL);

	/* offline all APs */
	stop_pcpus();

	stac();
	/* Save default main entry and we will restore it after
	 * back from S3. So the AP online could jmp to correct
	 * main entry.
	 */
	pmain_entry_saved = read_trampoline_sym(main_entry);

	/* Set the main entry for resume from S3 state */
	write_trampoline_sym(main_entry, (uint64_t)restore_s3_context);
	clac();

	CPU_IRQ_DISABLE();
	vmx_off();

	suspend_console();
	suspend_ioapic();
	suspend_iommu();
	suspend_lapic();

	asm_enter_s3(sstate_data, pm1a_cnt_val, pm1b_cnt_val);

	resume_lapic();
	resume_iommu();
	resume_ioapic();

	vmx_on();
	CPU_IRQ_ENABLE();

	/* restore the default main entry */
	stac();
	write_trampoline_sym(main_entry, pmain_entry_saved);
	clac();

	/* online all APs again */
	if (!start_pcpus(AP_MASK)) {
		panic("Failed to start all APs!");
	}

	/* Restore TSC on all PCPU
	 * Caution: There should no timer setup before TSC resumed.
	 */
	smp_call_function(get_active_pcpu_bitmap(), resume_tsc, NULL);

	/* console must be resumed after TSC restored since it will setup timer base on TSC */
	resume_console();
}

void reset_host(void)
{
	struct acpi_generic_address *gas = &(host_reset_reg.reg);


	/* TODO: gracefully shut down all guests before doing host reset. */

	/*
	 * Assumption:
	 * The platform we are running must support at least one of reset method:
	 *   - ACPI reset
	 *   - 0xcf9 reset
	 *
	 * UEFI more likely sets the reset value as 0x6 (not 0xe) for 0xcf9 port.
	 * This asserts PLTRST# to reset devices on the platform, but not the
	 * SLP_S3#/4#/5# signals, which power down the systems. This might not be
	 * enough for us.
	 */
	if ((gas->space_id == SPACE_SYSTEM_IO) &&
		(gas->bit_width == 8U) && (gas->bit_offset == 0U) &&
		(gas->address != 0U) && (gas->address != 0xcf9U)) {
		pio_write8(host_reset_reg.val, (uint16_t)host_reset_reg.reg.address);
	} else {
		/* making sure bit 2 (RST_CPU) is '0', when the reset command is issued. */
		pio_write8(0x2U, 0xcf9U);
		udelay(50U);
		pio_write8(0xeU, 0xcf9U);
	}

	pr_fatal("%s(): can't reset host.", __func__);
	while (1) {
		asm_pause();
	}
}

void init_pm(void)
{
	load_pcpu_state_data();
}
