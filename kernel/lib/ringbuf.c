#include "ringbuf.h"
int ringbuf_init(ringbuf_t *b,u8 *storage,usize cap) { if(!b||!storage||!cap) return -1; b->data=storage;b->capacity=cap;b->head=0;b->tail=0;b->count=0;b->dropped=0;return 0; }
int ringbuf_push(ringbuf_t *b,u8 value) { if(!b||!b->data||!b->capacity) return -1; if(b->count==b->capacity) { b->tail=(b->tail+1u)%b->capacity; --b->count; ++b->dropped; } b->data[b->head]=value;b->head=(b->head+1u)%b->capacity;++b->count;return 0; }
int ringbuf_pop(ringbuf_t *b,u8 *value) { if(!b||!value||!b->data||!b->count) return -1; *value=b->data[b->tail];b->tail=(b->tail+1u)%b->capacity;--b->count;return 0; }
usize ringbuf_count(const ringbuf_t *b) { return b?b->count:0; }
void ringbuf_clear(ringbuf_t *b) { if(b) { b->head=0;b->tail=0;b->count=0; } }
