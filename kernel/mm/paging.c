#include "paging.h"
#include "pmm.h"
#include "cpu.h"
#include "kstring.h"

#define PTE_PRESENT       (1ull << 0)
#define PTE_WRITE         (1ull << 1)
#define PTE_USER          (1ull << 2)
#define PTE_PWT           (1ull << 3)
#define PTE_PCD           (1ull << 4)
#define PTE_ACCESSED      (1ull << 5)
#define PTE_DIRTY         (1ull << 6)
#define PTE_PS_PAT        (1ull << 7)
#define PTE_GLOBAL        (1ull << 8)
#define PTE_PAT_LARGE     (1ull << 12)
#define PTE_NX            (1ull << 63)
#define PTE_ADDRESS_MASK  0x000FFFFFFFFFF000ull
#define PDE_2M_MASK       0x000FFFFFFFE00000ull
#define PDPTE_1G_MASK     0x000FFFFFC0000000ull
#define EFER_MSR          0xC0000080u
#define EFER_NXE          (1u << 11)
#define RFLAGS_IF         (1ull << 9)

#define LEAF_FLAGS (PTE_PRESENT | PTE_WRITE | PTE_USER | PTE_PWT | \
                   PTE_PCD | PTE_ACCESSED | PTE_DIRTY | PTE_GLOBAL | PTE_NX)
#define LARGE_FLAGS (LEAF_FLAGS | PTE_PAT_LARGE)

static u64 *root_table;
static u64 physical_address_mask = PTE_ADDRESS_MASK;
static vmm_stats_t statistics;

static int address_is_canonical(u64 address) {
    u64 upper = address >> 48;
    return ((address >> 47) & 1u) ? upper == 0xFFFFu : upper == 0u;
}

static int page_address_valid(u64 address) {
    return address_is_canonical(address) &&
           (address & (VMM_PAGE_SIZE - 1u)) == 0u;
}

static u64 irq_save(void) {
    u64 flags = cpu_read_flags();
    cpu_interrupt_disable();
    return flags;
}

static void irq_restore(u64 flags) {
    if (flags & RFLAGS_IF) cpu_interrupt_enable();
}

static void invalidate_page(u64 address) {
#ifndef DENOS_PAGING_TESTING
    __asm__ volatile("invlpg (%0)" : : "r"((usize)address) : "memory");
#else
    (void)address;
#endif
}

static int entry_address_valid(u64 entry) {
    return ((entry & PTE_ADDRESS_MASK) & ~physical_address_mask) == 0;
}

static u64 *table_pointer(u64 entry) {
    return (u64 *)(usize)(entry & PTE_ADDRESS_MASK);
}

static u8 detect_physical_address_bits(void) {
#ifndef DENOS_PAGING_TESTING
    u32 a, b, c, d;
    u32 maximum;
    u8 bits = 36; /* Architectural x86-64 baseline when the leaf is absent. */
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(0x80000000u), "c"(0));
    maximum = a;
    if (maximum >= 0x80000008u) {
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                         : "a"(0x80000008u), "c"(0));
        bits = (u8)(a & 0xFFu);
        if (bits < 32u) bits = 36u;
        if (bits > 52u) bits = 52u;
    }
    return bits;
#else
    return 52;
#endif
}

static u64 physical_mask_from_bits(u8 bits) {
    if (bits >= 52u) return PTE_ADDRESS_MASK;
    if (bits <= 12u) return 0;
    return (((1ull << bits) - 1u) & PTE_ADDRESS_MASK);
}

static void discard_empty_child(u64 *parent_entry) {
    u64 entry;
    u64 *table;
    if (!parent_entry) return;
    entry = *parent_entry;
    if (!(entry & PTE_PRESENT) || (entry & PTE_PS_PAT)) return;
    table = table_pointer(entry);
    for (usize i = 0; i < 512u; ++i)
        if (table[i] != 0) return;
    if (pmm_free_page(table) == 0) {
        *parent_entry = 0;
        if (statistics.page_table_pages) statistics.page_table_pages--;
    }
}

