/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <rtl.h>
#include <list.h>
#include <bits.h>
#include <cpu.h>
#include <per_cpu.h>
#include <lapic.h>
#include <schedule.h>
#include <sprintf.h>
#include <errno.h>

#define CONFIG_TASK_PER_PCPU 3

static uint64_t pcpu_used_bitmap[CONFIG_TASK_PER_PCPU];

static spinlock_t task_lock = { .head = 0U, .tail = 0U };

void init_scheduler(void)
{
	struct sched_context *ctx;
	uint32_t i;
	uint16_t pcpu_nums = get_pcpu_nums();

	for (i = 0U; i < pcpu_nums; i++) {
		ctx = &per_cpu(sched_ctx, i);

		spinlock_init(&ctx->runqueue_lock);
		spinlock_init(&ctx->scheduler_lock);
		INIT_LIST_HEAD(&ctx->runqueue);
		ctx->flags = 0UL;
		ctx->curr_obj = NULL;
	}
}

void get_schedule_lock(uint16_t pcpu_id)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);
	spinlock_obtain(&ctx->scheduler_lock);
}

void release_schedule_lock(uint16_t pcpu_id)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);
	spinlock_release(&ctx->scheduler_lock);
}

static void pin_task(uint16_t pcpu_id, uint16_t task_id)
{
	if (task_id < CONFIG_TASK_PER_PCPU) {
		bitmap_set_nolock(pcpu_id, &pcpu_used_bitmap[task_id]);
	} else {
		uint16_t i;

		for (i = 0U; i < CONFIG_TASK_PER_PCPU; i++) {
			bitmap_set_nolock(pcpu_id, &pcpu_used_bitmap[i]);
		}
	}
}


/**
 * @pre task_rc != NULL
 */
int32_t allocate_task(struct sched_task_rc *task_rc)
{
	uint16_t pcpu_id = task_rc->pcpu_id;
	uint16_t task_id = task_rc->task_id;
	uint16_t i, pcpu_nums = get_pcpu_nums();
	int ret = 0;

	spinlock_obtain(&task_lock);
	if ((pcpu_id < pcpu_nums) && (task_id < CONFIG_TASK_PER_PCPU)) {
		if (bitmap_test(pcpu_id, &pcpu_used_bitmap[task_id]) != 0) {
			ret = -ENODEV;
		}
	} else if ((pcpu_id < pcpu_nums) && (task_id == TASK_ID_MONOPOLY)) {
		for (i = 0U; i < CONFIG_TASK_PER_PCPU; i++) {
			if (bitmap_test(pcpu_id, &pcpu_used_bitmap[i]) != 0) {
				ret = -ENODEV;
				break;
			}
		}
	} else if (pcpu_id < pcpu_nums) {
		for (task_id = 0U; task_id < CONFIG_TASK_PER_PCPU; task_id++) {
			if (bitmap_test(pcpu_id, &pcpu_used_bitmap[task_id]) == 0) {
				break;
			}
		}
		if (task_id == CONFIG_TASK_PER_PCPU) {
			ret = -ENODEV;
		}
	} else if (task_id == TASK_ID_MONOPOLY) {
		for (pcpu_id = 0U; pcpu_id < pcpu_nums; pcpu_id++) {
			for (i = 0U; i < CONFIG_TASK_PER_PCPU; i++) {
				if (bitmap_test(pcpu_id, &pcpu_used_bitmap[i]) != 0) {
					break;
				}
			}
			if (i == CONFIG_TASK_PER_PCPU) {
				break;
			}
		}
		if (pcpu_id == pcpu_nums) {
			ret = -ENODEV;
		}
	} else {
		for (task_id = 0U; task_id < CONFIG_TASK_PER_PCPU; task_id++) {
			for (pcpu_id = 0U; pcpu_id < pcpu_nums; pcpu_id++) {
				if (bitmap_test(pcpu_id, &pcpu_used_bitmap[task_id]) == 0) {
					break;
				}
			}
			if (pcpu_id < pcpu_nums) {
				break;
			}
		}
		if (task_id == CONFIG_TASK_PER_PCPU) {
			ret = -ENODEV;
		}
	}

	if (ret == 0) {
		pin_task(pcpu_id, task_id);
		task_rc->pcpu_id = pcpu_id;
		task_rc->task_id = task_id;
		pr_err("%s: pcpu_id %d, task_id 0x%x\n", __func__, pcpu_id, task_id);
	}

	spinlock_release(&task_lock);
	return ret;
}

/**
 * @pre task_rc != NULL
 */
void free_task(struct sched_task_rc *task_rc)
{
	uint16_t pcpu_id = task_rc->pcpu_id;
	uint16_t task_id = task_rc->task_id;

	spinlock_obtain(&task_lock);
	pr_err("%s: pcpu_id %d, task_id 0x%x\n", __func__, pcpu_id, task_id);
	if (task_id < CONFIG_TASK_PER_PCPU) {
		bitmap_clear_nolock(pcpu_id, &pcpu_used_bitmap[task_id]);
	} else {
		uint16_t i;

		for (i = 0U; i < CONFIG_TASK_PER_PCPU; i++) {
			bitmap_clear_nolock(pcpu_id, &pcpu_used_bitmap[i]);
		}
	}
	spinlock_release(&task_lock);
}

