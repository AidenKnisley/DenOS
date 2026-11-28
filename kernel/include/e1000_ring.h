#ifndef DENOS_E1000_RING_H
#define DENOS_E1000_RING_H

#include "types.h"

#define E1000_RX_RING_COUNT 16u

typedef struct e1000_rx_recycle_step {
    u32 tail;
    u32 next;
} e1000_rx_recycle_step_t;

/* RDT is the last descriptor returned to the NIC, not the next descriptor
 * the driver will inspect. Keep the hardware tail and software next index
 * separate so wraparound cannot accidentally skip a descriptor. */
static inline e1000_rx_recycle_step_t e1000_rx_ring_recycle(u32 completed) {
    e1000_rx_recycle_step_t step;
    completed %= E1000_RX_RING_COUNT;
    step.tail = completed;
    step.next = (completed + 1u) % E1000_RX_RING_COUNT;
    return step;
}

#endif