static u64 allocate_table(void) {
    void *page = pmm_alloc_page();
    u64 address;
    if (!page) return 0;
    address = (u64)(usize)page;
    if ((address & (VMM_PAGE_SIZE - 1u)) ||
        (address & ~physical_address_mask)) {
        (void)pmm_free_page(page);
        return 0;
    }
    kmemset(page, 0, (usize)VMM_PAGE_SIZE);
    statistics.page_table_pages++;
    return address;
}

static int enable_write_protect(void) {
#ifndef DENOS_PAGING_TESTING
    u64 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1ull << 16); /* CR0.WP makes supervisor writes obey read-only PTEs. */
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    return 1;
#else
    return 1;
#endif
}

static int enable_global_pages(void) {
#ifndef DENOS_PAGING_TESTING
    u32 a, b, c, d;
    u64 cr4;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(1u), "c"(0));
    if (!(d & (1u << 13))) return 0;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ull << 7); /* CR4.PGE enables PTE.GLOBAL semantics. */
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr4));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr4) : "memory");
    return 1;
#else
    return 1;
#endif
}

static int enable_nx(void) {
#ifndef DENOS_PAGING_TESTING
    u32 a, b, c, d;
    u32 max_extended;
    u32 efer_low, efer_high;

    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(0x80000000u), "c"(0));
    max_extended = a;
    if (max_extended < 0x80000001u) return 0;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(0x80000001u), "c"(0));
    if (!(d & (1u << 20))) return 0;
    __asm__ volatile("rdmsr" : "=a"(efer_low), "=d"(efer_high)
                     : "c"(EFER_MSR));
    efer_low |= EFER_NXE;
    __asm__ volatile("wrmsr" : : "c"(EFER_MSR), "a"(efer_low), "d"(efer_high)
                     : "memory");
    return 1;
#else
    return 0;
#endif
}

static int split_1g(u64 *pdpt, usize index) {
    u64 old_entry = pdpt[index];
    u64 table_address;
    u64 base;
    u64 inherited;
    u64 *pd;

    if (!(old_entry & PTE_PRESENT) || !(old_entry & PTE_PS_PAT))
        return VMM_ERR_CORRUPT;
    table_address = allocate_table();
    if (!table_address) return VMM_ERR_NOMEM;
    pd = (u64 *)(usize)table_address;
    base = old_entry & PDPTE_1G_MASK;
    inherited = old_entry & LARGE_FLAGS;

    for (usize i = 0; i < 512u; ++i)
        pd[i] = (base + (u64)i * VMM_HUGE_2M_SIZE) |
                inherited | PTE_PS_PAT;

    /* The child PDEs now carry the original page permissions. Keep the
       parent permissive so later 4 KiB leaf permissions remain authoritative. */
    pdpt[index] = table_address | PTE_PRESENT | PTE_WRITE | PTE_USER;
    return VMM_OK;
}

static int split_2m(u64 *pd, usize index) {
    u64 old_entry = pd[index];
    u64 table_address;
    u64 base;
    u64 inherited;
    u64 *pt;

    if (!(old_entry & PTE_PRESENT) || !(old_entry & PTE_PS_PAT))
        return VMM_ERR_CORRUPT;
    table_address = allocate_table();
    if (!table_address) return VMM_ERR_NOMEM;
    pt = (u64 *)(usize)table_address;
    base = old_entry & PDE_2M_MASK;
    inherited = old_entry & LEAF_FLAGS;
    if (old_entry & PTE_PAT_LARGE) inherited |= PTE_PS_PAT;

    for (usize i = 0; i < 512u; ++i)
        pt[i] = (base + (u64)i * VMM_PAGE_SIZE) | inherited;

    pd[index] = table_address | PTE_PRESENT | PTE_WRITE | PTE_USER;
    return VMM_OK;
}

/* Return the 4 KiB leaf entry. When create is true, missing tables are
 * allocated and large-page mappings are split without changing sibling pages. */
