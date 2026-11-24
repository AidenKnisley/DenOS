#include "qoi.h"

#define QOI_HEADER_SIZE 14u
#define QOI_END_SIZE 8u
#define QOI_OP_RGB 0xFEu
#define QOI_OP_RGBA 0xFFu

typedef struct qoi_pixel {
    u8 red;
    u8 green;
    u8 blue;
    u8 alpha;
} qoi_pixel_t;

static u32 read_be32(const u8 *bytes) {
    return ((u32)bytes[0] << 24) | ((u32)bytes[1] << 16) |
           ((u32)bytes[2] << 8) | (u32)bytes[3];
}

static u32 pixel_hash(qoi_pixel_t pixel) {
    return ((u32)pixel.red * 3u + (u32)pixel.green * 5u +
            (u32)pixel.blue * 7u + (u32)pixel.alpha * 11u) & 63u;
}

static u32 pixel_rgb(qoi_pixel_t pixel) {
    return ((u32)pixel.red << 16) | ((u32)pixel.green << 8) | (u32)pixel.blue;
}

int qoi_decode_rgb32(const u8 *data, usize data_size,
                     u32 *pixels, usize pixel_capacity,
                     u32 expected_width, u32 expected_height) {
    qoi_pixel_t index[64];
    qoi_pixel_t previous = {0, 0, 0, 255};
    usize position = QOI_HEADER_SIZE;
    usize output = 0;
    usize pixel_count;

    if (!data || !pixels || data_size < QOI_HEADER_SIZE + QOI_END_SIZE ||
        !expected_width || !expected_height) return -1;
    if (data[0] != 'q' || data[1] != 'o' || data[2] != 'i' || data[3] != 'f') return -1;
    if (read_be32(data + 4) != expected_width ||
        read_be32(data + 8) != expected_height) return -1;
    if ((data[12] != 3u && data[12] != 4u) || data[13] > 1u) return -1;
    pixel_count = (usize)expected_width * (usize)expected_height;
    if (expected_width && pixel_count / (usize)expected_width != expected_height) return -1;
    if (pixel_count > pixel_capacity) return -1;
    for (usize i = 0; i < 64u; i++) {
        index[i].red = 0; index[i].green = 0; index[i].blue = 0; index[i].alpha = 0;
    }

    while (output < pixel_count) {
        u8 opcode;
        if (position >= data_size - QOI_END_SIZE) return -1;
        opcode = data[position++];
        if (opcode == QOI_OP_RGB) {
            if (position + 3u > data_size - QOI_END_SIZE) return -1;
            previous.red = data[position++];
            previous.green = data[position++];
            previous.blue = data[position++];
        } else if (opcode == QOI_OP_RGBA) {
            if (position + 4u > data_size - QOI_END_SIZE) return -1;
            previous.red = data[position++];
            previous.green = data[position++];
            previous.blue = data[position++];
            previous.alpha = data[position++];
        } else {
            u8 tag = opcode & 0xC0u;
            if (tag == 0x00u) {
                previous = index[opcode & 0x3Fu];
            } else if (tag == 0x40u) {
                previous.red = (u8)(previous.red + ((opcode >> 4) & 0x03u) - 2);
                previous.green = (u8)(previous.green + ((opcode >> 2) & 0x03u) - 2);
                previous.blue = (u8)(previous.blue + (opcode & 0x03u) - 2);
            } else if (tag == 0x80u) {
                i32 green_delta, red_delta, blue_delta;
                u8 second;
                if (position >= data_size - QOI_END_SIZE) return -1;
                second = data[position++];
                green_delta = (i32)(opcode & 0x3Fu) - 32;
                red_delta = green_delta + (i32)(second >> 4) - 8;
                blue_delta = green_delta + (i32)(second & 0x0Fu) - 8;
                previous.red = (u8)(previous.red + red_delta);
                previous.green = (u8)(previous.green + green_delta);
                previous.blue = (u8)(previous.blue + blue_delta);
            } else {
                usize run = (usize)(opcode & 0x3Fu) + 1u;
                if (run > pixel_count - output) return -1;
                u32 color = pixel_rgb(previous);
                while (run--) pixels[output++] = color;
                continue;
            }
        }
        index[pixel_hash(previous)] = previous;
        pixels[output++] = pixel_rgb(previous);
    }

    /* The eight-byte end marker is part of the format and is checked rather
       than allowing malformed/truncated assets to pass as successful loads. */
    for (usize i = 0; i < QOI_END_SIZE; i++) {
        if (data[position + i] != (i == QOI_END_SIZE - 1u ? 1u : 0u)) return -1;
    }
    return 0;
}
