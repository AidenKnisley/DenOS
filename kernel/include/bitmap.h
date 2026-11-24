#ifndef DENOS_BITMAP_H
#define DENOS_BITMAP_H
#include "types.h"
typedef struct bitmap { u8 *data; usize bit_count; } bitmap_t;
int bitmap_init(bitmap_t *map, u8 *storage, usize bit_count);
int bitmap_get(const bitmap_t *map, usize bit);
int bitmap_set(bitmap_t *map, usize bit, int used);
int bitmap_find_free(bitmap_t *map, usize *bit);
usize bitmap_count_set(const bitmap_t *map);
#endif
