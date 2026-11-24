#include "memory.h"
#include "kstring.h"

#define HEAP_SIZE          (1024u * 1024u)
#define HEAP_ALIGNMENT     16u
#define HEAP_ALIGN_UP(n)   (((n) + (HEAP_ALIGNMENT - 1u)) & ~(usize)(HEAP_ALIGNMENT - 1u))
#define HEAP_BLOCK_MAGIC   0xD30B10C5u
#define HEAP_MIN_SPLIT     HEAP_ALIGNMENT
#define HEAP_POISON_ALLOC  0xCDu
#define HEAP_POISON_FREE   0xDDu

typedef struct heap_block {
    usize size;
    struct heap_block *previous;
    struct heap_block *next;
    u32 magic;
    u32 is_free;
} heap_block_t;

#define HEAP_HEADER_SIZE HEAP_ALIGN_UP(sizeof(heap_block_t))

static u8 heap[HEAP_SIZE] __attribute__((aligned(HEAP_ALIGNMENT)));
static heap_block_t *first_block;
static usize used_bytes;
static usize live_allocations;
static usize total_allocations;
static usize failed_allocations;
static usize invalid_frees;

static int block_valid(const heap_block_t *block);

static int pointer_in_heap(const void *pointer) {
    usize address = (usize)pointer;
    usize begin = (usize)heap;
    return address >= begin && address < begin + HEAP_SIZE;
}

static heap_block_t *block_from_payload(void *pointer) {
    heap_block_t *block;
    if (!pointer_in_heap(pointer)) return 0;
    for (block = first_block; block; block = block->next) {
        if (!block_valid(block)) return 0;
        if ((void *)((u8 *)block + HEAP_HEADER_SIZE) == pointer)
            return block;
    }
    return 0;
}

static int block_valid(const heap_block_t *block) {
    usize address = (usize)block;
    usize begin = (usize)heap;
    usize end = begin + HEAP_SIZE;
    if (address < begin || address > end - HEAP_HEADER_SIZE ||
        (address & (HEAP_ALIGNMENT - 1u)) != 0u) return 0;
    if (block->magic != HEAP_BLOCK_MAGIC) return 0;
    if ((block->size & (HEAP_ALIGNMENT - 1u)) != 0u) return 0;
    if (block->size > end - address - HEAP_HEADER_SIZE) return 0;
    return block->is_free <= 1u;
}

static int merge_next(heap_block_t *block) {
    heap_block_t *next = block->next;
    heap_block_t *following;
    if (!next || !block_valid(next) || !next->is_free ||
        next->previous != block) return 0;
    following = next->next;
    if (following && (!block_valid(following) || following->previous != next))
        return 0;
    block->size += HEAP_HEADER_SIZE + next->size;
    block->next = following;
    if (following) following->previous = block;
    next->magic = 0;
    next->previous = 0;
    next->next = 0;
    if (block->is_free)
        kmemset((u8 *)block + HEAP_HEADER_SIZE, HEAP_POISON_FREE, block->size);
    return 1;
}

static void split_block(heap_block_t *block, usize wanted) {
    usize remainder;
    heap_block_t *tail;
    if (block->size < wanted) return;
    if (block->next && (!block_valid(block->next) ||
                        block->next->previous != block)) return;
    remainder = block->size - wanted;
    if (remainder < HEAP_HEADER_SIZE + HEAP_MIN_SPLIT) return;

    tail = (heap_block_t *)((u8 *)block + HEAP_HEADER_SIZE + wanted);
    tail->size = remainder - HEAP_HEADER_SIZE;
    tail->previous = block;
    tail->next = block->next;
    tail->magic = HEAP_BLOCK_MAGIC;
    tail->is_free = 1;
    if (tail->next) tail->next->previous = tail;
    block->next = tail;
    block->size = wanted;
    kmemset((u8 *)tail + HEAP_HEADER_SIZE, HEAP_POISON_FREE, tail->size);
}

void memory_init(void) {
    kmemset(heap, HEAP_POISON_FREE, sizeof(heap));
    first_block = (heap_block_t *)heap;
    first_block->size = HEAP_SIZE - HEAP_HEADER_SIZE;
    first_block->previous = 0;
    first_block->next = 0;
    first_block->magic = HEAP_BLOCK_MAGIC;
    first_block->is_free = 1;
    used_bytes = 0;
    live_allocations = 0;
    total_allocations = 0;
    failed_allocations = 0;
    invalid_frees = 0;
}

void *kmalloc(usize size) {
    heap_block_t *block;
    usize aligned;

    if (!size) return 0;
    if (size > (usize)-1 - (HEAP_ALIGNMENT - 1u)) {
        failed_allocations++;
        return 0;
    }
    aligned = HEAP_ALIGN_UP(size);
    if (aligned > HEAP_SIZE - HEAP_HEADER_SIZE) {
        failed_allocations++;
        return 0;
    }

    for (block = first_block; block; block = block->next) {
        if (!block_valid(block)) {
            failed_allocations++;
            return 0;
        }
        if (block->is_free && block->size >= aligned) {
            split_block(block, aligned);
            block->is_free = 0;
            used_bytes += block->size;
            live_allocations++;
            total_allocations++;
            kmemset((u8 *)block + HEAP_HEADER_SIZE, HEAP_POISON_ALLOC, block->size);
            return (u8 *)block + HEAP_HEADER_SIZE;
        }
    }
    failed_allocations++;
    return 0;
}

