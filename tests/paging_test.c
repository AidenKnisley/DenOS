#include <stdio.h>
#include <string.h>
#include "paging.h"
#include "pmm.h"
#include "cpu.h"

#define PRESENT (1ull << 0)
#define WRITE   (1ull << 1)
#define PS      (1ull << 7)
#define NX      (1ull << 63)

static _Alignas(4096) u64 root[512];
static _Alignas(4096) u64 boot_pdpt[512];
static _Alignas(4096) u64 boot_pd[512];
static _Alignas(4096) unsigned char allocated_pages[24][4096];
static unsigned allocated_count;
static unsigned char allocated_used[24];
static int interrupts_enabled;
static unsigned failures;

#define CHECK(expr, msg) do { \
    if (!(expr)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); failures++; } \
} while (0)

void *pmm_alloc_page(void) {
    for (unsigned i = 0; i < 24u; ++i) {
        if (!allocated_used[i]) {
            allocated_used[i] = 1;
            allocated_count++;
            memset(allocated_pages[i], 0, 4096);
            return allocated_pages[i];
        }
    }
    return 0;
}
int pmm_free_page(void *page) {
    for (unsigned i = 0; i < 24u; ++i) {
        if (page == allocated_pages[i] && allocated_used[i]) {
            allocated_used[i] = 0;
            return 0;
        }
    }
    return -1;
}
static unsigned used_pages(void) {
    unsigned count = 0;
    for (unsigned i = 0; i < 24u; ++i) count += allocated_used[i] != 0;
    return count;
}
int pmm_is_initialized(void) { return 1; }
u64 cpu_read_flags(void) { return interrupts_enabled ? (1ull << 9) : 0; }
void cpu_interrupt_disable(void) { interrupts_enabled = 0; }
void cpu_interrupt_enable(void) { interrupts_enabled = 1; }

static void setup_boot_tables(void) {
    memset(root, 0, sizeof(root));
    memset(boot_pdpt, 0, sizeof(boot_pdpt));
    memset(boot_pd, 0, sizeof(boot_pd));
    memset(allocated_pages, 0, sizeof(allocated_pages));
    allocated_count = 0;
    memset(allocated_used, 0, sizeof(allocated_used));
    interrupts_enabled = 1;
    root[0] = ((u64)(usize)boot_pdpt & 0x000FFFFFFFFFF000ull) | PRESENT | WRITE;
    boot_pdpt[0] = ((u64)(usize)boot_pd & 0x000FFFFFFFFFF000ull) | PRESENT | WRITE;
    boot_pd[0] = PRESENT | WRITE | PS; /* Bootstrap identity 2 MiB mapping. */
    boot_pdpt[1] = 0x40000000ull | PRESENT | WRITE | PS; /* 1 GiB leaf. */
}

static void test_identity_and_2m_split(void) {
    vmm_mapping_t map;
    vmm_stats_t stats;

    setup_boot_tables();
    CHECK(paging_test_init(root, 1) == VMM_OK, "test root initializes");
    CHECK(vmm_query(0x1234, &map) == VMM_OK, "query sees original 2 MiB identity page");
    CHECK(map.physical_address == 0x1234 && map.page_size == VMM_HUGE_2M_SIZE,
          "2 MiB translation includes correct page offset");

    CHECK(vmm_remap_page(0x1000, 0x900000, VMM_WRITE | VMM_NO_EXECUTE) == VMM_OK,
          "mapping a 4 KiB page splits an existing 2 MiB page");
    CHECK(vmm_query(0x1000, &map) == VMM_OK && map.physical_address == 0x900000,
          "new 4 KiB mapping points to requested physical page");
    CHECK(map.page_size == VMM_PAGE_SIZE && (map.flags & NX),
          "new leaf has 4 KiB granularity and NX permission");
    CHECK(vmm_query(0x2000, &map) == VMM_OK && map.physical_address == 0x2000,
          "splitting huge page preserves neighboring identity mapping");
    CHECK(!(map.flags & NX), "splitting does not apply new NX flag to sibling pages");
    CHECK(vmm_map_page(0x1000, 0xA00000, VMM_WRITE | VMM_NO_EXECUTE) == VMM_ERR_EXISTS,
          "existing mapping cannot be silently overwritten");

    CHECK(vmm_remap_page(0x3000, 0xA00000, VMM_USER | VMM_WRITE | VMM_NO_EXECUTE) == VMM_OK,
          "user page can be created under supervisor identity map");
    CHECK(vmm_query(0x3000, &map) == VMM_OK && (map.flags & (1ull << 2)),
          "requested leaf has user permission");
    CHECK(vmm_query(0x2000, &map) == VMM_OK && !(map.flags & (1ull << 2)),
          "neighboring identity leaf remains supervisor-only");
    CHECK(vmm_protect_page(0x3000, VMM_USER | VMM_NO_EXECUTE) == VMM_OK,
          "page protection can remove write permission");
    CHECK(vmm_query(0x3000, &map) == VMM_OK && !(map.flags & WRITE) && (map.flags & NX),
          "protection update is visible in the leaf flags");
    CHECK(vmm_unmap_page(0x3000, &map) == VMM_OK,
          "page can be unmapped without freeing its frame");
    CHECK(map.physical_address == 0xA00000 && vmm_query(0x3000, &map) == VMM_ERR_NOT_MAPPED,
          "unmap returns old frame and removes translation");
    CHECK(vmm_get_stats(&stats) == VMM_OK && stats.page_table_pages >= 1 &&
          stats.mappings_replaced == 2 && stats.mappings_removed == 1 &&
          stats.failed_operations >= 1,
          "page-table counters track map, unmap, split allocation and rejected operations");
}

