#include "vmalloc.h"
#include "paging.h"
#include "pmm.h"
#include "cpu.h"
#include "kstring.h"

#define VMALLOC_BITMAP_BYTES ((VMALLOC_PAGE_COUNT + 7u) / 8u)
#define VMALLOC_MAX_ALLOCS 128u
#define RFLAGS_IF (1ull << 9)

typedef struct vmalloc_record {
    u32 first_page;
    u32 page_count;
    u64 flags;
    u8 active;
} vmalloc_record_t;

static u8 allocation_bitmap[VMALLOC_BITMAP_BYTES];
static u64 physical_frames[VMALLOC_PAGE_COUNT];
static vmalloc_record_t records[VMALLOC_MAX_ALLOCS];
static vmalloc_stats_t statistics;
static u8 initialized;

static u64 irq_save(void) {
    u64 flags = cpu_read_flags();
    cpu_interrupt_disable();
    return flags;
}

static void irq_restore(u64 flags) {
    if (flags & RFLAGS_IF) cpu_interrupt_enable();
}

static int bit_get(u32 bit) {
    return (allocation_bitmap[bit >> 3] & (u8)(1u << (bit & 7u))) != 0;
}

static void bit_set(u32 bit) {
    allocation_bitmap[bit >> 3] |= (u8)(1u << (bit & 7u));
}

static void bit_clear(u32 bit) {
    allocation_bitmap[bit >> 3] &= (u8)~(1u << (bit & 7u));
}

static int find_record_slot(void) {
    for (u32 i = 0; i < VMALLOC_MAX_ALLOCS; ++i)
        if (!records[i].active) return (int)i;
    return -1;
}

static int find_virtual_run(u32 count, u32 *first_out) {
    u32 run = 0;
    u32 start = 0;
    if (!count || count > VMALLOC_PAGE_COUNT || !first_out) return -1;
    for (u32 i = 0; i < VMALLOC_PAGE_COUNT; ++i) {
        if (!bit_get(i)) {
            if (!run) start = i;
            run++;
            if (run == count) {
                *first_out = start;
                return 0;
            }
        } else run = 0;
    }
    return -1;
}

static void release_virtual_run(u32 first, u32 count) {
    for (u32 i = 0; i < count; ++i) bit_clear(first + i);
}

static int rollback_mapping(u32 first, u32 mapped_count, u32 allocated_count,
                            u32 reserved_count, int record_slot, u64 flags) {
    const u64 safe_to_free = 1ull << 63; /* Physical addresses are <= 52 bits. */
    int consistent = 1;
    u32 remaining_mapped = 0;

    /* Keep frame ownership until after every successful unmap. A frame whose
       unmap fails must never be returned to PMM while a live PTE may reference it. */
    for (u32 i = 0; i < mapped_count; ++i) {
        u32 slot = first + i;
        u64 virtual_address = VMALLOC_BASE + (u64)slot * VMALLOC_PAGE_SIZE;
        vmm_mapping_t old;
        if (vmm_unmap_page(virtual_address, &old) == VMM_OK)
            physical_frames[slot] |= safe_to_free;
        else { consistent = 0; remaining_mapped++; }
    }
    /* If mapping failed after allocation, the final frame has no PTE. */
    for (u32 i = mapped_count; i < allocated_count; ++i)
        physical_frames[first + i] |= safe_to_free;

    for (u32 i = 0; i < allocated_count; ++i) {
        u32 slot = first + i;
        u64 frame = physical_frames[slot];
        if (frame & safe_to_free) {
            frame &= ~safe_to_free;
            if (pmm_free_page((void *)(usize)frame) == 0)
                physical_frames[slot] = 0;
            else consistent = 0;
        }
    }

    if (consistent) {
        release_virtual_run(first, reserved_count);
        return 0;
    }

    /* Quarantine the whole virtual run if rollback itself detects corruption.
       This intentionally leaks capacity rather than allowing a second allocation
       to alias a potentially still-mapped frame. The shell exposes this counter. */
    records[record_slot].first_page = first;
    records[record_slot].page_count = reserved_count;
    records[record_slot].flags = flags;
    records[record_slot].active = 2;
    statistics.quarantined_allocations++;
    statistics.quarantined_bytes += (u64)reserved_count * VMALLOC_PAGE_SIZE;
    statistics.pages_mapped += remaining_mapped;
    statistics.used_bytes += (u64)reserved_count * VMALLOC_PAGE_SIZE;
    statistics.free_bytes -= (u64)reserved_count * VMALLOC_PAGE_SIZE;
    return -1;
}

