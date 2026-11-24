#include "vfs.h"
#include "fs.h"
#include "path.h"
#include "kstring.h"
#include "kformat.h"
#include "timer.h"
#include "memory.h"
#include "pmm.h"
#include "device.h"
#include "scheduler.h"
#include "net.h"
#include "event.h"
#include "ipc.h"

#define VFS_ERROR_CAP 48

typedef struct vfs_mount {
    vfs_mount_info_t info;
    const vfs_operations_t *ops;
    void *context;
} vfs_mount_t;

static vfs_mount_t mounts[VFS_MOUNT_CAP];
static usize mount_used;
static u32 next_mount_id;
static char last_error[VFS_ERROR_CAP];

static void set_error(const char *message) {
    kstrlcpy(last_error, message ? message : "VFS error", sizeof(last_error));
}

const char *vfs_last_error(void) { return last_error; }

static int normalize_path(const char *path, char output[VFS_PATH_CAP]) {
    if (!path || path[0] != '/') { set_error("path must be absolute"); return -1; }
    if (path[1] == '\0') { output[0] = '\0'; return 0; }
    if (fs_path_normalize("", path, output, VFS_PATH_CAP) < 0) {
        set_error("invalid or overlong path");
        return -1;
    }
    return 0;
}

int vfs_mount(const char *mount_path, const char *name,
              const vfs_operations_t *operations, void *context, u32 flags) {
    char normalized[VFS_PATH_CAP];
    vfs_mount_t *mount;
    if (!mount_path || !name || !name[0] || !operations ||
        !operations->stat || !operations->list ||
        (flags & ~VFS_MOUNT_READ_ONLY) != 0u ||
        kstrnlen(name, sizeof(mounts[0].info.name)) >= sizeof(mounts[0].info.name)) {
        set_error("invalid mount arguments");
        return -1;
    }
    if (normalize_path(mount_path, normalized) < 0) return -1;
    if (kstrcmp(normalized, "") != 0 && !fs_path_valid(normalized)) {
        set_error("invalid mount path");
        return -1;
    }
    for (usize i = 0; i < mount_used; ++i) {
        if (kstrcmp(mounts[i].info.path, normalized) == 0) {
            set_error("mount point already in use");
            return -1;
        }
    }
    if (mount_used >= VFS_MOUNT_CAP) { set_error("mount table full"); return -1; }
    mount = &mounts[mount_used++];
    kmemset(mount, 0, sizeof(*mount));
    kstrlcpy(mount->info.path, normalized, sizeof(mount->info.path));
    kstrlcpy(mount->info.name, name, sizeof(mount->info.name));
    mount->info.flags = flags;
    mount->info.id = next_mount_id++;
    if (!next_mount_id) next_mount_id = 1;
    mount->ops = operations;
    mount->context = context;
    last_error[0] = '\0';
    return 0;
}

static vfs_mount_t *resolve(const char *path, const char **relative) {
    char normalized[VFS_PATH_CAP];
    vfs_mount_t *best = 0;
    usize best_length = 0;
    if (normalize_path(path, normalized) < 0) return 0;
    for (usize i = 0; i < mount_used; ++i) {
        const char *prefix = mounts[i].info.path;
        usize length = kstrlen(prefix);
        int matches = length == 0 ||
            (kstrncmp(normalized, prefix, length) == 0 &&
             (normalized[length] == '\0' || normalized[length] == '/'));
        if (matches && (!best || length > best_length)) {
            best = &mounts[i];
            best_length = length;
        }
    }
    if (!best) { set_error("no filesystem mounted for path"); return 0; }
    /* The normalized buffer is copied into a static scratch area below. */
    static char resolved_path[VFS_PATH_CAP];
    kstrlcpy(resolved_path, normalized, sizeof(resolved_path));
    if (best_length == 0) *relative = resolved_path;
    else if (resolved_path[best_length] == '\0') *relative = "";
    else *relative = resolved_path + best_length + 1;
    return best;
}

int vfs_read(const char *path, char *buffer, usize capacity, usize *length) {
    const char *relative;
    vfs_mount_t *mount = resolve(path, &relative);
    if (!mount || !buffer || !capacity || !mount->ops->read) {
        set_error("read is not supported for path"); return -1;
    }
    if (mount->ops->read(mount->context, relative, buffer, capacity, length) < 0) {
        set_error("file could not be read"); return -1;
    }
    last_error[0] = '\0';
    return 0;
}

