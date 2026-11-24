#include <assert.h>
#include <stdio.h>
#include "vfs.h"
#include "kstring.h"
#include "fs.h"
#include "timer.h"
#include "memory.h"
#include "pmm.h"
#include "device.h"
#include "scheduler.h"
#include "ipc.h"
#include "event.h"
#include "net.h"


/* Host stubs used to exercise the actual built-in mount adapters. */
u64 timer_ticks(void) { return 1234; }
const char *fs_read(const char *path) {
    return kstrcmp(path, "home/user/hello.txt") == 0 ? "hello from DenFS" : 0;
}
int fs_write(const char *path, const char *data) { return path[0] && data ? 0 : -1; }
int fs_mkdir(const char *path) { return path[0] ? 0 : -1; }
int fs_remove(const char *path) { return path[0] ? 0 : -1; }
int fs_is_directory(const char *path) { return !path[0] || kstrcmp(path, "home/user") == 0; }
int fs_get_entry_in_dir(const char *directory, usize index, char *name,
                        usize capacity, usize *size, int *type) {
    if (kstrcmp(directory, "home/user") != 0 || index != 0 || capacity < 10) return -1;
    kstrlcpy(name, "hello.txt", capacity); if (size) *size = 16;
    if (type) *type = FS_ENTRY_FILE;
    return 0;
}
usize memory_capacity(void) { return 4096; }
usize memory_used(void) { return 1024; }
usize memory_available(void) { return 3072; }
int pmm_get_stats(pmm_stats_t *stats) { (void)stats; return -1; }
int device_count(void) { return 0; }
const device_info_t *device_get(int index) { (void)index; return 0; }
const char *device_class_name(enum device_class type) { (void)type; return "unknown"; }
const char *device_state_name(enum device_state state) { (void)state; return "unknown"; }
int scheduler_get_by_index(u32 index, scheduler_task_info_t *out) { (void)index; (void)out; return -1; }
int ipc_endpoint_get_stats(u32 id, ipc_endpoint_stats_t *stats) { (void)id; (void)stats; return IPC_ERROR_NO_ENDPOINT; }
u32 ipc_endpoint_count(void) { return 0; }
void event_get_stats(kernel_event_stats_t *stats) { if (stats) { stats->pending=0; stats->published=0; stats->consumed=0; stats->dropped=0; } }
int net_get_status(net_status_t *status) { if (status) { status->initialized=0; status->link_up=0; status->rx_frames=0; status->tx_frames=0; } return -1; }

static int root_read(void *ctx, const char *path, char *buffer, usize cap, usize *len) {
    const char *value = (const char *)ctx;
    (void)path;
    if (kstrlen(value) + 1 > cap) return -1;
    kstrlcpy(buffer, value, cap); if (len) *len = kstrlen(value); return 0;
}
static int root_write(void *ctx, const char *path, const char *data) {
    (void)ctx; return path[0] && data[0] ? 0 : -1;
}
static int root_stat(void *ctx, const char *path, vfs_stat_t *stat) {
    (void)ctx;
    if (!path[0]) { stat->type = VFS_TYPE_DIRECTORY; stat->size = 0; return 0; }
    if (kstrcmp(path, "file") == 0) { stat->type = VFS_TYPE_FILE; stat->size = 4; return 0; }
    return -1;
}
static int root_list(void *ctx, const char *path, usize index, vfs_dirent_t *entry) {
    (void)ctx;
    if (path[0] || index) return -1;
    kstrlcpy(entry->name, "file", sizeof(entry->name)); entry->size = 4;
    entry->type = VFS_TYPE_FILE; return 0;
}
static int data_stat(void *ctx, const char *path, vfs_stat_t *stat) {
    (void)ctx;
    if (kstrcmp(path, "file") != 0) return -1;
    stat->type = VFS_TYPE_FILE; stat->size = 11; return 0;
}
static int data_list(void *ctx, const char *path, usize index, vfs_dirent_t *entry) {
    (void)ctx; (void)path; (void)index; (void)entry; return -1;
}
static int data_read(void *ctx, const char *path, char *buffer, usize cap, usize *len) {
    (void)ctx;
    if (kstrcmp(path, "file") != 0 || cap < 12) return -1;
    kstrlcpy(buffer, "mounted-data", cap); if (len) *len = 12; return 0;
}
static const vfs_operations_t root_ops = {root_read, root_write, 0, 0, root_stat, root_list};
static const vfs_operations_t data_ops = {data_read, 0, 0, 0, data_stat, data_list};

int main(void) {
    char buffer[4096]; usize length = 0; vfs_stat_t stat; vfs_dirent_t entry;
    assert(vfs_mount("/", "root", &root_ops, "root-data", 0) == 0);
    assert(vfs_mount("/data", "datafs", &data_ops, 0, VFS_MOUNT_READ_ONLY) == 0);
    assert(vfs_mount("/data", "duplicate", &data_ops, 0, 0) < 0);
    assert(vfs_read("/file", buffer, sizeof(buffer), &length) == 0);
    assert(kstrcmp(buffer, "root-data") == 0 && length == 9);
    assert(vfs_read("/data/file", buffer, sizeof(buffer), &length) == 0);
    assert(kstrcmp(buffer, "mounted-data") == 0 && length == 12);
    assert(vfs_write("/file", "updated") == 0);
    assert(vfs_write("/data/file", "blocked") < 0);
    assert(vfs_stat_path("/data/file", &stat) == 0 && stat.read_only);
    assert(vfs_list("/", 0, &entry) == 0 && kstrcmp(entry.name, "file") == 0);
    assert(vfs_read("/../../escape", buffer, sizeof(buffer), &length) < 0);
    assert(vfs_mount_count() == 2);

    assert(vfs_init() == 0);
    assert(vfs_mount_count() == 3);
    assert(vfs_read("/home/user/hello.txt", buffer, sizeof(buffer), &length) == 0);
    assert(kstrcmp(buffer, "hello from DenFS") == 0);
    assert(vfs_read("/proc/uptime", buffer, sizeof(buffer), &length) == 0);
    assert(kstrcmp(buffer, "12.34 seconds\n") == 0);
    assert(vfs_read("/proc/meminfo", buffer, sizeof(buffer), &length) == 0);
    assert(kstrstr(buffer, "HeapTotal: 4096 bytes") != 0);
    assert(vfs_stat_path("/proc/net", &stat) == 0 && stat.type == VFS_TYPE_DIRECTORY);
    assert(vfs_read("/proc/net/dev", buffer, sizeof(buffer), &length) == 0);
    assert(kstrstr(buffer, "e1000 unavailable") != 0);
    assert(vfs_read("/dev/null", buffer, sizeof(buffer), &length) == 0 && length == 0 && buffer[0] == 0);
    assert(vfs_write("/dev/null", "discard this") == 0);
    assert(vfs_read("/dev/zero", buffer, sizeof(buffer), &length) == 0);
    assert(length == sizeof(buffer) - 1 && buffer[sizeof(buffer)-1] == 0);
    for (usize i = 0; i < length; ++i) assert(buffer[i] == 0);
    assert(vfs_write("/proc/uptime", "forbidden") < 0);
    assert(vfs_write("/home/user/new.txt", "created through VFS") == 0);
    assert(vfs_mkdir("/home/user/new-folder") == 0);
    assert(vfs_remove("/home/user/new.txt") == 0);
    assert(vfs_list("/proc", 0, &entry) == 0);
    puts("VFS tests passed: mount routing, read-only policy, traversal rejection, DenFS adapter, procfs views, and devfs pseudo-files.");
    return 0;
}