static void test_1g_split_and_high_mapping(void) {
    vmm_mapping_t map;
    CHECK(vmm_query(0x40001234, &map) == VMM_OK &&
          map.page_size == VMM_HUGE_1G_SIZE && map.physical_address == 0x40001234,
          "query recognizes a 1 GiB large-page mapping");
    CHECK(vmm_remap_page(0x40001000, 0xB00000, VMM_WRITE | VMM_NO_EXECUTE) == VMM_OK,
          "mapping inside a 1 GiB page splits it through 2 MiB and 4 KiB levels");
    CHECK(vmm_query(0x40001000, &map) == VMM_OK && map.physical_address == 0xB00000,
          "1 GiB mapping split installs the requested leaf");
    CHECK(vmm_query(0x40002000, &map) == VMM_OK && map.physical_address == 0x40002000,
          "1 GiB split preserves adjacent physical translation");

    CHECK(vmm_map_page(0x0000400000000000ull, 0xC00000, VMM_WRITE | VMM_NO_EXECUTE) == VMM_OK,
          "missing high-half hierarchy can be allocated on demand");
    CHECK(vmm_query(0x0000400000000123ull, &map) == VMM_OK &&
          map.physical_address == 0xC00123 && map.page_size == VMM_PAGE_SIZE,
          "high virtual mapping query includes within-page offset");
    {
        unsigned pages_before = used_pages();
        CHECK(vmm_unmap_page(0x0000400000000000ull, &map) == VMM_OK &&
              map.physical_address == 0xC00000,
              "high virtual mapping can be unmapped without releasing its data frame");
        CHECK(vmm_query(0x0000400000000000ull, &map) == VMM_ERR_NOT_MAPPED,
              "unmapping the last leaf removes the translation");
        CHECK(used_pages() + 3u == pages_before,
              "empty leaf/intermediate page tables are reclaimed to PMM");
    }
}

static void test_validation_and_nx_policy(void) {
    vmm_mapping_t map;
    CHECK(vmm_map_page(0x1001, 0xD00000, VMM_WRITE) == VMM_ERR_ARGUMENT,
          "unaligned virtual address is rejected");
    CHECK(vmm_map_page(0x800000000000ull, 0xD00000, VMM_WRITE) == VMM_ERR_ARGUMENT,
          "noncanonical virtual address is rejected");
    CHECK(vmm_map_page(0x5000, 0xD00001, VMM_WRITE | VMM_NO_EXECUTE) == VMM_ERR_ARGUMENT,
          "unaligned physical address is rejected");
    CHECK(vmm_map_page(0x9000, 0xD00000, VMM_WRITE) == VMM_ERR_PERMISSION,
          "writable executable mappings are rejected when NX is available");
    {
        unsigned before = allocated_count;
        CHECK(vmm_unmap_page(0x500000000000ull, 0) == VMM_ERR_NOT_MAPPED,
              "unmapping an absent high virtual address reports not mapped");
        CHECK(allocated_count == before,
              "unmapping an absent address does not allocate empty page tables");
    }
    CHECK(vmm_query(0x5000, 0) == VMM_ERR_ARGUMENT,
          "null query output is rejected");

    setup_boot_tables();
    CHECK(paging_test_init(root, 0) == VMM_OK, "NX-disabled mode initializes");
    CHECK(vmm_map_page(0x8000, 0xD00000, VMM_NO_EXECUTE) == VMM_ERR_UNSUPPORTED,
          "NX mapping is rejected when CPU does not advertise NX");
    CHECK(vmm_protect_page(0x8000, VMM_NO_EXECUTE) == VMM_ERR_UNSUPPORTED,
          "NX protection is rejected when NX is unavailable");
    CHECK(vmm_query(0x8000, &map) == VMM_OK && map.physical_address == 0x8000,
          "rejected NX operation leaves the bootstrap identity mapping unchanged");
}

static void test_oom_rollback(void) {
    vmm_stats_t stats;
    setup_boot_tables();
    memset(root, 0, sizeof(root));
    for (unsigned i = 0; i < 23u; ++i) allocated_used[i] = 1;
    CHECK(paging_test_init(root, 1) == VMM_OK, "empty root initializes for OOM test");
    CHECK(vmm_map_page(0x0000400000000000ull, 0xD00000,
                       VMM_WRITE | VMM_NO_EXECUTE) == VMM_ERR_NOMEM,
          "mapping fails cleanly when page-table allocation runs out");
    CHECK(root[128] == 0 && used_pages() == 23u,
          "failed hierarchy creation rolls back its newly allocated empty table");
    CHECK(vmm_get_stats(&stats) == VMM_OK && stats.page_table_pages == 0,
          "OOM rollback leaves no tracked page-table leak");
}

int main(void) {
    test_identity_and_2m_split();
    test_1g_split_and_high_mapping();
    test_validation_and_nx_policy();
    test_oom_rollback();
    if (failures) {
        fprintf(stderr, "%u paging test(s) failed.\n", failures);
        return 1;
    }
    puts("paging tests passed: CR3 abstraction, 1 GiB/2 MiB splits, 4 KiB map/protect/unmap, canonical checks, permissions, NX policy, high mappings, OOM rollback and statistics.");
    return 0;
}
