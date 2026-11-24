#include <stdio.h>
#include <string.h>
#include "pmm.h"

#define MIB (1024ull * 1024ull)
#define TEST_STORAGE_SIZE 256u

static unsigned char info_storage[TEST_STORAGE_SIZE];
static int failures;

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); \
        failures++; \
    } \
} while (0)

static void put_u32(unsigned char *out, unsigned int value) {
    out[0] = (unsigned char)value;
    out[1] = (unsigned char)(value >> 8);
    out[2] = (unsigned char)(value >> 16);
    out[3] = (unsigned char)(value >> 24);
}

static void put_u64(unsigned char *out, unsigned long long value) {
    put_u32(out, (unsigned int)value);
    put_u32(out + 4, (unsigned int)(value >> 32));
}

static size_t add_mmap_entry(size_t offset, unsigned long long base,
                             unsigned long long length, unsigned int type) {
    put_u64(info_storage + offset, base);
    put_u64(info_storage + offset + 8, length);
    put_u32(info_storage + offset + 16, type);
    put_u32(info_storage + offset + 20, 0);
    return offset + 24;
}

static void build_info_two_entries(void) {
    size_t offset = 8;
    size_t tag_start = offset;
    put_u32(info_storage + tag_start, 6);
    put_u32(info_storage + tag_start + 4, 16 + 2 * 24);
    put_u32(info_storage + tag_start + 8, 24);
    put_u32(info_storage + tag_start + 12, 0);
    offset = add_mmap_entry(tag_start + 16, MIB, 63 * MIB, 1);
    offset = add_mmap_entry(offset, 0x10000u, 0xF0000u, 2);
    offset += 0; /* Keep the entry builder's returned offset explicit. */
    offset = (offset + 7u) & ~7u;
    put_u32(info_storage + offset, 0);
    put_u32(info_storage + offset + 4, 8);
    offset += 8;
    put_u32(info_storage, (unsigned int)offset);
    put_u32(info_storage + 4, 0);
}

static void build_small_map(void) {
    size_t offset = 8;
    put_u32(info_storage + offset, 6);
    put_u32(info_storage + offset + 4, 16 + 24);
    put_u32(info_storage + offset + 8, 24);
    put_u32(info_storage + offset + 12, 0);
    offset = add_mmap_entry(offset + 16, MIB, 3 * MIB, 1);
    offset = (offset + 7u) & ~7u;
    put_u32(info_storage + offset, 0);
    put_u32(info_storage + offset + 4, 8);
    offset += 8;
    put_u32(info_storage, (unsigned int)offset);
    put_u32(info_storage + 4, 0);
}

static void test_initialization_and_allocation(void) {
    pmm_stats_t stats;
    void *first;
    void *second;

    build_info_two_entries();
    CHECK(pmm_init(PMM_BOOT_MAGIC, info_storage, MIB, 2 * MIB) == 0,
          "valid Multiboot2 memory map initializes PMM");
    CHECK(pmm_is_initialized(), "PMM reports initialized state");
    CHECK(pmm_get_stats(&stats) == 0, "PMM statistics are available");
    CHECK(stats.managed_pages == 63u * 256u,
          "PMM counts only complete pages from usable memory ranges");
    CHECK(stats.free_pages == 62u * 256u,
          "kernel image is reserved from the available physical pages");
    CHECK(stats.reserved_pages >= 256u,
          "reserved firmware and kernel ranges are tracked");
    CHECK(stats.memory_map_entries == 2u, "both memory map entries are parsed");

    first = pmm_alloc_page();
    second = pmm_alloc_page();
    CHECK((unsigned long long)(unsigned long)first == 2 * MIB,
          "first page allocation begins immediately after the reserved kernel");
    CHECK((unsigned long long)(unsigned long)second == 2 * MIB + PMM_PAGE_SIZE,
          "page allocations are sequential and page aligned");
    CHECK(pmm_page_is_allocated(first) && pmm_page_is_allocated(second),
          "non-mutating ownership query recognizes live allocated frames");
    CHECK(!pmm_page_is_allocated((void *)(unsigned long)MIB),
          "ownership query rejects reserved memory");
    CHECK(pmm_free_page(first) == 0, "allocated page can be freed");
    CHECK(!pmm_page_is_allocated(first), "ownership query reports a freed frame as free");
    CHECK(pmm_free_page(first) < 0, "double free is rejected");
    CHECK(pmm_free_page((void *)((unsigned long)second + 1u)) < 0,
          "unaligned physical address is rejected");
    CHECK(pmm_free_page((void *)(unsigned long)MIB) < 0,
          "reserved kernel page cannot be freed into the allocator");
    CHECK(pmm_alloc_page() == first, "released page is reused by the allocator hint");
    CHECK(pmm_get_stats(&stats) == 0 && stats.allocated_pages == 2,
          "allocated page count tracks live allocations");
    CHECK(pmm_free_page(first) == 0 && pmm_free_page(second) == 0,
          "both allocated test pages can be returned");
    CHECK(pmm_get_stats(&stats) == 0 && stats.free_pages == 62u * 256u,
          "free page count is restored after release");
}

