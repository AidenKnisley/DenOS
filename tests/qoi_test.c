#include "qoi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *message) {
    fprintf(stderr, "QOI test FAILED: %s\n", message);
    exit(1);
}

int main(void) {
    /* A minimal 2x2 RGB image encoded as four QOI_OP_RGB chunks. */
    static const u8 image[] = {
        'q','o','i','f', 0,0,0,2, 0,0,0,2, 3,0,
        0xFE,255,0,0,
        0xFE,0,255,0,
        0xFE,0,0,255,
        0xFE,255,255,255,
        0,0,0,0,0,0,0,1
    };
    static const u32 expected[] = {0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0x00FFFFFFu};
    u32 pixels[4] = {0,0,0,0};
    u8 broken[sizeof(image)];

    if (qoi_decode_rgb32(image, sizeof(image), pixels, 4, 2, 2) != 0)
        fail("valid RGB image did not decode");
    if (memcmp(pixels, expected, sizeof(expected)) != 0)
        fail("decoded pixel colors are incorrect");
    if (qoi_decode_rgb32(image, sizeof(image), pixels, 3, 2, 2) == 0)
        fail("decoder accepted insufficient destination capacity");
    if (qoi_decode_rgb32(image, sizeof(image) - 1, pixels, 4, 2, 2) == 0)
        fail("decoder accepted missing end marker byte");
    if (qoi_decode_rgb32(image, sizeof(image), pixels, 4, 3, 2) == 0)
        fail("decoder accepted unexpected dimensions");
    if (qoi_decode_rgb32(image, 10, pixels, 4, 2, 2) == 0)
        fail("decoder accepted a truncated header");

    memcpy(broken, image, sizeof(image));
    broken[0] = 'x';
    if (qoi_decode_rgb32(broken, sizeof(broken), pixels, 4, 2, 2) == 0)
        fail("decoder accepted an invalid magic signature");

    memcpy(broken, image, sizeof(image));
    broken[sizeof(broken) - 1] = 0;
    if (qoi_decode_rgb32(broken, sizeof(broken), pixels, 4, 2, 2) == 0)
        fail("decoder accepted an invalid end marker");

    puts("QOI decoder tests passed: RGB, dimensions, capacity and malformed input");
    return 0;
}
