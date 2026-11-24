#ifndef DENOS_CHECKSUM_H
#define DENOS_CHECKSUM_H

#include "types.h"

/* CRC-32/ISO-HDLC and FNV-1a are used for diagnostics and data fingerprints.
 * They detect accidental changes; they are not cryptographic signatures. */
u32 kcrc32(const void *data, usize length);
u32 kfnv1a32(const void *data, usize length);

#endif