int vfs_write(const char *path, const char *data) {
    const char *relative;
    vfs_mount_t *mount = resolve(path, &relative);
    if (!mount || !data || (mount->info.flags & VFS_MOUNT_READ_ONLY) || !mount->ops->write) {
        set_error("filesystem is read-only or write is unsupported"); return -1;
    }
    if (mount->ops->write(mount->context, relative, data) < 0) {
        set_error("write failed"); return -1;
    }
    last_error[0] = '\0';
    return 0;
}

int vfs_mkdir(const char *path) {
    const char *relative;
    vfs_mount_t *mount = resolve(path, &relative);
    if (!mount || (mount->info.flags & VFS_MOUNT_READ_ONLY) || !mount->ops->mkdir ||
        mount->ops->mkdir(mount->context, relative) < 0) {
        set_error("directory creation is unsupported or failed"); return -1;
    }
    return 0;
}

int vfs_remove(const char *path) {
    const char *relative;
    vfs_mount_t *mount = resolve(path, &relative);
    if (!mount || (mount->info.flags & VFS_MOUNT_READ_ONLY) || !mount->ops->remove ||
        mount->ops->remove(mount->context, relative) < 0) {
        set_error("remove is unsupported or failed"); return -1;
    }
    return 0;
}

int vfs_stat_path(const char *path, vfs_stat_t *result) {
    const char *relative;
    vfs_mount_t *mount = resolve(path, &relative);
    if (!mount || !result || mount->ops->stat(mount->context, relative, result) < 0) {
        set_error("path does not exist"); return -1;
    }
    result->read_only = (mount->info.flags & VFS_MOUNT_READ_ONLY) != 0u;
    return 0;
}

int vfs_list(const char *path, usize index, vfs_dirent_t *result) {
    const char *relative;
    vfs_mount_t *mount = resolve(path, &relative);
    if (!mount || !result || mount->ops->list(mount->context, relative, index, result) < 0) {
        set_error("directory entry is unavailable"); return -1;
    }
    return 0;
}

int vfs_mount_get(usize index, vfs_mount_info_t *result) {
    if (!result || index >= mount_used) return -1;
    *result = mounts[index].info;
    return 0;
}

usize vfs_mount_count(void) { return mount_used; }

/* -------------------------- DenFS root backend -------------------------- */
static int denfs_read(void *ctx, const char *path, char *buffer,
                      usize capacity, usize *length) {
    const char *data;
    usize size;
    (void)ctx;
    data = fs_read(path);
    if (!data) return -1;
    size = kstrnlen(data, capacity);
    if (size >= capacity) return -1;
    kmemcpy(buffer, data, size);
    buffer[size] = '\0';
    if (length) *length = size;
    return 0;
}
static int denfs_write(void *ctx, const char *path, const char *data) {
    (void)ctx; return fs_write(path, data);
}
static int denfs_mkdir(void *ctx, const char *path) { (void)ctx; return fs_mkdir(path); }
static int denfs_remove(void *ctx, const char *path) { (void)ctx; return fs_remove(path); }
static int denfs_stat(void *ctx, const char *path, vfs_stat_t *result) {
    const char *data;
    (void)ctx;
    if (!path || !result) return -1;
    if (!path[0] || fs_is_directory(path)) {
        result->type = VFS_TYPE_DIRECTORY; result->size = 0; return 0;
    }
    data = fs_read(path);
    if (!data) return -1;
    result->type = VFS_TYPE_FILE;
    result->size = kstrlen(data);
    return 0;
}
static int denfs_list(void *ctx, const char *path, usize index, vfs_dirent_t *result) {
    int type;
    (void)ctx;
    if (fs_get_entry_in_dir(path, index, result->name, sizeof(result->name),
                            &result->size, &type) < 0) return -1;
    result->type = type == FS_ENTRY_DIRECTORY ? VFS_TYPE_DIRECTORY : VFS_TYPE_FILE;
    return 0;
}
static const vfs_operations_t denfs_ops = {
    denfs_read, denfs_write, denfs_mkdir, denfs_remove, denfs_stat, denfs_list
};

