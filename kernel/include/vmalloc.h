#ifndef DENOS_VMALLOC_H
#define DENOS_VMALLOC_H

#include "types.h"

#define VMALLOC_PAGE_SIZE       4096ull
#define VMALLOC_BASE            0xFFFF900000000000ull
#define VMALLOC_PAGE_COUNT      16384u
#define VMALLOC_CAPACITY_BYTES  ((u64)VMALLOC_PAGE_COUNT * VMALLOC_PAGE_SIZE)

#define VMALLOC_OK               0
#define VMALLOC_ERR_ARGUMENT    -1
#define VMALLOC_ERR_NOT_READY   -2
#define VMALLOC_ERR_NOMEM       -3
#define VMALLOC_ERR_NOT_FOUND   -4
#define VMALLOC_ERR_CORRUPT     -5
#define VMALLOC_ERR_PERMISSION  -6

/* Statistics cover the dedicated 64 MiB kernel virtual allocation window. */
typedef struct vmalloc_stats {
    u64 total_bytes;
    u64 used_bytes;
    u64 free_bytes;
    u64 live_allocations;
    u64 allocation_failures;
    u64 pages_mapped;
    u64 quarantined_allocations;
    u64 quarantined_bytes;
} vmalloc_stats_t;

/* Initialize after PMM and paging_init. Safe to call once per boot. */
int vmalloc_init(void);
int vmalloc_is_initialized(void);

/* Allocate a contiguous virtual range backed by individually allocated 4 KiB
 * physical frames. Flags use the VMM_* permission/cache constants. User
 * mappings are deliberately rejected: this allocator owns kernel memory. */
void *kvalloc(usize size, u64 flags);
/* Release a complete allocation returned by kvalloc. Data frames are returned
 * to PMM only after all mappings have been successfully removed. */
int kvfree(void *address);
int vmalloc_get_stats(vmalloc_stats_t *stats);

#endif
