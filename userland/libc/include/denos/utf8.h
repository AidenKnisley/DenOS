#ifndef DENOS_LIBC_UTF8_H
#define DENOS_LIBC_UTF8_H
#include <stddef.h>
int den_utf8_next(const unsigned char*,size_t,size_t*,unsigned int*);
#endif
