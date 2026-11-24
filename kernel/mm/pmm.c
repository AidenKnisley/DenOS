#include "pmm.h"
#include "kstring.h"

#define PMM_FRAME_COUNT ((usize)(PMM_ADDRESS_LIMIT / PMM_PAGE_SIZE))
#define PMM_BITMAP_BYTES ((PMM_FRAME_COUNT + 7u) / 8u)
#define PMM_INFO_MIN_SIZE 16u
#define MB2_TAG_END 0u
#define MB2_TAG_MMAP 6u
#define MB2_MMAP_ENTRY_MIN_SIZE 24u

/* A page is allocatable only when managed=1, reserved=0 and allocated=0. */
static u8 managed_bitmap[PMM_BITMAP_BYTES];
static u8 allocated_bitmap[PMM_BITMAP_BYTES];
static u8 reserved_bitmap[PMM_BITMAP_BYTES];
static pmm_stats_t statistics;
static usize allocation_hint;

static u32 read_u32(const u8 *bytes) {
    return (u32)bytes[0] | ((u32)bytes[1] << 8) |
           ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24);
}

static u64 read_u64(const u8 *bytes) {
    return (u64)read_u32(bytes) | ((u64)read_u32(bytes + 4) << 32);
}

static int bit_get(const u8 *bitmap, usize index) {
    return (bitmap[index >> 3] & (u8)(1u << (index & 7u))) != 0;
}

static void bit_set(u8 *bitmap, usize index) {
    bitmap[index >> 3] |= (u8)(1u << (index & 7u));
}

static void bit_clear(u8 *bitmap, usize index) {
    bitmap[index >> 3] &= (u8)~(1u << (index & 7u));
}

static u64 clamp_end(u64 base, u64 length) {
    if (!length || base >= PMM_ADDRESS_LIMIT) return base;
    if (length > PMM_ADDRESS_LIMIT - base) return PMM_ADDRESS_LIMIT;
    return base + length;
}

static void mark_usable(u64 base, u64 length) {
    u64 end;
    u64 first;
    u64 last;
    if (!length || base >= PMM_ADDRESS_LIMIT) return;
    end = clamp_end(base, length);
    if (end <= base) return;
    first = (base + PMM_PAGE_SIZE - 1u) & ~(u64)(PMM_PAGE_SIZE - 1u);
    last = end & ~(u64)(PMM_PAGE_SIZE - 1u);

    if (first >= last) return;
    for (u64 address = first; address < last; address += PMM_PAGE_SIZE) {
        usize frame = (usize)(address / PMM_PAGE_SIZE);
        if (!bit_get(managed_bitmap, frame)) {
            bit_set(managed_bitmap, frame);
            statistics.managed_pages++;
        }
    }
}

static void mark_reserved(u64 base, u64 length) {
    u64 end;
    u64 first;
    u64 last;

    if (!length || base >= PMM_ADDRESS_LIMIT) return;
    end = clamp_end(base, length);
    if (end <= base) return;
    first = base & ~(u64)(PMM_PAGE_SIZE - 1u);
    last = end > PMM_ADDRESS_LIMIT - (PMM_PAGE_SIZE - 1u)
         ? PMM_ADDRESS_LIMIT
         : (end + PMM_PAGE_SIZE - 1u) & ~(u64)(PMM_PAGE_SIZE - 1u);
    if (last > PMM_ADDRESS_LIMIT) last = PMM_ADDRESS_LIMIT;

    for (u64 address = first; address < last; address += PMM_PAGE_SIZE) {
        usize frame = (usize)(address / PMM_PAGE_SIZE);
        if (!bit_get(reserved_bitmap, frame)) {
            bit_set(reserved_bitmap, frame);
            statistics.reserved_pages++;
        }
        if (bit_get(allocated_bitmap, frame) == 0) {
            bit_set(allocated_bitmap, frame);
        }
    }
}

static int parse_memory_map(const u8 *info, u32 total_size) {
    u32 offset = 8;
    int found_map = 0;

    while (offset <= total_size && total_size - offset >= 8u) {
        const u8 *tag = info + offset;
        u32 type = read_u32(tag);
        u32 size = read_u32(tag + 4);
        u32 aligned_size;

        if (size < 8u || size > total_size - offset) return -1;
        if (type == MB2_TAG_END) return found_map ? 0 : -1;

        if (type == MB2_TAG_MMAP) {
            u32 entry_size;
            u32 entry_version;
            u32 entries_bytes;
            u32 cursor;

            if (size < 16u) return -1;
            entry_size = read_u32(tag + 8);
            entry_version = read_u32(tag + 12);
            entries_bytes = size - 16u;
            if (entry_version != 0u || entry_size < MB2_MMAP_ENTRY_MIN_SIZE ||
                (entry_size & 7u) != 0u || entries_bytes % entry_size != 0u) {
                statistics.invalid_memory_map_entries++;
                return -1;
            }

            found_map = 1;
            for (cursor = 16u; cursor < size; cursor += entry_size) {
                u64 base = read_u64(tag + cursor);
                u64 length = read_u64(tag + cursor + 8u);
                u32 region_type = read_u32(tag + cursor + 16u);
                if (!length || base + length < base) {
                    statistics.invalid_memory_map_entries++;
                    continue;
                }
                if (region_type == 1u) mark_usable(base, length);
                else mark_reserved(base, length);
                statistics.memory_map_entries++;
            }
        }

        aligned_size = (size + 7u) & ~7u;
        if (aligned_size < size || aligned_size > total_size - offset) return -1;
        offset += aligned_size;
    }
    return -1;
}