/* --------------------------- procfs backend ----------------------------- */
static int proc_content(const char *path, char *buffer, usize capacity, usize *length) {
    int written = -1;
    if (kstrcmp(path, "uptime") == 0) {
        written = ksnprintf(buffer, capacity, "%llu.%02llu seconds\n",
            (unsigned long long)(timer_ticks() / 100u),
            (unsigned long long)((timer_ticks() % 100u)));
    } else if (kstrcmp(path, "meminfo") == 0) {
        pmm_stats_t pages;
        if (pmm_get_stats(&pages) == 0 && pages.initialized)
            written = ksnprintf(buffer, capacity,
                "HeapTotal: %lu bytes\nHeapUsed: %lu bytes\nHeapFree: %lu bytes\nPhysicalPagesTotal: %llu\nPhysicalPagesFree: %llu\n",
                (unsigned long)memory_capacity(), (unsigned long)memory_used(),
                (unsigned long)memory_available(), (unsigned long long)pages.managed_pages,
                (unsigned long long)pages.free_pages);
        else
            written = ksnprintf(buffer, capacity,
                "HeapTotal: %lu bytes\nHeapUsed: %lu bytes\nHeapFree: %lu bytes\nPhysicalPages: unavailable\n",
                (unsigned long)memory_capacity(), (unsigned long)memory_used(),
                (unsigned long)memory_available());
    } else if (kstrcmp(path, "tasks") == 0) {
        scheduler_task_info_t task;
        usize used = 0;
        written = ksnprintf(buffer, capacity, "ID STATE PRIORITY RUNS NAME\n");
        if (written < 0 || (usize)written >= capacity) return -1;
        used = (usize)written;
        for (u32 i = 0; i < SCHEDULER_MAX_TASKS; ++i) {
            char line[96];
            int n;
            if (scheduler_get_by_index(i, &task) < 0) break;
            n = ksnprintf(line, sizeof(line), "%u %s %u %llu %s\n", task.id,
                task.state == SCHEDULER_READY ? "ready" : "stopped", task.priority,
                (unsigned long long)task.runs, task.name);
            if (n < 0 || (usize)n >= sizeof(line) || used + (usize)n >= capacity) return -1;
            kmemcpy(buffer + used, line, (usize)n); used += (usize)n;
        }
        buffer[used] = '\0'; written = (int)used;
    } else if (kstrcmp(path, "events") == 0) {
        kernel_event_stats_t stats;
        event_get_stats(&stats);
        written = ksnprintf(buffer, capacity,
            "Pending: %u\nPublished: %llu\nConsumed: %llu\nDropped: %llu\n",
            stats.pending, (unsigned long long)stats.published,
            (unsigned long long)stats.consumed, (unsigned long long)stats.dropped);
    } else if (kstrcmp(path, "mounts") == 0) {
        usize used = 0;
        written = ksnprintf(buffer, capacity, "FILESYSTEM MOUNTPOINT FLAGS\n");
        if (written < 0 || (usize)written >= capacity) return -1;
        used = (usize)written;
        for (usize i = 0; i < vfs_mount_count(); ++i) {
            vfs_mount_info_t mount;
            char line[96];
            int n;
            if (vfs_mount_get(i, &mount) < 0) continue;
            n = ksnprintf(line, sizeof(line), "%s /%s %s\n", mount.name,
                mount.path, (mount.flags & VFS_MOUNT_READ_ONLY) ? "ro" : "rw");
            if (n < 0 || (usize)n >= sizeof(line) || used + (usize)n >= capacity) return -1;
            kmemcpy(buffer + used, line, (usize)n); used += (usize)n;
        }
        buffer[used] = '\0'; written = (int)used;
    } else if (kstrcmp(path, "ipc") == 0) {
        scheduler_task_info_t task;
        ipc_endpoint_stats_t stats;
        usize used = 0;
        written = ksnprintf(buffer, capacity, "TASK PENDING SENT RECEIVED DROPPED\n");
        if (written < 0 || (usize)written >= capacity) return -1;
        used = (usize)written;
        for (u32 i = 0; i < SCHEDULER_MAX_TASKS; ++i) {
            char line[96]; int n;
            if (scheduler_get_by_index(i, &task) < 0) break;
            if (ipc_endpoint_get_stats(task.id, &stats) != IPC_OK) continue;
            n = ksnprintf(line, sizeof(line), "%u %u %llu %llu %llu\n", stats.task_id,
                stats.pending, (unsigned long long)stats.sent,
                (unsigned long long)stats.received, (unsigned long long)stats.dropped);
            if (n < 0 || (usize)n >= sizeof(line) || used + (usize)n >= capacity) return -1;
            kmemcpy(buffer + used, line, (usize)n); used += (usize)n;
        }
        buffer[used] = '\0'; written = (int)used;
    } else if (kstrcmp(path, "devices") == 0) {
        usize used = 0;
        written = ksnprintf(buffer, capacity, "NAME DRIVER CLASS STATE\n");
        if (written < 0 || (usize)written >= capacity) return -1;
        used = (usize)written;
        for (int i = 0; i < device_count(); ++i) {
            const device_info_t *device = device_get(i);
            char line[112];
            int n;
            if (!device) continue;
            n = ksnprintf(line, sizeof(line), "%s %s %s %s\n", device->name,
                device->driver, device_class_name((enum device_class)device->device_class),
                device_state_name((enum device_state)device->state));
            if (n < 0 || (usize)n >= sizeof(line) || used + (usize)n >= capacity) return -1;
            kmemcpy(buffer + used, line, (usize)n); used += (usize)n;
        }
        buffer[used] = '\0'; written = (int)used;
    } else if (kstrcmp(path, "net/dev") == 0) {
        net_status_t status;
        if (net_get_status(&status) < 0)
            written = ksnprintf(buffer, capacity,
                "interface link rx_frames tx_frames\ne1000 unavailable 0 0\n");
        else
            written = ksnprintf(buffer, capacity,
                "interface link rx_frames tx_frames\ne1000 %s %u %u\n",
                status.link_up ? "up" : "down", status.rx_frames, status.tx_frames);
    }
    if (written < 0 || (usize)written >= capacity) return -1;
    if (length) *length = (usize)written;
    return 0;
}
static int proc_read(void *ctx, const char *path, char *buffer, usize capacity, usize *length) {
    (void)ctx; return proc_content(path, buffer, capacity, length);
}
static int proc_stat(void *ctx, const char *path, vfs_stat_t *result) {
    char scratch[4096]; usize length;
    (void)ctx;
    if (!path[0] || kstrcmp(path, "net") == 0) {
        result->type = VFS_TYPE_DIRECTORY; result->size = 0; return 0;
    }
    if (proc_content(path, scratch, sizeof(scratch), &length) < 0) return -1;
    result->type = VFS_TYPE_FILE; result->size = length; return 0;
}
static int proc_list(void *ctx, const char *path, usize index, vfs_dirent_t *result) {
    static const char *const root[] = {"uptime", "meminfo", "tasks", "ipc", "events", "mounts", "devices", "net"};
    static const char *const net[] = {"dev"};
    const char *name;
    (void)ctx;
    if (!path[0]) {
        if (index >= sizeof(root) / sizeof(root[0])) return -1;
        name = root[index];
    } else if (kstrcmp(path, "net") == 0) {
        if (index >= sizeof(net) / sizeof(net[0])) return -1;
        name = net[index];
    } else return -1;
    kstrlcpy(result->name, name, sizeof(result->name));
    result->type = (kstrcmp(path, "") == 0 && kstrcmp(name, "net") == 0) ?
                   VFS_TYPE_DIRECTORY : VFS_TYPE_FILE;
    result->size = 0;
    return 0;
}
static const vfs_operations_t proc_ops = {proc_read, 0, 0, 0, proc_stat, proc_list};

