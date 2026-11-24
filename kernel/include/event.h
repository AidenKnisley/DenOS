#ifndef DENOS_EVENT_H
#define DENOS_EVENT_H

#include "types.h"

#define KERNEL_EVENT_CAP 64

enum kernel_event_type {
    KERNEL_EVENT_TASK_STARTED = 1,
    KERNEL_EVENT_TASK_STOPPED,
    KERNEL_EVENT_TASK_RESTARTED,
    KERNEL_EVENT_DEVICE_REGISTERED,
    KERNEL_EVENT_DEVICE_STATE,
    KERNEL_EVENT_NETWORK_LINK,
    KERNEL_EVENT_FILESYSTEM
};

typedef struct kernel_event {
    u64 tick;
    u32 source;
    u32 code;
    u64 value;
    u16 type;
} kernel_event_t;

typedef struct kernel_event_stats {
    u32 pending;
    u64 published;
    u64 consumed;
    u64 dropped;
} kernel_event_stats_t;

void event_init(void);
int event_publish(u16 type, u32 source, u32 code, u64 value);
int event_poll(kernel_event_t *event);
void event_get_stats(kernel_event_stats_t *stats);
const char *event_type_name(u16 type);

#endif
