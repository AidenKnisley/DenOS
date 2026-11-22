#include "memory.h"

/* Early kernel heap. A page-backed allocator will replace this bootstrap heap. */
#define HEAP_SIZE (1024u * 1024u)
static u8 heap[HEAP_SIZE] __attribute__((aligned(16)));
static usize cursor;

void memory_init(void) { cursor = 0; }
void *kmalloc(usize size) {
    if (!size) return 0;
    /* Bound before rounding so size + 15 cannot wrap on a bad request. */
    if (size > HEAP_SIZE - cursor) return 0;
    size = (size + 15u) & ~(usize)15u;
    if (size > HEAP_SIZE - cursor) return 0;
    void *result = &heap[cursor];
    cursor += size;
    return result;
}
usize memory_used(void) { return cursor; }
usize memory_capacity(void) { return HEAP_SIZE; }
