#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "vmalloc.h"
#include "paging.h"
#include "pmm.h"
#include "cpu.h"

#define MAX_FAKE_FRAMES 256u
#define PRESENT 1u
#define TEST_ALLOCS 128u

typedef struct fake_frame {
    u64 physical;
    int allocated;
} fake_frame_t;

static fake_frame_t frames[MAX_FAKE_FRAMES];
static u64 mapped_frames[VMALLOC_PAGE_COUNT];
static u64 mapped_flags[VMALLOC_PAGE_COUNT];
static unsigned frame_cursor;
static unsigned map_calls;
static unsigned unmap_calls;
static unsigned alloc_calls;
static unsigned free_calls;
static int fail_alloc_after = -1;
static int fail_map_after = -1;
static int fail_unmap_after = -1;
static int interrupts_enabled = 1;
static unsigned failures;

#define CHECK(expr, msg) do { \
    if (!(expr)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); failures++; } \
} while (0)

int paging_is_initialized(void) { return 1; }
int pmm_is_initialized(void) { return 1; }
int pmm_page_is_allocated(const void *page) {
    u64 physical = (u64)(uintptr_t)page;
    for (unsigned i = 0; i < MAX_FAKE_FRAMES; ++i)
        if (frames[i].physical == physical && frames[i].allocated) return 1;
    return 0;
}
u64 cpu_read_flags(void) { return interrupts_enabled ? (1ull << 9) : 0; }
void cpu_interrupt_disable(void) { interrupts_enabled = 0; }
void cpu_interrupt_enable(void) { interrupts_enabled = 1; }

void *pmm_alloc_page(void) {
    if (fail_alloc_after >= 0 && (int)alloc_calls >= fail_alloc_after) return NULL;
    for (unsigned i = 0; i < MAX_FAKE_FRAMES; ++i) {
        unsigned slot = (frame_cursor + i) % MAX_FAKE_FRAMES;
        if (!frames[slot].allocated) {
            frames[slot].physical = 0x100000ull + (u64)slot * VMALLOC_PAGE_SIZE;
            frames[slot].allocated = 1;
            frame_cursor = (slot + 1u) % MAX_FAKE_FRAMES;
            alloc_calls++;
            return (void *)(uintptr_t)frames[slot].physical;
        }
    }
    return NULL;
}

int pmm_free_page(void *page) {
    u64 physical = (u64)(uintptr_t)page;
    for (unsigned i = 0; i < MAX_FAKE_FRAMES; ++i) {
        if (frames[i].physical == physical && frames[i].allocated) {
            frames[i].allocated = 0;
            free_calls++;
            return 0;
        }
    }
    return -1;
}

int vmm_map_page(u64 virtual_address, u64 physical_address, u64 flags) {
    u64 offset;
    if (fail_map_after >= 0 && (int)map_calls >= fail_map_after) return VMM_ERR_NOMEM;
    if (virtual_address < VMALLOC_BASE ||
        virtual_address >= VMALLOC_BASE + VMALLOC_CAPACITY_BYTES ||
        ((virtual_address - VMALLOC_BASE) & (VMALLOC_PAGE_SIZE - 1u)))
        return VMM_ERR_ARGUMENT;
    offset = (virtual_address - VMALLOC_BASE) / VMALLOC_PAGE_SIZE;
    if (mapped_frames[offset]) return VMM_ERR_EXISTS;
    mapped_frames[offset] = physical_address;
    mapped_flags[offset] = flags | PRESENT;
    map_calls++;
    return VMM_OK;
}

int vmm_remap_page(u64 virtual_address, u64 physical_address, u64 flags) {
    u64 offset;
    if (virtual_address < VMALLOC_BASE ||
        virtual_address >= VMALLOC_BASE + VMALLOC_CAPACITY_BYTES ||
        ((virtual_address - VMALLOC_BASE) & (VMALLOC_PAGE_SIZE - 1u)))
        return VMM_ERR_ARGUMENT;
    offset = (virtual_address - VMALLOC_BASE) / VMALLOC_PAGE_SIZE;
    if (mapped_frames[offset]) return VMM_ERR_EXISTS;
    mapped_frames[offset] = physical_address;
    mapped_flags[offset] = flags | PRESENT;
    return VMM_OK;
}

