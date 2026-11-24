#ifndef DENOS_PMM_H
#define DENOS_PMM_H

#include "types.h"

#define PMM_PAGE_SIZE       4096u
#define PMM_ADDRESS_LIMIT   (1ull << 30)
#define PMM_BOOT_MAGIC      0x36D76289u

typedef struct pmm_stats {
    u64 managed_pages;
    u64 free_pages;
    u64 allocated_pages;
    u64 reserved_pages;
    u32 memory_map_entries;
    u32 invalid_memory_map_entries;
    int initialized;
} pmm_stats_t;

/*
 * Initialize from the Multiboot2 information structure supplied by GRUB.
 * kernel_start/kernel_end describe the complete linked kernel image.
 * The initial implementation manages only the first identity-mapped GiB.
 */
int pmm_init(u32 boot_magic, const void *multiboot_info,
             u64 kernel_start, u64 kernel_end);

/* Returns a zero-filled, page-aligned identity-mapped physical page or NULL. */
void *pmm_alloc_page(void);

/* Releases a page previously returned by pmm_alloc_page; rejects reserved,
 * unaligned, unmanaged, and already-free addresses. Returns 0 on success. */
int pmm_free_page(void *page);
/* Non-mutating ownership check for a frame returned by pmm_alloc_page(). */
int pmm_page_is_allocated(const void *page);

int pmm_get_stats(pmm_stats_t *stats);
int pmm_is_initialized(void);
u64 pmm_address_limit(void);

#endif
