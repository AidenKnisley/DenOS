#ifndef DENOS_LIBC_MEMORY_H
#define DENOS_LIBC_MEMORY_H
#include <stddef.h>
void*den_memset(void*,int,size_t); void*den_memcpy(void*,const void*,size_t); void*den_memmove(void*,const void*,size_t); int den_memcmp(const void*,const void*,size_t);
#endif