int vmm_query(u64 virtual_address, vmm_mapping_t *mapping) {
    u64 offset;
    if (!mapping || virtual_address < VMALLOC_BASE ||
        virtual_address >= VMALLOC_BASE + VMALLOC_CAPACITY_BYTES)
        return VMM_ERR_ARGUMENT;
    offset = (virtual_address - VMALLOC_BASE) / VMALLOC_PAGE_SIZE;
    if (!mapped_frames[offset]) return VMM_ERR_NOT_MAPPED;
    mapping->physical_address = mapped_frames[offset] +
        (virtual_address & (VMALLOC_PAGE_SIZE - 1u));
    mapping->flags = mapped_flags[offset];
    mapping->page_size = VMALLOC_PAGE_SIZE;
    return VMM_OK;
}

int vmm_unmap_page(u64 virtual_address, vmm_mapping_t *old_mapping) {
    u64 offset;
    if (fail_unmap_after >= 0 && (int)unmap_calls >= fail_unmap_after)
        return VMM_ERR_CORRUPT;
    if (virtual_address < VMALLOC_BASE ||
        virtual_address >= VMALLOC_BASE + VMALLOC_CAPACITY_BYTES)
        return VMM_ERR_ARGUMENT;
    offset = (virtual_address - VMALLOC_BASE) / VMALLOC_PAGE_SIZE;
    if (!mapped_frames[offset]) return VMM_ERR_NOT_MAPPED;
    if (old_mapping) {
        old_mapping->physical_address = mapped_frames[offset];
        old_mapping->flags = mapped_flags[offset];
        old_mapping->page_size = VMALLOC_PAGE_SIZE;
    }
    mapped_frames[offset] = 0;
    mapped_flags[offset] = 0;
    unmap_calls++;
    return VMM_OK;
}

static unsigned live_fake_frames(void) {
    unsigned count = 0;
    for (unsigned i = 0; i < MAX_FAKE_FRAMES; ++i) count += frames[i].allocated != 0;
    return count;
}

static unsigned live_fake_mappings(void) {
    unsigned count = 0;
    for (unsigned i = 0; i < VMALLOC_PAGE_COUNT; ++i) count += mapped_frames[i] != 0;
    return count;
}

static void reset_fault_injection(void) {
    fail_alloc_after = -1;
    fail_map_after = -1;
    alloc_calls = 0;
    map_calls = 0;
}

static void test_basic_lifecycle(void) {
    vmalloc_stats_t stats;
    void *memory;
    CHECK(vmalloc_init() == VMALLOC_OK, "vmalloc initializes after PMM and VMM");
    CHECK(vmalloc_init() == VMALLOC_OK, "initialization is idempotent");
    memory = kvalloc(5000, VMM_WRITE | VMM_NO_EXECUTE);
    CHECK(memory == (void *)(uintptr_t)VMALLOC_BASE, "first allocation uses the kernel virtual window base");
    CHECK(live_fake_frames() == 2 && live_fake_mappings() == 2,
          "two page frames are allocated and mapped for a 5000-byte request");
    CHECK(vmalloc_get_stats(&stats) == VMALLOC_OK && stats.used_bytes == 8192 &&
          stats.free_bytes == VMALLOC_CAPACITY_BYTES - 8192 && stats.live_allocations == 1,
          "statistics account for page-rounded allocation size");
    CHECK(kvfree(memory) == VMALLOC_OK, "allocation can be unmapped and returned to PMM");
    CHECK(live_fake_frames() == 0 && live_fake_mappings() == 0,
          "free releases both virtual mappings and physical frames");
    CHECK(kvfree(memory) == VMALLOC_ERR_NOT_FOUND, "double free is rejected");
    CHECK(vmalloc_get_stats(&stats) == VMALLOC_OK && stats.used_bytes == 0 &&
          stats.live_allocations == 0 && stats.pages_mapped == 0,
          "statistics return to empty after free");
}

