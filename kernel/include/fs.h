#ifndef DENOS_FS_H
#define DENOS_FS_H
#include "types.h"

enum fs_error {
    FS_ERROR_NONE = 0,
    FS_ERROR_INVALID_ARGUMENT,
    FS_ERROR_INVALID_NAME,
    FS_ERROR_NAME_TOO_LONG,
    FS_ERROR_DATA_TOO_LARGE,
    FS_ERROR_NOT_FOUND,
    FS_ERROR_NO_SPACE,
    FS_ERROR_IO,
    FS_ERROR_BUFFER_TOO_SMALL,
    FS_ERROR_ALREADY_EXISTS,
    FS_ERROR_NOT_DIRECTORY,
    FS_ERROR_DIRECTORY_NOT_EMPTY,
    FS_ERROR_IS_DIRECTORY
};

#define FS_PATH_CAPACITY 256u
#define FS_FILE_MAX_BYTES 65535u
#define FS_ENTRY_FILE 1
#define FS_ENTRY_DIRECTORY 2

void fs_init(void);
void fs_seed_user_tree(void);
const char *fs_last_error_message(void);
int fs_write(const char *path, const char *data);
const char *fs_read(const char *path);
int fs_remove(const char *path);
int fs_copy(const char *source, const char *destination);
int fs_move(const char *source, const char *destination);
int fs_rename(const char *source, const char *destination);
int fs_exists(const char *path);
int fs_is_file(const char *path);
int fs_touch(const char *path);
int fs_mkdir(const char *path);
int fs_mkdir_p(const char *path);
int fs_is_directory(const char *path);
void fs_list(void);
usize fs_file_count(void);
usize fs_entry_count(void);
int fs_get_entry(usize index, char *name, usize name_capacity, usize *size);
int fs_get_entry_in_dir(const char *directory, usize index,
                        char *name, usize name_capacity,
                        usize *size, int *entry_type);
usize fs_entry_count_in_dir(const char *directory);
usize fs_bytes_used(void);
usize fs_storage_capacity(void);
usize fs_storage_free(void);
usize fs_storage_reserved(void);
usize fs_disk_size_bytes(void);
u32 fs_disk_sector_count(void);
const char *fs_disk_model(void);
int fs_is_persistent(void);
const char *fs_storage_name(void);
#endif
