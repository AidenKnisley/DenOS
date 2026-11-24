#ifndef DENOS_VFS_H
#define DENOS_VFS_H

#include "types.h"

#define VFS_MOUNT_CAP 8
#define VFS_PATH_CAP 64
#define VFS_NAME_CAP 64
#define VFS_MOUNT_READ_ONLY 0x01u
#define VFS_TYPE_FILE 1u
#define VFS_TYPE_DIRECTORY 2u

typedef struct vfs_dirent {
    char name[VFS_NAME_CAP];
    usize size;
    u8 type;
} vfs_dirent_t;

typedef struct vfs_stat {
    usize size;
    u8 type;
    u8 read_only;
} vfs_stat_t;

typedef struct vfs_operations {
    int (*read)(void *context, const char *path, char *buffer,
                usize capacity, usize *length);
    int (*write)(void *context, const char *path, const char *data);
    int (*mkdir)(void *context, const char *path);
    int (*remove)(void *context, const char *path);
    int (*stat)(void *context, const char *path, vfs_stat_t *result);
    int (*list)(void *context, const char *path, usize index,
                vfs_dirent_t *result);
} vfs_operations_t;

typedef struct vfs_mount_info {
    char path[VFS_PATH_CAP];
    char name[24];
    u32 flags;
    u32 id;
} vfs_mount_info_t;

/* Mounts are resolved by longest path-prefix match. All public paths are
 * absolute, slash-separated paths (for example /home/user/readme.txt). */
int vfs_init(void);
int vfs_mount(const char *mount_path, const char *name,
              const vfs_operations_t *operations, void *context, u32 flags);
int vfs_read(const char *path, char *buffer, usize capacity, usize *length);
int vfs_write(const char *path, const char *data);
int vfs_mkdir(const char *path);
int vfs_remove(const char *path);
int vfs_stat_path(const char *path, vfs_stat_t *result);
int vfs_list(const char *path, usize index, vfs_dirent_t *result);
int vfs_mount_get(usize index, vfs_mount_info_t *result);
usize vfs_mount_count(void);
const char *vfs_last_error(void);

#endif
