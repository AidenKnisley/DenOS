#ifndef AIDEN_FS_H
#define AIDEN_FS_H
#include "types.h"
void fs_init(void);
int fs_write(const char *name, const char *data);
const char *fs_read(const char *name);
int fs_remove(const char *name);
int fs_touch(const char *name);
void fs_list(void);
usize fs_file_count(void);
usize fs_bytes_used(void);
int fs_is_persistent(void);
const char *fs_storage_name(void);
#endif
