#include "event.h"
#include "timer.h"
#include "kstring.h"

static kernel_event_t queue[KERNEL_EVENT_CAP];
static u32 head, tail, pending;
static u64 published, consumed, dropped;

void event_init(void) {
    kmemset(queue, 0, sizeof(queue));
    head = tail = pending = 0;
    published = consumed = dropped = 0;
}

int event_publish(u16 type, u32 source, u32 code, u64 value) {
    kernel_event_t *event;
    if (!type) return -1;
    if (pending >= KERNEL_EVENT_CAP) { ++dropped; return -1; }
    event = &queue[head];
    event->tick = timer_ticks();
    event->source = source;
    event->code = code;
    event->value = value;
    event->type = type;
    head = (head + 1u) % KERNEL_EVENT_CAP;
    ++pending;
    ++published;
    return 0;
}

int event_poll(kernel_event_t *event) {
    if (!event || !pending) return -1;
    *event = queue[tail];
    kmemset(&queue[tail], 0, sizeof(queue[tail]));
    tail = (tail + 1u) % KERNEL_EVENT_CAP;
    --pending;
    ++consumed;
    return 0;
}

void event_get_stats(kernel_event_stats_t *stats) {
    if (!stats) return;
    stats->pending = pending;
    stats->published = published;
    stats->consumed = consumed;
    stats->dropped = dropped;
}

const char *event_type_name(u16 type) {
    switch (type) {
        case KERNEL_EVENT_TASK_STARTED: return "task-started";
        case KERNEL_EVENT_TASK_STOPPED: return "task-stopped";
        case KERNEL_EVENT_TASK_RESTARTED: return "task-restarted";
        case KERNEL_EVENT_DEVICE_REGISTERED: return "device-registered";
        case KERNEL_EVENT_DEVICE_STATE: return "device-state";
        case KERNEL_EVENT_NETWORK_LINK: return "network-link";
        case KERNEL_EVENT_FILESYSTEM: return "filesystem";
        default: return "unknown";
    }
}
