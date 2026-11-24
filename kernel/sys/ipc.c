#include "ipc.h"
#include "kstring.h"

typedef struct ipc_endpoint {
    ipc_message_t queue[IPC_QUEUE_CAP];
    ipc_endpoint_stats_t stats;
    u8 head;
    u8 tail;
    u8 active;
} ipc_endpoint_t;

static ipc_endpoint_t endpoints[IPC_ENDPOINT_CAP];
static u32 endpoint_used;

static ipc_endpoint_t *find_endpoint(u32 task_id) {
    if (!task_id) return 0;
    for (u32 i = 0; i < IPC_ENDPOINT_CAP; ++i)
        if (endpoints[i].active && endpoints[i].stats.task_id == task_id)
            return &endpoints[i];
    return 0;
}

void ipc_init(void) {
    kmemset(endpoints, 0, sizeof(endpoints));
    endpoint_used = 0;
}

int ipc_endpoint_open(u32 task_id) {
    if (!task_id) return IPC_ERROR_ARGUMENT;
    if (find_endpoint(task_id)) return IPC_OK;
    for (u32 i = 0; i < IPC_ENDPOINT_CAP; ++i) {
        if (endpoints[i].active) continue;
        kmemset(&endpoints[i], 0, sizeof(endpoints[i]));
        endpoints[i].active = 1;
        endpoints[i].stats.task_id = task_id;
        ++endpoint_used;
        return IPC_OK;
    }
    return IPC_ERROR_NO_ENDPOINT;
}

int ipc_endpoint_close(u32 task_id) {
    ipc_endpoint_t *endpoint = find_endpoint(task_id);
    if (!endpoint) return IPC_ERROR_NO_ENDPOINT;
    kmemset(endpoint, 0, sizeof(*endpoint));
    if (endpoint_used) --endpoint_used;
    return IPC_OK;
}

int ipc_send(u32 destination, u32 sender, u32 type, u64 value,
             const void *payload, usize payload_length) {
    ipc_endpoint_t *endpoint;
    ipc_endpoint_t *sender_endpoint;
    ipc_message_t *message;
    if (!destination || !sender || payload_length > IPC_PAYLOAD_CAP ||
        (payload_length && !payload)) return IPC_ERROR_ARGUMENT;
    endpoint = find_endpoint(destination);
    if (!endpoint) return IPC_ERROR_NO_ENDPOINT;
    if (endpoint->stats.pending >= IPC_QUEUE_CAP) {
        ++endpoint->stats.dropped;
        return IPC_ERROR_FULL;
    }
    message = &endpoint->queue[endpoint->head];
    kmemset(message, 0, sizeof(*message));
    message->sender = sender;
    message->type = type;
    message->value = value;
    message->payload_length = (u8)payload_length;
    if (payload_length) kmemcpy(message->payload, payload, payload_length);
    endpoint->head = (u8)((endpoint->head + 1u) % IPC_QUEUE_CAP);
    ++endpoint->stats.pending;
    sender_endpoint = find_endpoint(sender);
    if (sender_endpoint) ++sender_endpoint->stats.sent;
    return IPC_OK;
}

int ipc_receive(u32 task_id, ipc_message_t *message) {
    ipc_endpoint_t *endpoint = find_endpoint(task_id);
    if (!endpoint) return IPC_ERROR_NO_ENDPOINT;
    if (!message) return IPC_ERROR_ARGUMENT;
    if (!endpoint->stats.pending) return IPC_ERROR_EMPTY;
    *message = endpoint->queue[endpoint->tail];
    kmemset(&endpoint->queue[endpoint->tail], 0, sizeof(ipc_message_t));
    endpoint->tail = (u8)((endpoint->tail + 1u) % IPC_QUEUE_CAP);
    if (endpoint->stats.pending) --endpoint->stats.pending;
    ++endpoint->stats.received;
    return IPC_OK;
}

int ipc_endpoint_get_stats(u32 task_id, ipc_endpoint_stats_t *stats) {
    ipc_endpoint_t *endpoint = find_endpoint(task_id);
    if (!endpoint || !stats) return IPC_ERROR_NO_ENDPOINT;
    *stats = endpoint->stats;
    return IPC_OK;
}

u32 ipc_endpoint_count(void) { return endpoint_used; }
