#ifndef DENOS_PAGING_H
#define DENOS_PAGING_H

#include "types.h"

#define VMM_PAGE_SIZE       4096ull
#define VMM_HUGE_2M_SIZE    (2ull * 1024ull * 1024ull)
#define VMM_HUGE_1G_SIZE    (1024ull * 1024ull * 1024ull)

/* Leaf permission/cache flags. PRESENT is always set by the mapper. With NX
 * available, writable mappings must also be marked NO_EXECUTE (W^X policy). */
#define VMM_WRITE            (1ull << 1)
#define VMM_USER             (1ull << 2)
#define VMM_WRITE_THROUGH    (1ull << 3)
#define VMM_CACHE_DISABLE    (1ull << 4)
#define VMM_GLOBAL           (1ull << 8)
#define VMM_NO_EXECUTE       (1ull << 63)

#define VMM_OK               0
#define VMM_ERR_ARGUMENT    -1
#define VMM_ERR_NOT_READY   -2
#define VMM_ERR_NOMEM       -3
#define VMM_ERR_EXISTS      -4
#define VMM_ERR_NOT_MAPPED  -5
#define VMM_ERR_UNSUPPORTED -6
#define VMM_ERR_CORRUPT     -7
#define VMM_ERR_PERMISSION  -8

typedef struct vmm_mapping {
    u64 physical_address; /* Includes the offset within the mapped page. */
    u64 flags;            /* Hardware flags, without the physical address. */
    u64 page_size;        /* 4 KiB, 2 MiB, or 1 GiB. */
} vmm_mapping_t;

typedef struct vmm_stats {
    u64 page_table_pages;
    u64 mappings_created;
    u64 mappings_removed;
    u64 mappings_replaced;
    u64 failed_operations;
    u8 initialized;
    u8 nx_enabled;
    u8 write_protect_enabled;
    u8 global_pages_enabled;
    u8 physical_address_bits;
} vmm_stats_t;

/* Attach to the active CR3 root. Does not discard the bootstrap identity map. */
int paging_init(void);
int paging_is_initialized(void);
int paging_nx_enabled(void);

/* Virtual and physical addresses must be 4 KiB aligned. Mapping ownership
 * remains with the caller: unmapping never frees a physical frame. */
int vmm_map_page(u64 virtual_address, u64 physical_address, u64 flags);
/* Explicitly replace an existing leaf mapping; unlike map_page this is never
 * done implicitly. The old physical frame is not freed. */
int vmm_remap_page(u64 virtual_address, u64 physical_address, u64 flags);
int vmm_unmap_page(u64 virtual_address, vmm_mapping_t *old_mapping);
int vmm_protect_page(u64 virtual_address, u64 flags);
int vmm_query(u64 virtual_address, vmm_mapping_t *mapping);
int vmm_get_stats(vmm_stats_t *stats);

#ifdef DENOS_PAGING_TESTING
int paging_test_init(u64 *root, int nx_supported);
#endif

#endif
