#include "qoi.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define WIDTH 1600u
#define HEIGHT 900u
#define PIXEL_COUNT ((size_t)WIDTH * (size_t)HEIGHT)
#define BASE "resources/graphics-lab/fixtures/"

typedef struct { const char *name; } Fixture;
static const Fixture fixtures[] = {
    {"01-color-calibration"}, {"02-pixel-grid"}, {"03-rgb-gradients"},
    {"04-alpha-ramps"}, {"05-geometry-clipping"}, {"06-high-entropy-stress"},
    {"07-texture-noise"}, {"08-palette-atlas"}, {"09-ordered-dither"},
    {"10-scanline-edges"}
};

static uint8_t *read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    uint8_t *data;
    long size;
    if (!file) { fprintf(stderr, "cannot open fixture: %s\n", path); return NULL; }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) { fclose(file); return NULL; }
    data = (uint8_t *)malloc((size_t)size ? (size_t)size : 1u);
    if (!data || fread(data, 1, (size_t)size, file) != (size_t)size) {
        free(data); fclose(file); return NULL;
    }
    fclose(file); *length = (size_t)size; return data;
}

int main(void) {
    uint32_t *pixels = (uint32_t *)malloc(PIXEL_COUNT * sizeof(uint32_t));
    if (!pixels) { fputs("could not allocate fixture canvas\n", stderr); return 1; }
    for (size_t f = 0; f < sizeof(fixtures) / sizeof(fixtures[0]); ++f) {
        char qoi_path[160], rgba_path[160];
        size_t qoi_length = 0, rgba_length = 0;
        uint8_t *qoi, *rgba;
        snprintf(qoi_path, sizeof(qoi_path), BASE "%s.qoi", fixtures[f].name);
        snprintf(rgba_path, sizeof(rgba_path), BASE "%s.rgba", fixtures[f].name);
        qoi = read_file(qoi_path, &qoi_length);
        rgba = read_file(rgba_path, &rgba_length);
        if (!qoi || !rgba || rgba_length != PIXEL_COUNT * 4u) {
            fprintf(stderr, "invalid fixture files: %s\n", fixtures[f].name);
            free(qoi); free(rgba); free(pixels); return 1;
        }
        if (qoi_decode_rgb32(qoi, qoi_length, pixels, PIXEL_COUNT, WIDTH, HEIGHT) != 0) {
            fprintf(stderr, "QOI decode failed: %s\n", fixtures[f].name);
            free(qoi); free(rgba); free(pixels); return 1;
        }
        for (size_t i = 0; i < PIXEL_COUNT; ++i) {
            uint32_t expected = ((uint32_t)rgba[i * 4u] << 16) |
                                ((uint32_t)rgba[i * 4u + 1u] << 8) |
                                (uint32_t)rgba[i * 4u + 2u];
            if (pixels[i] != expected) {
                fprintf(stderr, "pixel mismatch: %s at pixel %lu\n",
                        fixtures[f].name, (unsigned long)i);
                free(qoi); free(rgba); free(pixels); return 1;
            }
        }
        printf("QOI fixture passed: %s (%zu QOI bytes, %u x %u)\n",
               fixtures[f].name, qoi_length, WIDTH, HEIGHT);
        free(qoi); free(rgba);
    }
    free(pixels);
    puts("All graphics reference fixtures decoded and matched their RGBA golden surfaces.");
    return 0;
}
