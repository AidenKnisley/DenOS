#include "e1000_ring.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

int main(void) {
    for (u32 completed = 0; completed < E1000_RX_RING_COUNT; ++completed) {
        e1000_rx_recycle_step_t step = e1000_rx_ring_recycle(completed);
        CHECK(step.tail == completed);
        CHECK(step.next == (completed + 1u) % E1000_RX_RING_COUNT);
    }
    {
        e1000_rx_recycle_step_t first = e1000_rx_ring_recycle(0u);
        e1000_rx_recycle_step_t last = e1000_rx_ring_recycle(E1000_RX_RING_COUNT - 1u);
        CHECK(first.tail == 0u && first.next == 1u);
        CHECK(last.tail == E1000_RX_RING_COUNT - 1u && last.next == 0u);
    }
    puts("E1000 RX ring tests passed: completed descriptor is returned to RDT and index advances separately.");
    return 0;
}