static void test_validation_and_ownership(void) {
    void *memory = kvalloc(4096, VMM_WRITE | VMM_NO_EXECUTE);
    vmm_mapping_t original;
    u64 slot = ((u64)(uintptr_t)memory - VMALLOC_BASE) / VMALLOC_PAGE_SIZE;
    CHECK(memory != NULL, "single-page allocation succeeds");
    CHECK(kvfree((void *)((uintptr_t)memory + 1u)) == VMALLOC_ERR_ARGUMENT,
          "unaligned free is rejected");
    CHECK(kvfree((void *)((uintptr_t)memory + VMALLOC_PAGE_SIZE)) == VMALLOC_ERR_NOT_FOUND,
          "interior page pointer cannot free an allocation");
    CHECK(vmm_query(VMALLOC_BASE + slot * VMALLOC_PAGE_SIZE, &original) == VMM_OK,
          "allocation mapping can be inspected");
    mapped_frames[slot] += VMALLOC_PAGE_SIZE;
    CHECK(kvfree(memory) == VMALLOC_ERR_CORRUPT && live_fake_frames() == 1,
          "changed mapping is detected and physical frame ownership is retained");
    mapped_frames[slot] = original.physical_address;
    CHECK(kvfree(memory) == VMALLOC_OK, "restored mapping can be safely released");
    CHECK(kvalloc(0, VMM_WRITE | VMM_NO_EXECUTE) == NULL,
          "zero-size allocation is rejected");
    CHECK(kvalloc(4096, VMM_USER | VMM_NO_EXECUTE) == NULL,
          "kernel virtual allocator rejects user mappings");
    CHECK(kvalloc((usize)VMALLOC_CAPACITY_BYTES + 1u, VMM_NO_EXECUTE) == NULL,
          "request larger than the virtual window is rejected");
}

static void test_allocation_failure_rollback(void) {
    vmalloc_stats_t stats;
    void *memory;
    reset_fault_injection();
    fail_alloc_after = 1;
    memory = kvalloc(3u * 4096u, VMM_WRITE | VMM_NO_EXECUTE);
    CHECK(memory == NULL, "PMM exhaustion fails a multi-page allocation");
    CHECK(live_fake_frames() == 0 && live_fake_mappings() == 0,
          "PMM exhaustion rolls back frames and mappings");
    CHECK(vmalloc_get_stats(&stats) == VMALLOC_OK && stats.used_bytes == 0,
          "PMM exhaustion releases the entire reserved virtual run");

    reset_fault_injection();
    fail_map_after = 1;
    memory = kvalloc(3u * 4096u, VMM_WRITE | VMM_NO_EXECUTE);
    CHECK(memory == NULL, "page-table mapping failure aborts allocation");
    CHECK(live_fake_frames() == 0 && live_fake_mappings() == 0,
          "mapping failure returns every allocated frame");
    CHECK(vmalloc_get_stats(&stats) == VMALLOC_OK && stats.used_bytes == 0,
          "mapping failure releases all reserved virtual bits");

    reset_fault_injection();
    memory = kvalloc(3u * 4096u, VMM_WRITE | VMM_NO_EXECUTE);
    CHECK(memory != NULL, "virtual range remains reusable after failed allocations");
    CHECK(kvfree(memory) == VMALLOC_OK, "reused allocation frees correctly");
}

static void test_window_exhaustion_and_reuse(void) {
    void *allocations[TEST_ALLOCS];
    vmalloc_stats_t stats;
    for (unsigned i = 0; i < TEST_ALLOCS; ++i) {
        allocations[i] = kvalloc(4096, VMM_WRITE | VMM_NO_EXECUTE);
        CHECK(allocations[i] != NULL, "allocation descriptor capacity is usable");
    }
    CHECK(kvalloc(4096, VMM_WRITE | VMM_NO_EXECUTE) == NULL,
          "allocation descriptor limit fails cleanly");
    for (unsigned i = 0; i < TEST_ALLOCS; ++i)
        CHECK(kvfree(allocations[i]) == VMALLOC_OK, "all tracked allocations can be released");
    CHECK(vmalloc_get_stats(&stats) == VMALLOC_OK && stats.used_bytes == 0 &&
          stats.live_allocations == 0 && stats.free_bytes == VMALLOC_CAPACITY_BYTES,
          "full allocation/release cycle restores all virtual capacity");
    CHECK(live_fake_frames() == 0 && live_fake_mappings() == 0,
          "stress cycle leaves no physical frames or virtual mappings behind");
}