static int locate_leaf(u64 virtual_address, int create, int want_user,
                       int want_write, u64 **leaf_out) {
    usize pml4_index = (usize)((virtual_address >> 39) & 0x1FFu);
    usize pdpt_index = (usize)((virtual_address >> 30) & 0x1FFu);
    usize pd_index = (usize)((virtual_address >> 21) & 0x1FFu);
    usize pt_index = (usize)((virtual_address >> 12) & 0x1FFu);
    u64 *pdpt, *pd, *pt;
    u64 *pml4_slot = &root_table[pml4_index];
    u64 *pdpt_slot;
    u64 *pd_slot;
    u64 entry;
    u64 address;
    int made_pdpt = 0;
    int made_pd = 0;
    int result;

    if (!root_table || !leaf_out) return VMM_ERR_NOT_READY;

    entry = *pml4_slot;
    if (!(entry & PTE_PRESENT)) {
        if (!create) return VMM_ERR_NOT_MAPPED;
        address = allocate_table();
        if (!address) return VMM_ERR_NOMEM;
        *pml4_slot = address | PTE_PRESENT | PTE_WRITE | PTE_USER;
        entry = *pml4_slot;
        made_pdpt = 1;
    } else {
        if ((entry & PTE_PS_PAT) || !entry_address_valid(entry)) return VMM_ERR_CORRUPT;
        if (want_write && !(entry & PTE_WRITE)) return VMM_ERR_PERMISSION;
        if (want_user) *pml4_slot |= PTE_USER;
    }
    pdpt = table_pointer(entry);

    pdpt_slot = &pdpt[pdpt_index];
    entry = *pdpt_slot;
    if (entry & PTE_PRESENT) {
        if (!entry_address_valid(entry)) return VMM_ERR_CORRUPT;
        if (entry & PTE_PS_PAT) {
            if (!create) { *leaf_out = 0; return VMM_OK; }
            result = split_1g(pdpt, pdpt_index);
            if (result < 0) return result;
            entry = *pdpt_slot;
        }
        if (want_write && !(entry & PTE_WRITE)) return VMM_ERR_PERMISSION;
        if (want_user) *pdpt_slot |= PTE_USER;
    } else {
        if (!create) return VMM_ERR_NOT_MAPPED;
        address = allocate_table();
        if (!address) {
            if (made_pdpt) discard_empty_child(pml4_slot);
            return VMM_ERR_NOMEM;
        }
        *pdpt_slot = address | PTE_PRESENT | PTE_WRITE | PTE_USER;
        entry = *pdpt_slot;
        made_pd = 1;
    }
    pd = table_pointer(entry);

    pd_slot = &pd[pd_index];
    entry = *pd_slot;
    if (entry & PTE_PRESENT) {
        if (!entry_address_valid(entry)) return VMM_ERR_CORRUPT;
        if (entry & PTE_PS_PAT) {
            if (!create) { *leaf_out = 0; return VMM_OK; }
            result = split_2m(pd, pd_index);
            if (result < 0) return result;
            entry = *pd_slot;
        }
        if (want_write && !(entry & PTE_WRITE)) return VMM_ERR_PERMISSION;
        if (want_user) *pd_slot |= PTE_USER;
    } else {
        if (!create) return VMM_ERR_NOT_MAPPED;
        address = allocate_table();
        if (!address) {
            if (made_pd) discard_empty_child(pdpt_slot);
            if (made_pdpt) discard_empty_child(pml4_slot);
            return VMM_ERR_NOMEM;
        }
        *pd_slot = address | PTE_PRESENT | PTE_WRITE | PTE_USER;
        entry = *pd_slot;
    }
    pt = table_pointer(entry);
    *leaf_out = &pt[pt_index];
    return VMM_OK;
}

int paging_init(void) {
    u64 cr3;
    u64 flags = irq_save();
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    cr3 &= PTE_ADDRESS_MASK;
    if (!cr3 || !pmm_is_initialized()) {
        irq_restore(flags);
        return VMM_ERR_NOT_READY;
    }
    kmemset(&statistics, 0, sizeof(statistics));
    statistics.physical_address_bits = detect_physical_address_bits();
    physical_address_mask = physical_mask_from_bits(statistics.physical_address_bits);
    if (!physical_address_mask || (cr3 & ~physical_address_mask)) {
        irq_restore(flags);
        return VMM_ERR_CORRUPT;
    }
    root_table = (u64 *)(usize)cr3;
    statistics.nx_enabled = (u8)enable_nx();
    statistics.write_protect_enabled = (u8)enable_write_protect();
    statistics.global_pages_enabled = (u8)enable_global_pages();
    statistics.initialized = 1;
    irq_restore(flags);
    return VMM_OK;
}

