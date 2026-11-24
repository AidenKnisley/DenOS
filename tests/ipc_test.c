#include <assert.h>
#include <stdio.h>
#include "ipc.h"
#include "kstring.h"

int main(void) {
    ipc_message_t message;
    ipc_endpoint_stats_t stats;
    const char payload[] = "hello service";
    ipc_init();
    assert(ipc_endpoint_open(10) == IPC_OK);
    assert(ipc_endpoint_open(20) == IPC_OK);
    assert(ipc_endpoint_open(10) == IPC_OK);
    assert(ipc_endpoint_count() == 2);
    assert(ipc_receive(20, &message) == IPC_ERROR_EMPTY);
    assert(ipc_send(20, 10, 7, 0x12345678u, payload, sizeof(payload)) == IPC_OK);
    assert(ipc_receive(20, &message) == IPC_OK);
    assert(message.sender == 10 && message.type == 7 && message.value == 0x12345678u);
    assert(message.payload_length == sizeof(payload));
    assert(kmemcmp(message.payload, payload, sizeof(payload)) == 0);
    for (int i = 0; i < IPC_QUEUE_CAP; ++i)
        assert(ipc_send(20, 10, 1, (u64)i, 0, 0) == IPC_OK);
    assert(ipc_send(20, 10, 1, 0, 0, 0) == IPC_ERROR_FULL);
    assert(ipc_endpoint_get_stats(20, &stats) == IPC_OK);
    assert(stats.pending == IPC_QUEUE_CAP && stats.dropped == 1);
    for (int i = 0; i < IPC_QUEUE_CAP; ++i)
        assert(ipc_receive(20, &message) == IPC_OK);
    assert(ipc_receive(20, &message) == IPC_ERROR_EMPTY);
    assert(ipc_endpoint_close(10) == IPC_OK);
    assert(ipc_endpoint_count() == 1);
    assert(ipc_send(10, 20, 1, 0, 0, 0) == IPC_ERROR_NO_ENDPOINT);
    puts("IPC tests passed: endpoint lifecycle, message ordering, payloads, bounded queues, and overflow accounting.");
    return 0;
}
