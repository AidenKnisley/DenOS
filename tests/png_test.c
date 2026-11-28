#include "png.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "PNG test failure at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    exit(1); \
} } while (0)

#define PNG_WORK_CAP (60000u + 640u * 480u * 4u + 1024u)
#define PNG_PIXEL_CAP (640u * 480u)

typedef struct png_fixture {
    u8 *bytes;
    usize length;
    u32 *pixels;
    u8 *workspace;
    u32 width;
    u32 height;
} png_fixture_t;

static png_fixture_t load_fixture(const char *path) {
    png_fixture_t fixture = {0};
    FILE *file = fopen(path, "rb");
    CHECK(file != 0);
    CHECK(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    CHECK(size > 0 && size < 60000);
    CHECK(fseek(file, 0, SEEK_SET) == 0);
    fixture.bytes = (u8 *)malloc((usize)size);
    fixture.pixels = (u32 *)malloc(PNG_PIXEL_CAP * sizeof(u32));
    fixture.workspace = (u8 *)malloc(PNG_WORK_CAP);
    CHECK(fixture.bytes && fixture.pixels && fixture.workspace);
    fixture.length = (usize)size;
    CHECK(fread(fixture.bytes, 1, fixture.length, file) == fixture.length);
    fclose(file);
    CHECK(png_decode_rgb32(fixture.bytes, fixture.length, fixture.pixels,
                           PNG_PIXEL_CAP, fixture.workspace, PNG_WORK_CAP,
                           &fixture.width, &fixture.height) == 0);
    return fixture;
}

static void release_fixture(png_fixture_t *fixture) {
    free(fixture->bytes);
    free(fixture->pixels);
    free(fixture->workspace);
    fixture->bytes = 0;
    fixture->pixels = 0;
    fixture->workspace = 0;
}

int main(void) {
    png_fixture_t image = load_fixture("tests/fixtures/png/rgba-filters.png");
    CHECK(image.width == 3u && image.height == 5u);
    for (u32 y = 0; y < image.height; ++y) {
        for (u32 x = 0; x < image.width; ++x) {
            u32 red = (x * 70u + y * 11u + 3u) & 255u;
            u32 green = (y * 40u + x * 9u + 5u) & 255u;
            u32 blue = (x * 17u + y * 31u + 7u) & 255u;
            CHECK(image.pixels[y * image.width + x] == ((red << 16) | (green << 8) | blue));
        }
    }
    release_fixture(&image);

    image = load_fixture("tests/fixtures/png/indexed-alpha.png");
    CHECK(image.width == 4u && image.height == 3u);
    CHECK(image.pixels[0] == 0x00FF0000u); /* Red, opaque. */
    CHECK(image.pixels[1] == 0x007FFF7Fu); /* Green, half transparent over white. */
    CHECK(image.pixels[2] == 0x00FFFFFFu); /* Blue, fully transparent. */
    CHECK(image.pixels[3] == 0x0014283Cu);
    release_fixture(&image);

    image = load_fixture("tests/fixtures/png/gray-alpha.png");
    CHECK(image.width == 2u && image.height == 2u);
    CHECK(image.pixels[0] == 0x00282828u);
    CHECK(image.pixels[1] == 0x00E3E3E3u);
    CHECK(image.pixels[2] == 0x00D8D8D8u);
    CHECK(image.pixels[3] == 0x00FFFFFFu);
    release_fixture(&image);

    image = load_fixture("tests/fixtures/png/dynamic-rgb.png");
    CHECK(image.width == 64u && image.height == 48u);
    for (u32 y = 0; y < image.height; y += 7u) {
        for (u32 x = 0; x < image.width; x += 9u) {
            u32 red = ((x * 13u + y * 7u) & 15u) * 16u;
            u32 green = ((x * 5u + y * 11u + 3u) & 15u) * 16u;
            u32 blue = ((x * 9u + y * 3u + 7u) & 15u) * 16u;
            CHECK(image.pixels[y * image.width + x] == ((red << 16) | (green << 8) | blue));
        }
    }
    image.bytes[image.length - 1u] ^= 0x01u;
    CHECK(png_decode_rgb32(image.bytes, image.length, image.pixels,
                           PNG_PIXEL_CAP, image.workspace, PNG_WORK_CAP,
                           &image.width, &image.height) < 0);
    image.bytes[image.length - 1u] ^= 0x01u;
    CHECK(png_decode_rgb32(image.bytes, image.length - 4u, image.pixels,
                           PNG_PIXEL_CAP, image.workspace, PNG_WORK_CAP,
                           &image.width, &image.height) < 0);
    release_fixture(&image);

    puts("PNG decoder tests passed: PNG chunks/CRC, split IDAT, RGB/RGBA/indexed/grayscale-alpha, all row filters, stored/fixed/dynamic DEFLATE, transparency, and malformed input.");
    return 0;
}
