#ifndef DENOS_IPC_H
#define DENOS_IPC_H

#include "types.h"

#define IPC_ENDPOINT_CAP 24
#define IPC_QUEUE_CAP 16
#define IPC_PAYLOAD_CAP 32

enum ipc_status {
    IPC_OK = 0,
    IPC_ERROR_ARGUMENT = -1,
    IPC_ERROR_NO_ENDPOINT = -2,
    IPC_ERROR_FULL = -3,
    IPC_ERROR_EMPTY = -4
};

typedef struct ipc_message {
    u32 sender;
    u32 type;
    u64 value;
    u8 payload[IPC_PAYLOAD_CAP];
    u8 payload_length;
} ipc_message_t;

typedef struct ipc_endpoint_stats {
    u32 task_id;
    u32 pending;
    u64 received;
    u64 sent;
    u64 dropped;
} ipc_endpoint_stats_t;

void ipc_init(void);
int ipc_endpoint_open(u32 task_id);
int ipc_endpoint_close(u32 task_id);
int ipc_send(u32 destination, u32 sender, u32 type, u64 value,
             const void *payload, usize payload_length);
int ipc_receive(u32 task_id, ipc_message_t *message);
int ipc_endpoint_get_stats(u32 task_id, ipc_endpoint_stats_t *stats);
u32 ipc_endpoint_count(void);

#endif
