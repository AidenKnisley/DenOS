#ifndef AIDEN_MEMORY_H
#define AIDEN_MEMORY_H
#include "types.h"
void memory_init(void);
void *kmalloc(usize size);
usize memory_used(void);
usize memory_capacity(void);
#endif
