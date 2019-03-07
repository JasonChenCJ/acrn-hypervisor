/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SCHEDULE_H
#define SCHEDULE_H
#include <spinlock.h>

#define	NEED_RESCHEDULE		(1U)
#define	NEED_OFFLINE		(2U)

#define DEL_MODE_INIT		(1U)
#define DEL_MODE_IPI		(2U)

#define INVALID_TASK_ID		0xFFFFU
#define TASK_ID_MONOPOLY	0xFFFEU

struct sched_object;
typedef void (*run_thread_t)(struct sched_object *obj);
typedef void (*prepare_switch_t)(struct sched_object *obj);

struct sched_task_rc {
	uint16_t pcpu_id;
	uint16_t task_id;
	uint64_t left_cycles;
	uint64_t slice_cycles;
};

struct sched_object {
	char name[16];
	struct list_head run_list;
	struct sched_task_rc task_rc;
	uint64_t host_sp;
	run_thread_t thread;
	prepare_switch_t prepare_switch_out;
	prepare_switch_t prepare_switch_in;
};

struct sched_context {
	spinlock_t runqueue_lock;
	struct list_head runqueue;
	uint64_t flags;
	struct sched_object *curr_obj;
	spinlock_t scheduler_lock;
	struct hv_timer timer;
};

void init_scheduler(void);
void switch_to_idle(run_thread_t idle_thread);
void get_schedule_lock(uint16_t pcpu_id);
void release_schedule_lock(uint16_t pcpu_id);

int32_t allocate_task(struct sched_task_rc *task_rc);
void free_task(struct sched_task_rc *task_rc);

void add_to_cpu_runqueue(struct sched_object *obj, uint16_t pcpu_id);
void remove_from_cpu_runqueue(struct sched_object *obj, uint16_t pcpu_id);

void make_reschedule_request(uint16_t pcpu_id, uint16_t delmode);
bool need_reschedule(uint16_t pcpu_id);
void make_pcpu_offline(uint16_t pcpu_id);
int32_t need_offline(uint16_t pcpu_id);
struct sched_object *get_cur_sched_obj(uint16_t pcpu_id);

uint16_t pcpuid_from_sched_obj(const struct sched_object *obj);

void yield(void);
void schedule(void);
void run_sched_thread(struct sched_object *obj);

void arch_switch_to(void *prev_sp, void *next_sp);
#endif /* SCHEDULE_H */

