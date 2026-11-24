#include "heap_diag.h"
#include "memory.h"

int memory_diagnostics(heap_diagnostics_t *result) {
    usize used = memory_used();
    usize free_bytes = memory_available();
    usize total = memory_capacity();
    int healthy = memory_validate() && used <= total && free_bytes <= total - used;
    if (result) {
        result->used_bytes = used;
        result->free_bytes = free_bytes;
        result->metadata_bytes = healthy ? total - used - free_bytes : 0;
        result->total_bytes = total;
        result->largest_free_block = memory_largest_free_block();
        result->block_count = memory_block_count();
        result->live_allocations = memory_allocation_count();
        result->total_allocations = memory_total_allocations();
        result->failed_allocations = memory_failed_allocations();
        result->invalid_frees = memory_invalid_frees();
        result->healthy = healthy;
    }
    return healthy ? 0 : -1;
}

int memory_is_healthy(void) {
    return memory_diagnostics(0) == 0;
}