#ifdef DENOS_PAGING_TESTING
int paging_test_init(u64 *root, int nx_supported) {
    if (!root || ((usize)root & (VMM_PAGE_SIZE - 1u))) return VMM_ERR_ARGUMENT;
    root_table = root;
    kmemset(&statistics, 0, sizeof(statistics));
    statistics.physical_address_bits = 52;
    physical_address_mask = PTE_ADDRESS_MASK;
    statistics.nx_enabled = (u8)(nx_supported != 0);
    statistics.write_protect_enabled = 1;
    statistics.global_pages_enabled = 1;
    statistics.initialized = 1;
    return VMM_OK;
}
#endif

int paging_is_initialized(void) { return statistics.initialized != 0; }
int paging_nx_enabled(void) { return statistics.nx_enabled != 0; }

static int map_page_internal(u64 virtual_address, u64 physical_address,
                             u64 flags, int replace) {
    const u64 allowed = VMM_WRITE | VMM_USER | VMM_WRITE_THROUGH |
                        VMM_CACHE_DISABLE | VMM_GLOBAL | VMM_NO_EXECUTE;
    u64 *leaf;
    u64 irq_flags;
    u64 entry_flags = PTE_PRESENT;
    int result;

    if (!page_address_valid(virtual_address) ||
        (physical_address & (VMM_PAGE_SIZE - 1u)) ||
        (physical_address & ~physical_address_mask) || (flags & ~allowed)) {
        statistics.failed_operations++;
        return VMM_ERR_ARGUMENT;
    }
    if ((flags & VMM_NO_EXECUTE) && !statistics.nx_enabled) {
        statistics.failed_operations++;
        return VMM_ERR_UNSUPPORTED;
    }
    if ((flags & VMM_GLOBAL) && !statistics.global_pages_enabled) {
        statistics.failed_operations++;
        return VMM_ERR_UNSUPPORTED;
    }
    if (statistics.nx_enabled && (flags & VMM_WRITE) && !(flags & VMM_NO_EXECUTE)) {
        statistics.failed_operations++;
        return VMM_ERR_PERMISSION; /* Enforce W^X on newly installed writable leaves. */
    }
    if (!statistics.initialized) {
        statistics.failed_operations++;
        return VMM_ERR_NOT_READY;
    }

    if (flags & VMM_WRITE) entry_flags |= PTE_WRITE;
    if (flags & VMM_USER) entry_flags |= PTE_USER;
    if (flags & VMM_WRITE_THROUGH) entry_flags |= PTE_PWT;
    if (flags & VMM_CACHE_DISABLE) entry_flags |= PTE_PCD;
    if (flags & VMM_GLOBAL) entry_flags |= PTE_GLOBAL;
    if (flags & VMM_NO_EXECUTE) entry_flags |= PTE_NX;

    irq_flags = irq_save();
    {
        vmm_mapping_t existing;
        int query_result = vmm_query(virtual_address, &existing);
        if (query_result == VMM_OK && !replace) result = VMM_ERR_EXISTS;
        else if (query_result != VMM_OK && query_result != VMM_ERR_NOT_MAPPED)
            result = query_result;
        else result = locate_leaf(virtual_address, 1, (flags & VMM_USER) != 0,
                                  (flags & VMM_WRITE) != 0, &leaf);
    }
    if (result == VMM_OK) {
        if (*leaf & PTE_PRESENT) {
            if (!replace) result = VMM_ERR_EXISTS;
            else {
                *leaf = physical_address | entry_flags;
                statistics.mappings_replaced++;
                invalidate_page(virtual_address);
            }
        } else {
            *leaf = physical_address | entry_flags;
            statistics.mappings_created++;
            invalidate_page(virtual_address);
        }
    }
    if (result < 0) statistics.failed_operations++;
    irq_restore(irq_flags);
    return result;
}

int vmm_map_page(u64 virtual_address, u64 physical_address, u64 flags) {
    return map_page_internal(virtual_address, physical_address, flags, 0);
}

