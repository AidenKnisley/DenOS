#ifndef DENOS_SHA256_H
#define DENOS_SHA256_H
#include "types.h"
void sha256_digest(const void *data, usize length, u8 digest[32]);
void sha256_hex(const u8 digest[32], char output[65]);
#endif
