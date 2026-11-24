#ifndef DENOS_ENTROPY_H
#define DENOS_ENTROPY_H
#include "types.h"
/* Fast non-cryptographic kernel PRNG; do not use for keys or secrets. */
void entropy_init(void);
u64 entropy_next(void);
u32 entropy_u32(void);
int entropy_is_seeded(void);
#endif
