#ifndef DENOS_KFORMAT_H
#define DENOS_KFORMAT_H

#include "types.h"
#include <stdarg.h>

/* Bounded formatter. Returns the number of characters that would have been
 * written, excluding the final NUL, just like snprintf. */
int kvsnprintf(char *buffer, usize capacity, const char *format, va_list args);
int ksnprintf(char *buffer, usize capacity, const char *format, ...);

#endif