static void test_exhaustion_and_reuse(void) {
    void *pages[512];
    pmm_stats_t stats;
    int count = 0;
    int i;

    build_small_map();
    CHECK(pmm_init(PMM_BOOT_MAGIC, info_storage, MIB, 2 * MIB) == 0,
          "small usable memory map initializes PMM");
    while (count < (int)(sizeof(pages) / sizeof(pages[0]))) {
        pages[count] = pmm_alloc_page();
        if (!pages[count]) break;
        count++;
    }
    CHECK(count == 512, "small map yields every available page exactly once");
    CHECK(pmm_alloc_page() == 0, "allocator returns NULL when physical memory is exhausted");
    CHECK(pmm_get_stats(&stats) == 0 && stats.free_pages == 0,
          "free page statistic reaches zero at exhaustion");
    for (i = 0; i < count; i++) CHECK(pmm_free_page(pages[i]) == 0,
                                      "exhausted page can be freed");
    CHECK(pmm_get_stats(&stats) == 0 && stats.free_pages == 512,
          "freeing all pages restores the complete free pool");
    CHECK(pmm_alloc_page() != 0, "allocator works again after pages are released");
}

static void test_bad_information(void) {
    build_info_two_entries();
    CHECK(pmm_init(0, info_storage, MIB, 2 * MIB) < 0,
          "invalid Multiboot2 boot magic is rejected");
    CHECK(!pmm_is_initialized(), "failed initialization leaves PMM disabled");
    CHECK(pmm_init(PMM_BOOT_MAGIC, 0, MIB, 2 * MIB) < 0,
          "null boot information pointer is rejected");
    CHECK(pmm_init(PMM_BOOT_MAGIC, info_storage, 2 * MIB, MIB) < 0,
          "reversed kernel image bounds are rejected");

    memset(info_storage, 0, sizeof(info_storage));
    put_u32(info_storage, 16);
    put_u32(info_storage + 8, 6);
    put_u32(info_storage + 12, 7); /* Memory-map tag smaller than its header. */
    CHECK(pmm_init(PMM_BOOT_MAGIC, info_storage, MIB, 2 * MIB) < 0,
          "truncated memory-map tag is rejected");

    memset(info_storage, 0, sizeof(info_storage));
    put_u32(info_storage, 16);
    put_u32(info_storage + 8, 0);
    put_u32(info_storage + 12, 8);
    CHECK(pmm_init(PMM_BOOT_MAGIC, info_storage, MIB, 2 * MIB) < 0,
          "boot information without a memory map is rejected");

    memset(info_storage, 0, sizeof(info_storage));
    put_u32(info_storage, 56);
    put_u32(info_storage + 8, 6);
    put_u32(info_storage + 12, 40);
    put_u32(info_storage + 16, 25); /* The protocol requires 8-byte entry sizes. */
    put_u32(info_storage + 20, 0);
    put_u32(info_storage + 48, 0);
    put_u32(info_storage + 52, 8);
    CHECK(pmm_init(PMM_BOOT_MAGIC, info_storage, MIB, 2 * MIB) < 0,
          "non-eight-byte-aligned memory map entry size is rejected");
}

int main(void) {
    test_initialization_and_allocation();
    test_exhaustion_and_reuse();
    test_bad_information();
    if (failures) {
        fprintf(stderr, "%d physical-memory test(s) failed.\n", failures);
        return 1;
    }
    puts("Physical memory tests passed: Multiboot2 parsing, reserved ranges, page alignment, allocation/free, reuse, exhaustion, statistics, and malformed-input rejection.");
    return 0;
}
