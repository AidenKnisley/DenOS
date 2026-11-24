#include <assert.h>
#include <stdio.h>
#include "scheduler.h"
#include "ipc.h"
#include "event.h"

static u64 fake_ticks;
static int fast_runs, slow_runs, once_runs;
u64 timer_ticks(void) { return fake_ticks; }
static int fast_task(void *p) { (void)p; ++fast_runs; return SCHEDULER_TASK_REPEAT; }
static int slow_task(void *p) { (void)p; ++slow_runs; return SCHEDULER_TASK_REPEAT; }
static int once_task(void *p) { (void)p; ++once_runs; return SCHEDULER_TASK_DONE; }

int main(void) {
    u32 fast_id, slow_id, once_id;
    scheduler_task_info_t info;
    kernel_event_t event;
    event_init();
    ipc_init();
    scheduler_init();
    assert(scheduler_create("fast-service", 6, 1, fast_task, 0, &fast_id) == 0);
    assert(scheduler_create("slow-service", 2, 5, slow_task, 0, &slow_id) == 0);
    assert(scheduler_create("one-shot", 7, 1, once_task, 0, &once_id) == 0);
    assert(scheduler_count() == 3);
    assert(ipc_endpoint_count() == 3);
    assert(event_poll(&event) == 0 && event.type == KERNEL_EVENT_TASK_STARTED);
    assert(event_poll(&event) == 0 && event.type == KERNEL_EVENT_TASK_STARTED);
    assert(event_poll(&event) == 0 && event.type == KERNEL_EVENT_TASK_STARTED);
    scheduler_run();
    assert(fast_runs == 1 && slow_runs == 1 && once_runs == 1);
    assert(scheduler_count() == 2);
    assert(scheduler_get(once_id, &info) == 0 && info.state == SCHEDULER_STOPPED);
    scheduler_run();
    assert(fast_runs == 1 && slow_runs == 1);
    fake_ticks = 1;
    scheduler_run();
    assert(fast_runs == 2 && slow_runs == 1);
    fake_ticks = 5;
    scheduler_run();
    assert(slow_runs == 2);
    assert(scheduler_stop(fast_id) == 0);
    assert(scheduler_count() == 1);
    assert(scheduler_restart(fast_id) == 0);
    assert(scheduler_count() == 2);
    assert(ipc_endpoint_count() == 3);
    assert(event_poll(&event) == 0 && event.type == KERNEL_EVENT_TASK_STOPPED);
    assert(event_poll(&event) == 0 && event.type == KERNEL_EVENT_TASK_RESTARTED);
    assert(scheduler_get(slow_id, &info) == 0 && info.runs == 2);
    assert(scheduler_total_runs() == 6);
    assert(scheduler_total_overruns() == 0);
    puts("scheduler tests: PASS");
    return 0;
}
