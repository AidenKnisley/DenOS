#include <denos/ringbuf.h>
#include <stdlib.h>
int den_rb_init(den_ringbuf*r,size_t cap){if(!r||!cap)return-1;r->data=malloc(cap);if(!r->data)return-1;r->cap=cap;r->head=r->len=0;return 0;}
void den_rb_free(den_ringbuf*r){if(r){free(r->data);r->data=NULL;r->cap=r->head=r->len=0;}}
int den_rb_push(den_ringbuf*r,unsigned char c){if(!r||r->len==r->cap)return-1;r->data[(r->head+r->len)%r->cap]=c;r->len++;return 0;}
int den_rb_pop(den_ringbuf*r,unsigned char*out){if(!r||!out||!r->len)return-1;*out=r->data[r->head];r->head=(r->head+1)%r->cap;r->len--;return 0;}
