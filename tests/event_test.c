#include <assert.h>
#include <stdio.h>
#include "event.h"

static u64 fake_tick;
u64 timer_ticks(void) { return fake_tick; }

int main(void) {
    kernel_event_t event;
    kernel_event_stats_t stats;
    event_init();
    assert(event_poll(&event) < 0);
    assert(event_publish(KERNEL_EVENT_TASK_STARTED, 12, 4, 100) == 0);
    fake_tick = 17;
    assert(event_publish(KERNEL_EVENT_NETWORK_LINK, 2, 1, 1) == 0);
    assert(event_poll(&event) == 0);
    assert(event.tick == 0 && event.source == 12 && event.code == 4 && event.value == 100);
    assert(event.type == KERNEL_EVENT_TASK_STARTED);
    assert(event_poll(&event) == 0 && event.tick == 17 && event.type == KERNEL_EVENT_NETWORK_LINK);
    for (u32 i = 0; i < KERNEL_EVENT_CAP; ++i)
        assert(event_publish(KERNEL_EVENT_FILESYSTEM, i, 0, i) == 0);
    assert(event_publish(KERNEL_EVENT_FILESYSTEM, 0, 0, 0) < 0);
    event_get_stats(&stats);
    assert(stats.pending == KERNEL_EVENT_CAP && stats.published == KERNEL_EVENT_CAP + 2);
    assert(stats.consumed == 2 && stats.dropped == 1);
    for (u32 i = 0; i < KERNEL_EVENT_CAP; ++i) assert(event_poll(&event) == 0);
    assert(event_poll(&event) < 0);
    assert(event_type_name(KERNEL_EVENT_TASK_STARTED)[0] == 't');
    puts("event bus tests passed: FIFO order, timestamps, bounded capacity, and drop accounting.");
    return 0;
}