int vmalloc_init(void) {
    u64 flags;
    if (initialized) return VMALLOC_OK;
    if (!paging_is_initialized() || !pmm_is_initialized())
        return VMALLOC_ERR_NOT_READY;
    flags = irq_save();
    kmemset(allocation_bitmap, 0, sizeof(allocation_bitmap));
    kmemset(physical_frames, 0, sizeof(physical_frames));
    kmemset(records, 0, sizeof(records));
    kmemset(&statistics, 0, sizeof(statistics));
    statistics.total_bytes = VMALLOC_CAPACITY_BYTES;
    statistics.free_bytes = VMALLOC_CAPACITY_BYTES;
    initialized = 1;
    irq_restore(flags);
    return VMALLOC_OK;
}

int vmalloc_is_initialized(void) { return initialized != 0; }

void *kvalloc(usize size, u64 flags) {
    const u64 allowed = VMM_WRITE | VMM_WRITE_THROUGH | VMM_CACHE_DISABLE |
                        VMM_GLOBAL | VMM_NO_EXECUTE;
    u64 irq_flags;
    u64 rounded;
    u32 pages, first;
    int record_slot;
    u32 allocated = 0;
    u32 mapped = 0;
    void *result = NULL;

    if (!initialized) return NULL;
    if (!size || (flags & ~allowed)) goto failed_unlocked;
    if ((u64)size > ~0ull - (VMALLOC_PAGE_SIZE - 1u)) goto failed_unlocked;
    rounded = ((u64)size + VMALLOC_PAGE_SIZE - 1u) & ~(VMALLOC_PAGE_SIZE - 1u);
    if (rounded > VMALLOC_CAPACITY_BYTES) goto failed_unlocked;
    pages = (u32)(rounded / VMALLOC_PAGE_SIZE);
    if (!pages) goto failed_unlocked;

    irq_flags = irq_save();
    record_slot = find_record_slot();
    if (record_slot < 0 || find_virtual_run(pages, &first) < 0) {
        statistics.allocation_failures++;
        irq_restore(irq_flags);
        return NULL;
    }
    for (u32 i = 0; i < pages; ++i) bit_set(first + i);

    for (allocated = 0; allocated < pages; ++allocated) {
        void *frame = pmm_alloc_page();
        u64 physical;
        u64 virtual_address;
        int map_result;
        if (!frame) break;
        physical = (u64)(usize)frame;
        physical_frames[first + allocated] = physical;
        virtual_address = VMALLOC_BASE + (u64)(first + allocated) * VMALLOC_PAGE_SIZE;
        map_result = vmm_map_page(virtual_address, physical, flags);
        if (map_result != VMM_OK) {
            allocated++;
            break;
        }
        mapped++;
    }

    if (mapped != pages) {
        (void)rollback_mapping(first, mapped, allocated, pages, record_slot, flags);
        statistics.allocation_failures++;
        irq_restore(irq_flags);
        return NULL;
    }

    records[record_slot].first_page = first;
    records[record_slot].page_count = pages;
    records[record_slot].flags = flags;
    records[record_slot].active = 1;
    statistics.live_allocations++;
    statistics.pages_mapped += pages;
    statistics.used_bytes += (u64)pages * VMALLOC_PAGE_SIZE;
    statistics.free_bytes -= (u64)pages * VMALLOC_PAGE_SIZE;
    result = (void *)(usize)(VMALLOC_BASE + (u64)first * VMALLOC_PAGE_SIZE);
    irq_restore(irq_flags);
    return result;

failed_unlocked:
    if (initialized) {
        irq_flags = irq_save();
        statistics.allocation_failures++;
        irq_restore(irq_flags);
    }
    return NULL;
}

