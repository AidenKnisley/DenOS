#ifndef DENOS_SCHEDULER_H
#define DENOS_SCHEDULER_H

#include "types.h"

#define SCHEDULER_MAX_TASKS 24
#define SCHEDULER_NAME_CAP 24
#define SCHEDULER_RUN_BUDGET 8

enum scheduler_state {
    SCHEDULER_UNUSED = 0,
    SCHEDULER_READY,
    SCHEDULER_SLEEPING,
    SCHEDULER_STOPPED
};

enum scheduler_result {
    SCHEDULER_TASK_DONE = 0,
    SCHEDULER_TASK_REPEAT = 1
};

typedef int (*scheduler_entry_t)(void *context);

typedef struct scheduler_task_info {
    u32 id;
    char name[SCHEDULER_NAME_CAP];
    u64 runs;
    u64 deadline;
    u32 interval;
    u8 priority;
    u8 state;
} scheduler_task_info_t;

/* Cooperative kernel service scheduler. It never switches stacks and must
 * only be called from normal kernel context, never from an IRQ handler. */
void scheduler_init(void);
int scheduler_create(const char *name, u8 priority, u32 interval_ticks,
                     scheduler_entry_t entry, void *context, u32 *task_id);
int scheduler_stop(u32 task_id);
int scheduler_restart(u32 task_id);
void scheduler_run(void);
int scheduler_get(u32 task_id, scheduler_task_info_t *out);
int scheduler_get_by_index(u32 index, scheduler_task_info_t *out);
u32 scheduler_count(void);
u64 scheduler_total_runs(void);
u64 scheduler_total_overruns(void);

#endif
