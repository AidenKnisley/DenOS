#include "scheduler.h"
#include "timer.h"
#include "kstring.h"
#include "ipc.h"
#include "event.h"

#define SCHEDULER_MAGIC 0x53434844u

typedef struct scheduler_task {
    scheduler_task_info_t info;
    scheduler_entry_t entry;
    void *context;
    u32 generation;
} scheduler_task_t;

static scheduler_task_t tasks[SCHEDULER_MAX_TASKS];
static u32 next_id;
static u32 cursor;
static u32 used;
static u64 total_runs;
static u64 overruns;
static int running;

static scheduler_task_t *find_task(u32 id) {
    if (!id) return 0;
    for (u32 i = 0; i < SCHEDULER_MAX_TASKS; ++i)
        if (tasks[i].info.state != SCHEDULER_UNUSED && tasks[i].info.id == id)
            return &tasks[i];
    return 0;
}

void scheduler_init(void) {
    for (u32 i = 0; i < SCHEDULER_MAX_TASKS; ++i) {
        kmemset(&tasks[i], 0, sizeof(tasks[i]));
        tasks[i].info.state = SCHEDULER_UNUSED;
    }
    next_id = 1;
    cursor = 0;
    used = 0;
    total_runs = 0;
    overruns = 0;
    running = 0;
}

int scheduler_create(const char *name, u8 priority, u32 interval_ticks,
                     scheduler_entry_t entry, void *context, u32 *task_id) {
    u32 slot = SCHEDULER_MAX_TASKS;
    scheduler_task_t *task;
    if (!name || !name[0] || kstrnlen(name, SCHEDULER_NAME_CAP) >= SCHEDULER_NAME_CAP ||
        !entry || priority > 7u || !interval_ticks) return -1;
    for (u32 i = 0; i < SCHEDULER_MAX_TASKS; ++i) {
        if (tasks[i].info.state == SCHEDULER_UNUSED ||
            tasks[i].info.state == SCHEDULER_STOPPED) { slot = i; break; }
    }
    if (slot == SCHEDULER_MAX_TASKS) return -1;
    task = &tasks[slot];
    if (task->info.state == SCHEDULER_STOPPED && task->info.id)
        (void)ipc_endpoint_close(task->info.id);
    kmemset(task, 0, sizeof(*task));
    task->info.id = next_id++;
    if (!next_id) next_id = 1;
    kstrlcpy(task->info.name, name, sizeof(task->info.name));
    task->info.priority = priority;
    task->info.interval = interval_ticks;
    task->info.deadline = timer_ticks();
    task->info.state = SCHEDULER_READY;
    task->entry = entry;
    task->context = context;
    task->generation = SCHEDULER_MAGIC;
    if (ipc_endpoint_open(task->info.id) != IPC_OK) {
        kmemset(task, 0, sizeof(*task));
        task->info.state = SCHEDULER_UNUSED;
        return -1;
    }
    ++used;
    (void)event_publish(KERNEL_EVENT_TASK_STARTED, task->info.id,
                        task->info.priority, task->info.interval);
    if (task_id) *task_id = task->info.id;
    return 0;
}

int scheduler_stop(u32 task_id) {
    scheduler_task_t *task = find_task(task_id);
    if (!task || task->info.state == SCHEDULER_STOPPED) return -1;
    task->info.state = SCHEDULER_STOPPED;
    if (used) --used;
    (void)event_publish(KERNEL_EVENT_TASK_STOPPED, task->info.id, 0, 0);
    return 0;
}

int scheduler_restart(u32 task_id) {
    scheduler_task_t *task = find_task(task_id);
    if (!task || task->info.state != SCHEDULER_STOPPED ||
        task->generation != SCHEDULER_MAGIC) return -1;
    task->info.deadline = timer_ticks();
    task->info.state = SCHEDULER_READY;
    ++used;
    (void)event_publish(KERNEL_EVENT_TASK_RESTARTED, task->info.id, 0, 0);
    return 0;
}

void scheduler_run(void) {
    u32 executed = 0;
    u64 now;
    if (running) { ++overruns; return; }
    running = 1;
    now = timer_ticks();
    /* Priority is strict within each pass; the rotating cursor prevents a
       same-priority service from permanently owning the first slot. */
    for (int priority = 7; priority >= 0 && executed < SCHEDULER_RUN_BUDGET; --priority) {
        for (u32 n = 0; n < SCHEDULER_MAX_TASKS && executed < SCHEDULER_RUN_BUDGET; ++n) {
            u32 i = (cursor + n) % SCHEDULER_MAX_TASKS;
            scheduler_task_t *task = &tasks[i];
            int result;
            if (task->info.state != SCHEDULER_READY ||
                task->info.priority != (u8)priority || now < task->info.deadline)
                continue;
            result = task->entry(task->context);
            ++task->info.runs;
            ++total_runs;
            ++executed;
            if (result == SCHEDULER_TASK_DONE) {
                task->info.state = SCHEDULER_STOPPED;
                if (used) --used;
            } else {
                task->info.deadline = timer_ticks() + task->info.interval;
            }
            cursor = (i + 1u) % SCHEDULER_MAX_TASKS;
        }
    }
    running = 0;
}

static int copy_info(const scheduler_task_t *task, scheduler_task_info_t *out) {
    if (!task || !out || task->info.state == SCHEDULER_UNUSED) return -1;
    *out = task->info;
    return 0;
}

int scheduler_get(u32 task_id, scheduler_task_info_t *out) {
    return copy_info(find_task(task_id), out);
}

int scheduler_get_by_index(u32 index, scheduler_task_info_t *out) {
    u32 found = 0;
    if (!out) return -1;
    for (u32 i = 0; i < SCHEDULER_MAX_TASKS; ++i) {
        if (tasks[i].info.state == SCHEDULER_UNUSED) continue;
        if (found++ == index) return copy_info(&tasks[i], out);
    }
    return -1;
}

u32 scheduler_count(void) { return used; }
u64 scheduler_total_runs(void) { return total_runs; }
u64 scheduler_total_overruns(void) { return overruns; }
