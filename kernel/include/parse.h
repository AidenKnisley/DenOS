#ifndef DENOS_PARSE_H
#define DENOS_PARSE_H
#include "types.h"
int parse_u64(const char *text, u64 *value);
int parse_u32(const char *text, u32 *value);
int parse_ipv4(const char *text, u8 address[4]);
#endif
