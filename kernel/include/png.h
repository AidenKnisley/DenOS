#ifndef DENOS_PNG_H
#define DENOS_PNG_H

#include "types.h"

/* Decode a non-interlaced, 8-bit PNG into opaque 0x00RRGGBB pixels.
 * Supports grayscale, grayscale+alpha, RGB, RGBA, and indexed-color images.
 * The caller supplies scratch storage for concatenated IDAT data and the
 * decompressed scanlines; all bounds are checked before writing. */
int png_decode_rgb32(const u8 *data, usize data_size,
                     u32 *pixels, usize pixel_capacity,
                     u8 *workspace, usize workspace_capacity,
                     u32 *width, u32 *height);

#endif