int vmm_remap_page(u64 virtual_address, u64 physical_address, u64 flags) {
    return map_page_internal(virtual_address, physical_address, flags, 1);
}

static u64 effective_flags(u64 leaf_flags, u64 pml4e, u64 pdpte, u64 pde) {
    if (!(pml4e & PTE_WRITE) || !(pdpte & PTE_WRITE) || !(pde & PTE_WRITE))
        leaf_flags &= ~PTE_WRITE;
    if (!(pml4e & PTE_USER) || !(pdpte & PTE_USER) || !(pde & PTE_USER))
        leaf_flags &= ~PTE_USER;
    if ((pml4e | pdpte | pde) & PTE_NX) leaf_flags |= PTE_NX;
    return leaf_flags;
}

int vmm_query(u64 virtual_address, vmm_mapping_t *mapping) {
    usize pml4_index, pdpt_index, pd_index, pt_index;
    u64 pml4e, pdpte, pde, entry;
    u64 *pdpt, *pd, *pt;
    if (!mapping || !address_is_canonical(virtual_address))
        return VMM_ERR_ARGUMENT;
    if (!statistics.initialized || !root_table) return VMM_ERR_NOT_READY;

    pml4_index = (usize)((virtual_address >> 39) & 0x1FFu);
    pdpt_index = (usize)((virtual_address >> 30) & 0x1FFu);
    pd_index = (usize)((virtual_address >> 21) & 0x1FFu);
    pt_index = (usize)((virtual_address >> 12) & 0x1FFu);

    pml4e = root_table[pml4_index];
    if (!(pml4e & PTE_PRESENT)) return VMM_ERR_NOT_MAPPED;
    if ((pml4e & PTE_PS_PAT) || !entry_address_valid(pml4e)) return VMM_ERR_CORRUPT;
    pdpt = table_pointer(pml4e);
    pdpte = pdpt[pdpt_index];
    if (!(pdpte & PTE_PRESENT)) return VMM_ERR_NOT_MAPPED;
    if (pdpte & PTE_PS_PAT) {
        if (((pdpte & PDPTE_1G_MASK) & ~physical_address_mask) != 0)
            return VMM_ERR_CORRUPT;
        mapping->page_size = VMM_HUGE_1G_SIZE;
        mapping->physical_address = (pdpte & PDPTE_1G_MASK) +
                                    (virtual_address & (VMM_HUGE_1G_SIZE - 1u));
        mapping->flags = effective_flags(pdpte & LARGE_FLAGS, pml4e,
                                         pdpte, pdpte);
        return VMM_OK;
    }
    if (!entry_address_valid(pdpte)) return VMM_ERR_CORRUPT;
    pd = table_pointer(pdpte);
    pde = pd[pd_index];
    if (!(pde & PTE_PRESENT)) return VMM_ERR_NOT_MAPPED;
    if (pde & PTE_PS_PAT) {
        if (((pde & PDE_2M_MASK) & ~physical_address_mask) != 0)
            return VMM_ERR_CORRUPT;
        mapping->page_size = VMM_HUGE_2M_SIZE;
        mapping->physical_address = (pde & PDE_2M_MASK) +
                                    (virtual_address & (VMM_HUGE_2M_SIZE - 1u));
        mapping->flags = effective_flags(pde & LARGE_FLAGS, pml4e, pdpte, pde);
        return VMM_OK;
    }
    if (!entry_address_valid(pde)) return VMM_ERR_CORRUPT;
    pt = table_pointer(pde);
    entry = pt[pt_index];
    if (!(entry & PTE_PRESENT)) return VMM_ERR_NOT_MAPPED;
    if (!entry_address_valid(entry)) return VMM_ERR_CORRUPT;
    mapping->page_size = VMM_PAGE_SIZE;
    mapping->physical_address = (entry & PTE_ADDRESS_MASK) +
                                (virtual_address & (VMM_PAGE_SIZE - 1u));
    mapping->flags = effective_flags(entry & LEAF_FLAGS, pml4e, pdpte, pde);
    return VMM_OK;
}

