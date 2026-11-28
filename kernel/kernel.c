#include "serial.h"
#include "fs.h"
#include "shell.h"
#include "timer.h"
#include "memory.h"
#include "version.h"
#include "log.h"
#include "boot_status.h"
#include "device.h"
#include "heap_diag.h"
#include "panic.h"
#include "pmm.h"
#include "paging.h"
#include "vmalloc.h"
#include "net.h"
#include "scheduler.h"
#include "vfs.h"
#include "ipc.h"
#include "event.h"
#include "bootargs.h"
#include "clock.h"
#include "entropy.h"
#include "env.h"
#include "history.h"
#include "service.h"
#include "apps.h"

extern char _kernel_start[];
extern char _kernel_end[];

static void boot_status(const char *message) {
    serial_write("\033[1;32m[ OK ]\033[0m ");
    serial_write(message);
    serial_putc('\n');
}

static int network_service(void *context) {
    (void)context;
    net_poll();
    return SCHEDULER_TASK_REPEAT;
}

static int heap_health_service(void *context) {
    (void)context;
    if (!memory_validate())
        klog_write(KLOG_ERROR, "scheduled heap integrity check failed");
    return SCHEDULER_TASK_REPEAT;
}

static void register_core_devices(void) {
    device_registry_init();
    (void)device_register("VGA text console", "vga-text-80x25",
                          DEVICE_CLASS_DISPLAY, DEVICE_STATE_READY);
    (void)device_register("PS/2 keyboard", "i8042-poll", DEVICE_CLASS_INPUT,
                          DEVICE_STATE_READY);
    (void)device_register("PIT clock", "8253-pit", DEVICE_CLASS_TIMER,
                          DEVICE_STATE_READY);
    (void)device_register("PC speaker", "8253-channel2", DEVICE_CLASS_AUDIO,
                          DEVICE_STATE_READY);
    (void)device_register("ATA storage", "ide-pio", DEVICE_CLASS_STORAGE,
                          fs_is_persistent() ? DEVICE_STATE_READY : DEVICE_STATE_DEGRADED);
    (void)device_register("Ethernet adapter", "e1000", DEVICE_CLASS_NETWORK,
                          DEVICE_STATE_OFFLINE);
}

