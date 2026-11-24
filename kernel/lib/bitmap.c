#include "bitmap.h"
int bitmap_init(bitmap_t *map,u8 *storage,usize bits) { if(!map||(!storage&&bits)) return -1; map->data=storage; map->bit_count=bits; for(usize i=0;i<(bits+7u)/8u;i++) storage[i]=0; return 0; }
int bitmap_get(const bitmap_t *map,usize bit) { if(!map||!map->data||bit>=map->bit_count) return -1; return (map->data[bit/8u]>>(bit%8u))&1u; }
int bitmap_set(bitmap_t *map,usize bit,int used) { if(!map||!map->data||bit>=map->bit_count) return -1; u8 mask=(u8)(1u<<(bit%8u)); if(used) map->data[bit/8u]|=mask; else map->data[bit/8u]&=(u8)~mask; return 0; }
int bitmap_find_free(bitmap_t *map,usize *bit) { if(!map||!map->data||!bit) return -1; for(usize i=0;i<map->bit_count;i++) if(!bitmap_get(map,i)) { bitmap_set(map,i,1); *bit=i; return 0; } return -1; }
usize bitmap_count_set(const bitmap_t *map) { usize count=0; if(!map||!map->data) return 0; for(usize i=0;i<map->bit_count;i++) if(bitmap_get(map,i)==1) ++count; return count; }