static void test_unmap_failure_restores_mapping(void) {
    void *memory = kvalloc(2u * 4096u, VMM_WRITE | VMM_NO_EXECUTE);
    u64 first_slot;
    vmalloc_stats_t stats;
    CHECK(memory != NULL, "two-page allocation for unmap rollback succeeds");
    first_slot = ((u64)(uintptr_t)memory - VMALLOC_BASE) / VMALLOC_PAGE_SIZE;
    unmap_calls = 0;
    fail_unmap_after = 1;
    CHECK(kvfree(memory) == VMALLOC_ERR_CORRUPT,
          "partial unmap failure is reported");
    CHECK(mapped_frames[first_slot] != 0 && mapped_frames[first_slot + 1u] != 0 &&
          live_fake_frames() == 2,
          "previously unmapped pages are restored before returning on failure");
    CHECK(vmalloc_get_stats(&stats) == VMALLOC_OK && stats.live_allocations == 1 &&
          stats.used_bytes == 2u * VMALLOC_PAGE_SIZE,
          "allocation remains live and accounted after successful rollback");
    fail_unmap_after = -1;
    CHECK(kvfree(memory) == VMALLOC_OK, "restored allocation can be freed on retry");
}

static void test_rollback_quarantine(void) {
    vmalloc_stats_t stats;
    void *failed;
    void *next;
    reset_fault_injection();
    unmap_calls = 0;
    fail_map_after = 1;
    fail_unmap_after = 0;
    failed = kvalloc(2u * 4096u, VMM_WRITE | VMM_NO_EXECUTE);
    CHECK(failed == NULL, "allocation failure returns no pointer when rollback unmap fails");
    CHECK(live_fake_frames() == 1 && live_fake_mappings() == 1,
          "failed rollback never frees a frame still referenced by a PTE");
    CHECK(vmalloc_get_stats(&stats) == VMALLOC_OK && stats.quarantined_allocations == 1 &&
          stats.quarantined_bytes == 2u * VMALLOC_PAGE_SIZE &&
          stats.used_bytes == 2u * VMALLOC_PAGE_SIZE,
          "failed rollback quarantines virtual capacity and reports it");
    CHECK(kvfree((void *)(uintptr_t)VMALLOC_BASE) == VMALLOC_ERR_CORRUPT,
          "quarantined address cannot be released as an ordinary allocation");

    fail_unmap_after = -1;
    reset_fault_injection();
    next = kvalloc(4096, VMM_WRITE | VMM_NO_EXECUTE);
    CHECK(next == (void *)(uintptr_t)(VMALLOC_BASE + 2u * VMALLOC_PAGE_SIZE),
          "allocator skips quarantined ranges instead of aliasing them");
    CHECK(kvfree(next) == VMALLOC_OK, "allocation adjacent to quarantine remains usable");
}

int main(void) {
    memset(frames, 0, sizeof(frames));
    memset(mapped_frames, 0, sizeof(mapped_frames));
    memset(mapped_flags, 0, sizeof(mapped_flags));
    test_basic_lifecycle();
    test_validation_and_ownership();
    test_allocation_failure_rollback();
    test_window_exhaustion_and_reuse();
    test_unmap_failure_restores_mapping();
    test_rollback_quarantine();
    if (interrupts_enabled != 1) { fprintf(stderr, "FAIL: interrupt state restored\n"); failures++; }
    if (failures) {
        fprintf(stderr, "vmalloc tests failed: %u\n", failures);
        return 1;
    }
    printf("vmalloc tests passed: multi-page allocation, accounting, pointer ownership, corruption detection, OOM/map rollback, rollback quarantine, recoverable unmap failure, exhaustion, reuse and full cleanup.\n");
    return 0;
}