void kernel_main(u32 multiboot_magic, const void *multiboot_info) {
    u64 boot_info_address;
    int boot_info_mapped = 0;
    u32 heap_service_id = 0;
    u32 network_service_id = 0;
    int safe_mode;
    int pmm_ready = 0;

    interrupt_init();
    serial_init();
    serial_write("\033[2J\033[H");
    serial_write("\033[1;36m" DENOS_NAME " " DENOS_VERSION "\033[0m\n");
    serial_write("\033[1;34m64-bit operating system\033[0m\n");
    serial_write("\033[1;34m----------------------------------------\033[0m\n\n");

    klog_init();
    boot_status_init();
    boot_status("VGA text console and COM1 serial initialized");
    boot_status("CPU exception vectors and emergency double-fault stack installed");
    (void)boot_status_add("text console", BOOT_RESULT_OK);
    klog_write(KLOG_INFO, "text console initialized");
    klog_write(KLOG_INFO, "CPU exception IDT initialized before subsystem startup");

    /* The bootstrap identity maps the low 4 GiB. Validate the complete
       Multiboot2 block before any subsystem parses its tags. */
    boot_info_address = (u64)(usize)multiboot_info;
    if (multiboot_magic == PMM_BOOT_MAGIC && multiboot_info &&
        boot_info_address < PMM_ADDRESS_LIMIT) {
        u32 info_size = *(const u32 *)multiboot_info;
        boot_info_mapped = info_size >= 16u &&
                           info_size <= PMM_ADDRESS_LIMIT - boot_info_address;
        if (boot_info_mapped) {
            bootargs_init(multiboot_info, info_size);
        }
    }
    safe_mode = bootargs_has("safe-mode");
    if (bootargs_get()[0]) klogf(KLOG_INFO, "boot arguments: %s", bootargs_get());
    else klog_write(KLOG_INFO, "no boot arguments supplied");

    if (boot_info_mapped &&
        pmm_init(multiboot_magic, multiboot_info,
                 (u64)(usize)_kernel_start, (u64)(usize)_kernel_end) == 0) {
        pmm_stats_t stats;
        if (pmm_get_stats(&stats) == 0) {
            pmm_ready = 1;
            boot_status("physical page allocator initialized");
            (void)boot_status_add("physical page manager", BOOT_RESULT_OK);
            klogf(KLOG_INFO, "PMM: %llu managed pages, %llu free pages",
                  (unsigned long long)stats.managed_pages,
                  (unsigned long long)stats.free_pages);
        }
    } else {
        serial_write("\033[1;33m[WARN]\033[0m physical page manager unavailable; using the kernel heap only\n");
        (void)boot_status_add("physical page manager", BOOT_RESULT_WARN);
        klog_write(KLOG_WARN, "Multiboot2 memory map unavailable or outside the identity map");
    }

    memory_init();
    if (!memory_is_healthy()) KPANIC("bootstrap heap failed its initial integrity check");
    boot_status("dynamic kernel heap initialized");
    (void)boot_status_add("dynamic kernel heap", BOOT_RESULT_OK);

    if (pmm_ready && paging_init() == VMM_OK) {
        vmm_stats_t paging_stats;
        (void)boot_status_add("four-level page-table manager", BOOT_RESULT_OK);
        if (vmm_get_stats(&paging_stats) == VMM_OK)
            klogf(KLOG_INFO, "paging: CR3 attached, NX %s",
                  paging_stats.nx_enabled ? "enabled" : "unavailable");
        if (vmalloc_init() == VMALLOC_OK) {
            (void)boot_status_add("kernel virtual allocator", BOOT_RESULT_OK);
            klog_write(KLOG_INFO, "vmalloc: 64 MiB kernel virtual window ready");
        } else {
            (void)boot_status_add("kernel virtual allocator", BOOT_RESULT_WARN);
            klog_write(KLOG_WARN, "vmalloc unavailable; fixed kernel heap remains active");
        }
    } else {
        (void)boot_status_add("four-level page-table manager", BOOT_RESULT_WARN);
        klog_write(KLOG_WARN, "paging manager unavailable; bootstrap page tables remain active");
    }

    fs_init();
    fs_seed_user_tree();
    apps_install(); /* Installs bundled Aiden examples without overwriting user edits. */
    if (vfs_init() == 0) {
        (void)boot_status_add("VFS mount namespace", BOOT_RESULT_OK);
        klog_write(KLOG_INFO, "mounted DenFS root, read-only procfs, and devfs");
    } else {
        (void)boot_status_add("VFS mount namespace", BOOT_RESULT_WARN);
        klog_write(KLOG_WARN, "VFS mount namespace initialization failed");
    }
    if (fs_is_persistent()) {
        boot_status("persistent ATA disk mounted");
        (void)boot_status_add("root filesystem", BOOT_RESULT_OK);
        klogf(KLOG_INFO, "mounted storage device: %s", fs_disk_model());
    } else {
        serial_write("\033[1;33m[WARN]\033[0m storage is using RAM fallback: ");
        serial_write(fs_storage_name());
        serial_putc('\n');
        (void)boot_status_add("root filesystem", BOOT_RESULT_WARN);
        klogf(KLOG_WARN, "persistent storage unavailable: %s", fs_storage_name());
    }

    timer_init(100);
    clock_init(100);
    event_init();
    ipc_init();
    scheduler_init();
    (void)service_init();
    entropy_init();
    env_init();
    history_init();
    (void)boot_status_add("IDT/PIC/PIT timer", BOOT_RESULT_OK);

    register_core_devices();
    if (service_register("heap-watch", 2, 500, heap_health_service, 0,
                         &heap_service_id) == 0)
        (void)service_start(heap_service_id);
    else klog_write(KLOG_WARN, "could not register heap-watch service");

    if (safe_mode) {
        (void)boot_status_add("network service", BOOT_RESULT_WARN);
        klog_write(KLOG_WARN, "safe-mode requested; network initialization skipped");
    } else if (net_init() == 0) {
        (void)device_set_state("Ethernet adapter", DEVICE_STATE_READY);
        (void)boot_status_add("Intel E1000 network adapter", BOOT_RESULT_OK);
        klog_write(KLOG_INFO, "Intel E1000 initialized (static QEMU user-net IPv4 profile)");
        if (service_register("network-poll", 6, 1, network_service, 0,
                             &network_service_id) == 0)
            (void)service_start(network_service_id);
        else klog_write(KLOG_WARN, "could not register network-poll service");
    } else {
        (void)boot_status_add("Intel E1000 network adapter", BOOT_RESULT_WARN);
        klog_write(KLOG_WARN, "Intel E1000 adapter not found or could not be initialized");
    }
    (void)boot_status_add("device registry", BOOT_RESULT_OK);
    boot_status("kernel services initialized");


    serial_write("\nDenOS command console is ready. Type 'help' for commands.\n");
    serial_write("Use 'sysinfo', 'devices', 'services', 'mounts', 'paging' and 'bootlog' to inspect the system.\n");
    serial_write("The serial console remains available on COM1 for debugging.\n\n");
    klog_write(KLOG_INFO, "kernel initialization complete; entering command shell");
    shell_run();
    KPANIC("command shell unexpectedly returned");
}
