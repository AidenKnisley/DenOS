#ifndef DENOS_LIBC_RINGBUF_H
#define DENOS_LIBC_RINGBUF_H
#include <stddef.h>
typedef struct{unsigned char*data;size_t cap,head,len;}den_ringbuf;int den_rb_init(den_ringbuf*,size_t);void den_rb_free(den_ringbuf*);int den_rb_push(den_ringbuf*,unsigned char);int den_rb_pop(den_ringbuf*,unsigned char*);
#endif
