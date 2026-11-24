#ifndef DENOS_LIBC_HASHMAP_H
#define DENOS_LIBC_HASHMAP_H
#include <stddef.h>
typedef struct{char*key;void*value;}den_map_slot;typedef struct{den_map_slot*slots;size_t cap,len;}den_map;int den_map_init(den_map*,size_t);void den_map_free(den_map*);int den_map_put(den_map*,const char*,void*);void*den_map_get(const den_map*,const char*);
#endif