int kvfree(void *address) {
    u64 irq_flags;
    u64 base = (u64)(usize)address;
    int record_slot = -1;
    vmalloc_record_t record;

    if (!initialized) return VMALLOC_ERR_NOT_READY;
    if (!address || base < VMALLOC_BASE ||
        base >= VMALLOC_BASE + VMALLOC_CAPACITY_BYTES ||
        ((base - VMALLOC_BASE) & (VMALLOC_PAGE_SIZE - 1u)))
        return VMALLOC_ERR_ARGUMENT;

    irq_flags = irq_save();
    for (u32 i = 0; i < VMALLOC_MAX_ALLOCS; ++i) {
        if (records[i].active &&
            records[i].first_page == (u32)((base - VMALLOC_BASE) / VMALLOC_PAGE_SIZE)) {
            if (records[i].active == 2) {
                irq_restore(irq_flags);
                return VMALLOC_ERR_CORRUPT;
            }
            record_slot = (int)i;
            break;
        }
    }
    if (record_slot < 0) {
        irq_restore(irq_flags);
        return VMALLOC_ERR_NOT_FOUND;
    }
    record = records[record_slot];

    /* Preflight every mapping before mutating the address space. */
    for (u32 i = 0; i < record.page_count; ++i) {
        u32 slot = record.first_page + i;
        u64 virtual_address = VMALLOC_BASE + (u64)slot * VMALLOC_PAGE_SIZE;
        vmm_mapping_t current;
        if (!bit_get(slot) || !physical_frames[slot] ||
            vmm_query(virtual_address, &current) != VMM_OK ||
            current.page_size != VMALLOC_PAGE_SIZE ||
            (current.physical_address & ~(VMALLOC_PAGE_SIZE - 1u)) != physical_frames[slot] ||
            !pmm_page_is_allocated((const void *)(usize)physical_frames[slot])) {
            irq_restore(irq_flags);
            return VMALLOC_ERR_CORRUPT;
        }
    }

    {
        u32 removed = 0;
        int unmap_failed = 0;
        for (u32 i = 0; i < record.page_count; ++i) {
            u32 slot = record.first_page + i;
            u64 virtual_address = VMALLOC_BASE + (u64)slot * VMALLOC_PAGE_SIZE;
            vmm_mapping_t old;
            int unmap_result = vmm_unmap_page(virtual_address, &old);
            if (unmap_result != VMM_OK) {
                unmap_failed = 1;
                break;
            }
            removed++;
            if ((old.physical_address & ~(VMALLOC_PAGE_SIZE - 1u)) != physical_frames[slot]) {
                unmap_failed = 1;
                break;
            }
        }
        if (unmap_failed) {
            int restored = 1;
            u32 currently_mapped = record.page_count - removed;
            for (u32 i = 0; i < removed; ++i) {
                u32 slot = record.first_page + i;
                u64 virtual_address = VMALLOC_BASE + (u64)slot * VMALLOC_PAGE_SIZE;
                if (vmm_remap_page(virtual_address, physical_frames[slot], record.flags) != VMM_OK)
                    restored = 0;
                else currently_mapped++;
            }
            if (!restored) {
                /* The range is now permanently quarantined. Keep every backing
                   frame owned and prevent a later allocation from aliasing it. */
                records[record_slot].active = 2;
                statistics.live_allocations--;
                statistics.quarantined_allocations++;
                statistics.quarantined_bytes += (u64)record.page_count * VMALLOC_PAGE_SIZE;
                statistics.pages_mapped -= record.page_count;
                statistics.pages_mapped += currently_mapped;
            }
            irq_restore(irq_flags);
            return VMALLOC_ERR_CORRUPT;
        }
        statistics.pages_mapped -= record.page_count;
    }

    for (u32 i = 0; i < record.page_count; ++i) {
        u32 slot = record.first_page + i;
        if (pmm_free_page((void *)(usize)physical_frames[slot]) != 0) {
            /* A PMM ownership failure is a kernel accounting fault. The VA is
               already unmapped; do not make the range available to another
               allocation while its backing-frame status is uncertain. */
            records[record_slot].active = 2;
            statistics.live_allocations--;
            statistics.quarantined_allocations++;
            statistics.quarantined_bytes += (u64)record.page_count * VMALLOC_PAGE_SIZE;
            irq_restore(irq_flags);
            return VMALLOC_ERR_CORRUPT;
        }
        physical_frames[slot] = 0;
    }
    release_virtual_run(record.first_page, record.page_count);

    records[record_slot].active = 0;
    statistics.live_allocations--;
    statistics.used_bytes -= (u64)record.page_count * VMALLOC_PAGE_SIZE;
    statistics.free_bytes += (u64)record.page_count * VMALLOC_PAGE_SIZE;
    irq_restore(irq_flags);
    return VMALLOC_OK;
}

int vmalloc_get_stats(vmalloc_stats_t *stats) {
    u64 flags;
    if (!stats) return VMALLOC_ERR_ARGUMENT;
    if (!initialized) return VMALLOC_ERR_NOT_READY;
    flags = irq_save();
    *stats = statistics;
    irq_restore(flags);
    return VMALLOC_OK;
}