int pmm_init(u32 boot_magic, const void *multiboot_info,
             u64 kernel_start, u64 kernel_end) {
    const u8 *info = (const u8 *)multiboot_info;
    u32 total_size;
    usize frame;

    kmemset(managed_bitmap, 0, sizeof(managed_bitmap));
    kmemset(allocated_bitmap, 0, sizeof(allocated_bitmap));
    kmemset(reserved_bitmap, 0, sizeof(reserved_bitmap));
    kmemset(&statistics, 0, sizeof(statistics));
    allocation_hint = 0;

    if (boot_magic != PMM_BOOT_MAGIC || !info ||
        kernel_start >= kernel_end || kernel_end > PMM_ADDRESS_LIMIT)
        return -1;

    total_size = read_u32(info);
    if (total_size < PMM_INFO_MIN_SIZE || (total_size & 7u) != 0u)
        return -1;

    if (parse_memory_map(info, total_size) < 0 || !statistics.managed_pages)
        return -1;

    /* Firmware/legacy memory and the complete kernel image are never free. */
    mark_reserved(0, 1024u * 1024u);
    mark_reserved(kernel_start, kernel_end - kernel_start);

    /* GRUB's information block must remain readable after initialization. */
    if ((u64)(usize)multiboot_info < PMM_ADDRESS_LIMIT)
        mark_reserved((u64)(usize)multiboot_info, total_size);

    statistics.free_pages = 0;
    statistics.allocated_pages = 0;
    for (frame = 0; frame < PMM_FRAME_COUNT; frame++) {
        if (bit_get(managed_bitmap, frame)) {
            if (bit_get(reserved_bitmap, frame)) {
                if (!bit_get(allocated_bitmap, frame)) bit_set(allocated_bitmap, frame);
            } else {
                bit_clear(allocated_bitmap, frame);
                statistics.free_pages++;
            }
        }
    }
    /* Reserved pages are tracked separately from allocatable allocations. */
    statistics.initialized = 1;
    allocation_hint = 0;
    return 0;
}

void *pmm_alloc_page(void) {
    usize checked;
    if (!statistics.initialized || !statistics.free_pages) return 0;

    for (checked = 0; checked < PMM_FRAME_COUNT; checked++) {
        usize frame = allocation_hint + checked;
        if (frame >= PMM_FRAME_COUNT) frame -= PMM_FRAME_COUNT;
        if (bit_get(managed_bitmap, frame) &&
            !bit_get(reserved_bitmap, frame) &&
            !bit_get(allocated_bitmap, frame)) {
            u64 address = (u64)frame * PMM_PAGE_SIZE;
            bit_set(allocated_bitmap, frame);
            statistics.free_pages--;
            statistics.allocated_pages++;
            allocation_hint = frame + 1u;
            if (allocation_hint >= PMM_FRAME_COUNT) allocation_hint = 0;
#ifndef DENOS_PMM_TESTING
            kmemset((void *)(usize)address, 0, PMM_PAGE_SIZE);
#endif
            return (void *)(usize)address;
        }
    }
    return 0;
}

int pmm_free_page(void *page) {
    u64 address = (u64)(usize)page;
    usize frame;
    if (!statistics.initialized || !page ||
        (address & (PMM_PAGE_SIZE - 1u)) != 0u ||
        address >= PMM_ADDRESS_LIMIT)
        return -1;

    frame = (usize)(address / PMM_PAGE_SIZE);
    if (!bit_get(managed_bitmap, frame) ||
        bit_get(reserved_bitmap, frame) ||
        !bit_get(allocated_bitmap, frame))
        return -1;

    bit_clear(allocated_bitmap, frame);
    statistics.free_pages++;
    if (statistics.allocated_pages) statistics.allocated_pages--;
    if (frame < allocation_hint) allocation_hint = frame;
    return 0;
}

int pmm_page_is_allocated(const void *page) {
    u64 address = (u64)(usize)page;
    usize frame;
    if (!statistics.initialized || !page ||
        (address & (PMM_PAGE_SIZE - 1u)) != 0u ||
        address >= PMM_ADDRESS_LIMIT)
        return 0;
    frame = (usize)(address / PMM_PAGE_SIZE);
    return bit_get(managed_bitmap, frame) &&
           !bit_get(reserved_bitmap, frame) &&
           bit_get(allocated_bitmap, frame);
}

int pmm_get_stats(pmm_stats_t *stats) {
    if (!statistics.initialized || !stats) return -1;
    *stats = statistics;
    return 0;
}

int pmm_is_initialized(void) { return statistics.initialized; }
u64 pmm_address_limit(void) { return PMM_ADDRESS_LIMIT; }
