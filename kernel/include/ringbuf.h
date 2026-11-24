#ifndef DENOS_RINGBUF_H
#define DENOS_RINGBUF_H
#include "types.h"
typedef struct ringbuf { u8 *data; usize capacity; usize head; usize tail; usize count; u64 dropped; } ringbuf_t;
int ringbuf_init(ringbuf_t *buffer,u8 *storage,usize capacity);
int ringbuf_push(ringbuf_t *buffer,u8 value);
int ringbuf_pop(ringbuf_t *buffer,u8 *value);
usize ringbuf_count(const ringbuf_t *buffer);
void ringbuf_clear(ringbuf_t *buffer);
#endif
