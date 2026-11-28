#include "terminal_apps.h"
#include "serial.h"
#include "fs.h"
#include "kformat.h"
#include "clock.h"
#include "timer.h"
#include "cpu.h"
#include "scheduler.h"
#include "heap_diag.h"
#include "pmm.h"
#include "memory.h"
#include "terminal_ui.h"

void terminal_monitor_run(void) {
    u64 last = (u64)-1;
    u64 refresh_ticks = (u64)clock_frequency() / 2u;
    if (!refresh_ticks) refresh_ticks = 1u;
    for (;;) {
        int key;
        u64 now = timer_ticks();
        while (serial_try_getkey(&key))
            if (key == 'q' || key == 'Q' || key == 27) { terminal_ui_clear(); return; }
        if (last == (u64)-1 || now - last >= refresh_ticks) {
            heap_diagnostics_t heap;
            pmm_stats_t pages;
            int have_heap = memory_diagnostics(&heap) == 0;
            int have_pages = pmm_get_stats(&pages) == 0;
            terminal_ui_clear();
            serial_write(TERM_CYAN "DENOS SYSTEM MONITOR" TERM_RESET "\n\n");
            serial_write("Uptime: "); terminal_ui_u64(clock_seconds()); serial_write(" seconds\n");
            serial_write("Timer:  "); terminal_ui_u64(now); serial_write(" ticks\n\n");
            serial_write(TERM_YELLOW "KERNEL HEAP" TERM_RESET "\n");
            if (have_heap) {
                serial_write("  Used:      "); terminal_ui_u64(heap.used_bytes); serial_write(" bytes\n");
                serial_write("  Free:      "); terminal_ui_u64(heap.free_bytes); serial_write(" bytes\n");
                serial_write("  Blocks:    "); terminal_ui_u64(heap.block_count); serial_write("\n");
                serial_write("  Allocated: "); terminal_ui_u64(heap.live_allocations); serial_write(" live allocations\n");
                serial_write("  Health:    "); serial_write(heap.healthy ? TERM_GREEN "OK" : TERM_RED "ERROR"); serial_write(TERM_RESET "\n");
            } else serial_write("  Diagnostics unavailable\n");
            serial_write("\n" TERM_YELLOW "PHYSICAL MEMORY" TERM_RESET "\n");
            if (have_pages) {
                serial_write("  Managed:   "); terminal_ui_u64(pages.managed_pages); serial_write(" pages\n");
                serial_write("  Free:      "); terminal_ui_u64(pages.free_pages); serial_write(" pages\n");
                serial_write("  Allocated: "); terminal_ui_u64(pages.allocated_pages); serial_write(" pages\n");
            } else serial_write("  Page manager unavailable\n");
            serial_write("\n" TERM_YELLOW "DENFS STORAGE" TERM_RESET "\n");
            serial_write("  Used:      "); terminal_ui_u64(fs_bytes_used()); serial_write(" bytes\n");
            serial_write("  Capacity:  "); terminal_ui_u64(fs_storage_capacity()); serial_write(" bytes\n");
            serial_write("  Free:      "); terminal_ui_u64(fs_storage_free()); serial_write(" bytes\n");
            serial_write("  Entries:   "); terminal_ui_u64(fs_entry_count()); serial_write("\n");
            serial_write("  Backend:   "); serial_write(fs_storage_name()); serial_write("\n");
            serial_write("\nACTIVE TASKS\n  Count:     "); terminal_ui_u64(scheduler_count()); serial_write("\n\n");
            serial_write(TERM_DIM "Refreshes twice per second. Press Q to return to the shell." TERM_RESET "\n");
            last = now;
        }
        scheduler_run();
        cpu_pause();
    }
}