void add_to_cpu_runqueue(struct sched_object *obj, uint16_t pcpu_id)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);

	spinlock_obtain(&ctx->runqueue_lock);
	if (list_empty(&obj->run_list)) {
		list_add_tail(&obj->run_list, &ctx->runqueue);
	}
	spinlock_release(&ctx->runqueue_lock);
}

void remove_from_cpu_runqueue(struct sched_object *obj, uint16_t pcpu_id)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);

	spinlock_obtain(&ctx->runqueue_lock);
	list_del_init(&obj->run_list);
	spinlock_release(&ctx->runqueue_lock);
}

static struct sched_object *get_next_sched_obj(struct sched_context *ctx)
{
	struct sched_object *obj = NULL;

	spinlock_obtain(&ctx->runqueue_lock);
	if (!list_empty(&ctx->runqueue)) {
		obj = get_first_item(&ctx->runqueue, struct sched_object, run_list);
	} else {
		obj = &get_cpu_var(idle);
	}
	spinlock_release(&ctx->runqueue_lock);

	return obj;
}

/**
 * @pre delmode == DEL_MODE_IPI || delmode == DEL_MODE_INIT
 */
void make_reschedule_request(uint16_t pcpu_id, uint16_t delmode)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);

	bitmap_set_lock(NEED_RESCHEDULE, &ctx->flags);
	if (get_cpu_id() != pcpu_id) {
		switch (delmode) {
		case DEL_MODE_IPI:
			send_single_ipi(pcpu_id, VECTOR_NOTIFY_VCPU);
			break;
		case DEL_MODE_INIT:
			send_single_init(pcpu_id);
			break;
		default:
			ASSERT(false, "Unknown delivery mode %u for pCPU%u", delmode, pcpu_id);
			break;
		}
	}
}

bool need_reschedule(uint16_t pcpu_id)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);

	return bitmap_test(NEED_RESCHEDULE, &ctx->flags);
}

void make_pcpu_offline(uint16_t pcpu_id)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);

	bitmap_set_lock(NEED_OFFLINE, &ctx->flags);
	if (get_cpu_id() != pcpu_id) {
		send_single_ipi(pcpu_id, VECTOR_NOTIFY_VCPU);
	}
}

int32_t need_offline(uint16_t pcpu_id)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);

	return bitmap_test_and_clear_lock(NEED_OFFLINE, &ctx->flags);
}

struct sched_object *get_cur_sched_obj(uint16_t pcpu_id)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);
	struct sched_object *obj = NULL;

	get_schedule_lock(pcpu_id);
	if (ctx->curr_obj != &get_cpu_var(idle)) {
		obj = ctx->curr_obj;
	}
	release_schedule_lock(pcpu_id);

	return obj;
}

/**
 * @pre obj != NULL
 */
uint16_t pcpuid_from_sched_obj(const struct sched_object *obj)
{
	return obj->task_rc.pcpu_id;
}

static void prepare_switch(struct sched_object *prev, struct sched_object *next)
{
	if ((prev != NULL) && (prev->prepare_switch_out != NULL)) {
		prev->prepare_switch_out(prev);
	}

	/* update current object */
	get_cpu_var(sched_ctx).curr_obj = next;

	if ((next != NULL) && (next->prepare_switch_in != NULL)) {
		next->prepare_switch_in(next);
	}
}

void schedule(void)
{
	uint16_t pcpu_id = get_cpu_id();
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);
	struct sched_object *next = NULL;
	struct sched_object *prev = ctx->curr_obj;

	get_schedule_lock(pcpu_id);
	next = get_next_sched_obj(ctx);
	bitmap_clear_lock(NEED_RESCHEDULE, &ctx->flags);

	if (prev == next) {
		release_schedule_lock(pcpu_id);
	} else {
		prepare_switch(prev, next);
		release_schedule_lock(pcpu_id);

		arch_switch_to(&prev->host_sp, &next->host_sp);
	}
}

void run_sched_thread(struct sched_object *obj)
{
	if (obj->thread != NULL) {
		obj->thread(obj);
	}

	ASSERT(false, "Shouldn't go here, invalid thread!");
}

void switch_to_idle(run_thread_t idle_thread)
{
	uint16_t pcpu_id = get_cpu_id();
	struct sched_object *idle = &per_cpu(idle, pcpu_id);
	char idle_name[16];

	snprintf(idle_name, 16U, "idle%hu", pcpu_id);
	(void)strncpy_s(idle->name, 16U, idle_name, 16U);
	idle->thread = idle_thread;
	idle->prepare_switch_out = NULL;
	idle->prepare_switch_in = NULL;
	get_cpu_var(sched_ctx).curr_obj = idle;

	run_sched_thread(idle);
}
