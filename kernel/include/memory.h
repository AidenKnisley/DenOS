#ifndef AIDEN_MEMORY_H
#define AIDEN_MEMORY_H

#include "types.h"

/* General-purpose kernel heap. Returned pointers are 16-byte aligned. */
void memory_init(void);
void *kmalloc(usize size);
void *kcalloc(usize count, usize size);
void *krealloc(void *pointer, usize size);
void kfree(void *pointer);

/* Heap accounting reports aligned payload bytes, not allocator metadata. */
usize memory_used(void);
usize memory_capacity(void);
usize memory_available(void);
usize memory_allocation_count(void);
usize memory_total_allocations(void);
usize memory_failed_allocations(void);
usize memory_invalid_frees(void);
usize memory_largest_free_block(void);
usize memory_block_count(void);
int memory_validate(void);

#endif