void *kcalloc(usize count, usize size) {
    usize total;
    void *memory;
    if (!count || !size) return 0;
    if (count > (usize)-1 / size) {
        failed_allocations++;
        return 0;
    }
    total = count * size;
    memory = kmalloc(total);
    if (memory) kmemset(memory, 0, total);
    return memory;
}

void kfree(void *pointer) {
    heap_block_t *block;
    if (!pointer) return;
    block = block_from_payload(pointer);
    if (!block || !block_valid(block) || block->is_free) {
        invalid_frees++;
        return;
    }

    block->is_free = 1;
    kmemset((u8 *)block + HEAP_HEADER_SIZE, HEAP_POISON_FREE, block->size);
    if (used_bytes >= block->size) used_bytes -= block->size;
    else used_bytes = 0;
    if (live_allocations) live_allocations--;

    (void)merge_next(block);
    if (block->previous && block_valid(block->previous) &&
        block->previous->is_free) {
        block = block->previous;
        (void)merge_next(block);
    }
}

void *krealloc(void *pointer, usize size) {
    heap_block_t *block;
    usize wanted;
    usize old_size;
    void *replacement;

    if (!pointer) return kmalloc(size);
    if (!size) {
        kfree(pointer);
        return 0;
    }
    if (size > (usize)-1 - (HEAP_ALIGNMENT - 1u)) {
        failed_allocations++;
        return 0;
    }
    block = block_from_payload(pointer);
    if (!block || !block_valid(block) || block->is_free) {
        failed_allocations++;
        return 0;
    }
    wanted = HEAP_ALIGN_UP(size);
    old_size = block->size;
    if (wanted <= old_size) {
        split_block(block, wanted);
        if (block->size < old_size) used_bytes -= old_size - block->size;
        /* split_block may have created a new free tail. Merge that tail with
         * a following free block, but never merge it back into the live block. */
        if (block->next && block_valid(block->next) && block->next->is_free)
            (void)merge_next(block->next);
        return pointer;
    }

    if (block->next && block_valid(block->next) && block->next->is_free &&
        old_size + HEAP_HEADER_SIZE + block->next->size >= wanted) {
        if (!merge_next(block)) {
            failed_allocations++;
            return 0;
        }
        split_block(block, wanted);
        if (block->size < wanted) {
            failed_allocations++;
            return 0;
        }
        used_bytes += block->size - old_size;
        return pointer;
    }

    replacement = kmalloc(size);
    if (!replacement) return 0;
    kmemcpy(replacement, pointer, old_size);
    kfree(pointer);
    return replacement;
}

usize memory_used(void) { return used_bytes; }
usize memory_capacity(void) { return HEAP_SIZE; }
usize memory_available(void) {
    usize available = 0;
    heap_block_t *block;
    for (block = first_block; block; block = block->next) {
        if (!block_valid(block)) return 0;
        if (block->is_free) available += block->size;
    }
    return available;
}
usize memory_allocation_count(void) { return live_allocations; }
usize memory_total_allocations(void) { return total_allocations; }
usize memory_failed_allocations(void) { return failed_allocations; }
usize memory_invalid_frees(void) { return invalid_frees; }

usize memory_largest_free_block(void) {
    usize largest = 0;
    heap_block_t *block;
    for (block = first_block; block; block = block->next) {
        if (!block_valid(block)) return 0;
        if (block->is_free && block->size > largest) largest = block->size;
    }
    return largest;
}

usize memory_block_count(void) {
    usize count = 0;
    heap_block_t *block;
    for (block = first_block; block; block = block->next) {
        if (!block_valid(block)) return 0;
        count++;
    }
    return count;
}

int memory_validate(void) {
    usize total = 0;
    usize allocations = 0;
    usize blocks = 0;
    heap_block_t *block;
    heap_block_t *previous = 0;
    u8 *expected = heap;

    if (!first_block || first_block != (heap_block_t *)heap) return 0;
    for (block = first_block; block; block = block->next) {
        if ((u8 *)block != expected || !block_valid(block) ||
            block->previous != previous) return 0;
        if (block->is_free && previous && previous->is_free) return 0;
        if (block->is_free) {
            const u8 *payload = (const u8 *)block + HEAP_HEADER_SIZE;
            for (usize i = 0; i < block->size; ++i)
                if (payload[i] != HEAP_POISON_FREE) return 0;
        } else {
            total += block->size;
            allocations++;
        }
        expected = (u8 *)block + HEAP_HEADER_SIZE + block->size;
        previous = block;
        blocks++;
        if (blocks > HEAP_SIZE / HEAP_HEADER_SIZE) return 0;
    }
    if (expected != heap + HEAP_SIZE) return 0;
    return total == used_bytes && allocations == live_allocations;
}
