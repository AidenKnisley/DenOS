#ifndef DENOS_UTF8_H
#define DENOS_UTF8_H
#include "types.h"
int utf8_decode(const u8 *text, usize length, u32 *codepoint, usize *consumed);
int utf8_validate(const u8 *text, usize length);
#endif
