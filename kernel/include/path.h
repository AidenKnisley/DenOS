#ifndef DENOS_PATH_H
#define DENOS_PATH_H

#include "types.h"

/* DenFS v4 stores up to 255 bytes per path. Paths omit a leading slash. */
int fs_name_valid(const char *name);
int fs_path_valid(const char *path);
int fs_path_normalize(const char *base, const char *input,
                      char *output, usize capacity);
int fs_path_parent(const char *path, char *output, usize capacity);
const char *fs_path_leaf(const char *path);
int fs_name_is_hidden(const char *name);
const char *fs_name_extension(const char *name);

#endif
