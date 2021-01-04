/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <util.h>
#include <x86/lib/spinlock.h>
#include <x86/cpu.h>
#include <interrupt/include/irq.h>
#include <interrupt/sub/x86/include/irq.h>
#include <dump.h>
#include <logmsg.h>

void dispatch_exception(struct intr_excp_ctx *ctx)
{
	uint16_t pcpu_id = get_pcpu_id();

	/* Dump exception context */
	dump_exception(ctx, pcpu_id);

	/* Halt the CPU */
	cpu_dead();

}
