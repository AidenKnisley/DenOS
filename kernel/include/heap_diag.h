#ifndef DENOS_HEAP_DIAG_H
#define DENOS_HEAP_DIAG_H

#include "types.h"

typedef struct heap_diagnostics {
    usize used_bytes;
    usize free_bytes;
    usize metadata_bytes;
    usize total_bytes;
    usize largest_free_block;
    usize block_count;
    usize live_allocations;
    usize total_allocations;
    usize failed_allocations;
    usize invalid_frees;
    int healthy;
} heap_diagnostics_t;

int memory_diagnostics(heap_diagnostics_t *result);
int memory_is_healthy(void);

#endif
