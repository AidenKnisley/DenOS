#ifndef DENOS_BOOTARGS_H
#define DENOS_BOOTARGS_H
#include "types.h"
#define BOOTARGS_CAP 256u
void bootargs_init(const void *multiboot_info, u32 total_size);
const char *bootargs_get(void);
int bootargs_has(const char *option);
int bootargs_value(const char *key, char *out, usize capacity);
#endif