int vmm_unmap_page(u64 virtual_address, vmm_mapping_t *old_mapping) {
    u64 *leaf;
    u64 irq_flags;
    int result;
    if (!page_address_valid(virtual_address)) return VMM_ERR_ARGUMENT;
    if (!statistics.initialized) return VMM_ERR_NOT_READY;
    irq_flags = irq_save();
    {
        vmm_mapping_t existing;
        result = vmm_query(virtual_address, &existing);
        if (result == VMM_OK && old_mapping) *old_mapping = existing;
        if (result == VMM_OK) result = locate_leaf(virtual_address, 1, 0, 0, &leaf);
    }
    if (result == VMM_OK && (!leaf || !(*leaf & PTE_PRESENT)))
        result = VMM_ERR_NOT_MAPPED;
    if (result == VMM_OK) {
        usize pml4_index = (usize)((virtual_address >> 39) & 0x1FFu);
        usize pdpt_index = (usize)((virtual_address >> 30) & 0x1FFu);
        usize pd_index = (usize)((virtual_address >> 21) & 0x1FFu);
        u64 *pml4_slot = &root_table[pml4_index];
        u64 *pdpt = table_pointer(*pml4_slot);
        u64 *pdpt_slot = &pdpt[pdpt_index];
        u64 *pd = table_pointer(*pdpt_slot);
        u64 *pd_slot = &pd[pd_index];
        *leaf = 0;
        statistics.mappings_removed++;
        invalidate_page(virtual_address);
        /* Reclaim only empty table pages allocated from PMM. Bootstrap tables
           are reserved by PMM and therefore remain in place. */
        discard_empty_child(pd_slot);
        discard_empty_child(pdpt_slot);
        discard_empty_child(pml4_slot);
    } else statistics.failed_operations++;
    irq_restore(irq_flags);
    return result;
}

int vmm_protect_page(u64 virtual_address, u64 flags) {
    const u64 allowed = VMM_WRITE | VMM_USER | VMM_WRITE_THROUGH |
                        VMM_CACHE_DISABLE | VMM_GLOBAL | VMM_NO_EXECUTE;
    u64 *leaf;
    u64 entry_flags = PTE_PRESENT;
    u64 irq_flags;
    int result;
    if (!page_address_valid(virtual_address) || (flags & ~allowed))
        return VMM_ERR_ARGUMENT;
    if ((flags & VMM_NO_EXECUTE) && !statistics.nx_enabled)
        return VMM_ERR_UNSUPPORTED;
    if ((flags & VMM_GLOBAL) && !statistics.global_pages_enabled)
        return VMM_ERR_UNSUPPORTED;
    if (statistics.nx_enabled && (flags & VMM_WRITE) && !(flags & VMM_NO_EXECUTE))
        return VMM_ERR_PERMISSION;
    if (!statistics.initialized) return VMM_ERR_NOT_READY;
    if (flags & VMM_WRITE) entry_flags |= PTE_WRITE;
    if (flags & VMM_USER) entry_flags |= PTE_USER;
    if (flags & VMM_WRITE_THROUGH) entry_flags |= PTE_PWT;
    if (flags & VMM_CACHE_DISABLE) entry_flags |= PTE_PCD;
    if (flags & VMM_GLOBAL) entry_flags |= PTE_GLOBAL;
    if (flags & VMM_NO_EXECUTE) entry_flags |= PTE_NX;

    irq_flags = irq_save();
    {
        vmm_mapping_t existing;
        result = vmm_query(virtual_address, &existing);
        if (result == VMM_OK)
            result = locate_leaf(virtual_address, 1, (flags & VMM_USER) != 0,
                                 (flags & VMM_WRITE) != 0, &leaf);
    }
    if (result == VMM_OK && (!leaf || !(*leaf & PTE_PRESENT)))
        result = VMM_ERR_NOT_MAPPED;
    if (result == VMM_OK) {
        *leaf = (*leaf & PTE_ADDRESS_MASK) | entry_flags;
        invalidate_page(virtual_address);
    } else statistics.failed_operations++;
    irq_restore(irq_flags);
    return result;
}

int vmm_get_stats(vmm_stats_t *stats) {
    if (!stats) return VMM_ERR_ARGUMENT;
    if (!statistics.initialized) return VMM_ERR_NOT_READY;
    *stats = statistics;
    return VMM_OK;
}
