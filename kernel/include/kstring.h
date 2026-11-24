#ifndef DENOS_KSTRING_H
#define DENOS_KSTRING_H

#include "types.h"

/* Small freestanding string/memory library. Names are prefixed to avoid
 * colliding with compiler built-ins or a future hosted C library. */
usize kstrlen(const char *text);
usize kstrnlen(const char *text, usize limit);
int kstrcmp(const char *left, const char *right);
int kstrncmp(const char *left, const char *right, usize limit);
usize kstrlcpy(char *destination, const char *source, usize capacity);
void *kmemset(void *destination, int value, usize count);
void *kmemcpy(void *destination, const void *source, usize count);
void *kmemmove(void *destination, const void *source, usize count);
int kmemcmp(const void *left, const void *right, usize count);
char *kstrchr(const char *text, int character);
char *kstrrchr(const char *text, int character);
char *kstrstr(const char *text, const char *pattern);

#endif
