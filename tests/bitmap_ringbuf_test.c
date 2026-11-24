#include <stdio.h>
#include "bitmap.h"
#include "ringbuf.h"
int main(void) { u8 bits[2],storage[3],v;usize bit;bitmap_t map;ringbuf_t q;if(bitmap_init(&map,bits,10)<0)return 1;if(bitmap_find_free(&map,&bit)<0||bit!=0)return 2;if(bitmap_set(&map,9,1)<0||bitmap_count_set(&map)!=2)return 3;if(bitmap_set(&map,0,0)<0||bitmap_get(&map,0)!=0)return 4;if(ringbuf_init(&q,storage,3)<0)return 5;ringbuf_push(&q,1);ringbuf_push(&q,2);ringbuf_push(&q,3);ringbuf_push(&q,4);if(q.dropped!=1||ringbuf_pop(&q,&v)<0||v!=2)return 6;ringbuf_clear(&q);if(ringbuf_count(&q)!=0)return 7;puts("Bitmap and ring buffer tests passed.");return 0; }