/* ---------------------------- devfs backend ----------------------------- */
static int dev_read(void *ctx, const char *path, char *buffer, usize capacity, usize *length) {
    (void)ctx;
    if (kstrcmp(path, "null") == 0) { if (length) *length = 0; if (capacity) buffer[0] = 0; return 0; }
    if (kstrcmp(path, "zero") == 0) {
        if (!capacity) return -1;
        kmemset(buffer, 0, capacity - 1u);
        buffer[capacity - 1u] = '\0';
        if (length) *length = capacity - 1u;
        return 0;
    }
    return -1;
}
static int dev_write(void *ctx, const char *path, const char *data) {
    (void)ctx; (void)data;
    return kstrcmp(path, "null") == 0 ? 0 : -1;
}
static int dev_stat(void *ctx, const char *path, vfs_stat_t *result) {
    (void)ctx;
    if (!path[0]) { result->type = VFS_TYPE_DIRECTORY; result->size = 0; return 0; }
    if (kstrcmp(path, "null") != 0 && kstrcmp(path, "zero") != 0) return -1;
    result->type = VFS_TYPE_FILE; result->size = 0; return 0;
}
static int dev_list(void *ctx, const char *path, usize index, vfs_dirent_t *result) {
    static const char *const names[] = {"null", "zero"};
    (void)ctx;
    if (path[0] || index >= 2) return -1;
    kstrlcpy(result->name, names[index], sizeof(result->name));
    result->type = VFS_TYPE_FILE; result->size = 0; return 0;
}
static const vfs_operations_t dev_ops = {dev_read, dev_write, 0, 0, dev_stat, dev_list};

int vfs_init(void) {
    mount_used = 0;
    next_mount_id = 1;
    last_error[0] = '\0';
    kmemset(mounts, 0, sizeof(mounts));
    if (vfs_mount("/", "denfs", &denfs_ops, 0, 0) < 0 ||
        vfs_mount("/proc", "procfs", &proc_ops, 0, VFS_MOUNT_READ_ONLY) < 0 ||
        vfs_mount("/dev", "devfs", &dev_ops, 0, 0) < 0) {
        set_error("failed to mount built-in filesystems");
        return -1;
    }
    return 0;
}
