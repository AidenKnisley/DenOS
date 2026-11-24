#ifndef DENOS_LIBC_STDLIB_H
#define DENOS_LIBC_STDLIB_H
#include <stddef.h>
long den_strtol(const char*,char**,int); int den_atoi(const char*); int den_abs(int); void den_qsort(void*,size_t,size_t,int(*)(const void*,const void*));
#endif
