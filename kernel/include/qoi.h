#ifndef DENOS_QOI_H
#define DENOS_QOI_H

#include "types.h"

/* Decode a QOI image into opaque 0x00RRGGBB pixels. Dimensions must match
 * the caller's expected canvas, and the destination capacity is checked. */
int qoi_decode_rgb32(const u8 *data, usize data_size,
                     u32 *pixels, usize pixel_capacity,
                     u32 expected_width, u32 expected_height);

#endif
