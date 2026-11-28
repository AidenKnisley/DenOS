#include "png.h"
#include "crc32.h"

#define PNG_MAX_HUFFMAN_BITS 15u
#define PNG_MAX_LITERAL_SYMBOLS 288u
#define PNG_MAX_DISTANCE_SYMBOLS 32u
#define PNG_MAX_CODE_LENGTH_SYMBOLS 19u

typedef struct png_bit_reader {
    const u8 *data;
    usize length;
    usize position;
    u32 bits;
    u8 bit_count;
} png_bit_reader_t;

typedef struct png_huffman {
    u16 count[PNG_MAX_HUFFMAN_BITS + 1u];
    u16 symbols[PNG_MAX_LITERAL_SYMBOLS];
} png_huffman_t;

static u32 png_read_be32(const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static u16 png_read_be16(const u8 *p) {
    return (u16)(((u16)p[0] << 8) | p[1]);
}

static u32 png_crc32_chunk(const u8 *type, const u8 *data, usize length) {
    u32 crc = 0xFFFFFFFFu;
    for (usize part = 0; part < length + 4u; ++part) {
        u8 value = part < 4u ? type[part] : data[part - 4u];
        crc ^= value;
        for (u32 bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return crc ^ 0xFFFFFFFFu;
}

static int png_read_bits(png_bit_reader_t *reader, u8 count, u32 *value) {
    if (!reader || !value || count > 16u) return -1;
    while (reader->bit_count < count) {
        if (reader->position >= reader->length) return -1;
        reader->bits |= (u32)reader->data[reader->position++] << reader->bit_count;
        reader->bit_count = (u8)(reader->bit_count + 8u);
    }
    *value = reader->bits & (count ? ((1u << count) - 1u) : 0u);
    reader->bits >>= count;
    reader->bit_count = (u8)(reader->bit_count - count);
    return 0;
}

static int png_build_huffman(png_huffman_t *tree, const u8 *lengths, u16 symbol_count) {
    u16 offsets[PNG_MAX_HUFFMAN_BITS + 1u];
    int left = 1;
    if (!tree || !lengths || !symbol_count || symbol_count > PNG_MAX_LITERAL_SYMBOLS) return -1;
    for (u16 i = 0; i <= PNG_MAX_HUFFMAN_BITS; ++i) tree->count[i] = 0;
    for (u16 symbol = 0; symbol < symbol_count; ++symbol) {
        if (lengths[symbol] > PNG_MAX_HUFFMAN_BITS) return -1;
        ++tree->count[lengths[symbol]];
    }
    if (tree->count[0] == symbol_count) return -1;
    for (u16 length = 1; length <= PNG_MAX_HUFFMAN_BITS; ++length) {
        left <<= 1;
        left -= tree->count[length];
        if (left < 0) return -1; /* Oversubscribed code tree. */
    }
    offsets[1] = 0;
    for (u16 length = 1; length < PNG_MAX_HUFFMAN_BITS; ++length)
        offsets[length + 1u] = (u16)(offsets[length] + tree->count[length]);
    for (u16 symbol = 0; symbol < symbol_count; ++symbol) {
        u8 length = lengths[symbol];
        if (length) tree->symbols[offsets[length]++] = symbol;
    }
    return 0;
}

static int png_decode_symbol(png_bit_reader_t *reader,
                             const png_huffman_t *tree, u16 *symbol) {
    u32 code = 0, first = 0, index = 0;
    for (u32 length = 1; length <= PNG_MAX_HUFFMAN_BITS; ++length) {
        u32 bit;
        if (png_read_bits(reader, 1, &bit) < 0) return -1;
        code |= bit;
        u32 count = tree->count[length];
        if (code >= first && code - first < count) {
            *symbol = tree->symbols[index + code - first];
            return 0;
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

static int png_inflate_codes(png_bit_reader_t *reader,
                             const png_huffman_t *literal_tree,
                             const png_huffman_t *distance_tree,
                             u8 *output, usize output_capacity, usize *output_length) {
    static const u16 length_base[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,
        131,163,195,227,258
    };
    static const u8 length_extra[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,
        5,5,5,5,0
    };
    static const u16 distance_base[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
        1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    static const u8 distance_extra[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,
        9,9,10,10,11,11,12,12,13,13
    };
    for (;;) {
        u16 symbol;
        if (png_decode_symbol(reader, literal_tree, &symbol) < 0) return -1;
        if (symbol < 256u) {
            if (*output_length >= output_capacity) return -1;
            output[(*output_length)++] = (u8)symbol;
        } else if (symbol == 256u) {
            return 0;
        } else {
            u32 extra = 0, distance_symbol;
            usize length, distance;
            if (symbol < 257u || symbol > 285u) return -1;
            u16 length_index = (u16)(symbol - 257u);
            if (png_read_bits(reader, length_extra[length_index], &extra) < 0) return -1;
            length = (usize)length_base[length_index] + extra;
            if (png_decode_symbol(reader, distance_tree, &symbol) < 0 || symbol >= 30u) return -1;
            distance_symbol = symbol;
            extra = 0;
            if (png_read_bits(reader, distance_extra[distance_symbol], &extra) < 0) return -1;
            distance = (usize)distance_base[distance_symbol] + extra;
            if (!distance || distance > *output_length || length > output_capacity - *output_length)
                return -1;
            for (usize i = 0; i < length; ++i) {
                output[*output_length] = output[*output_length - distance];
                ++*output_length;
            }
        }
    }
}

static int png_inflate_zlib(const u8 *input, usize input_length,
                            u8 *output, usize output_capacity, usize *output_length) {
    static const u8 code_order[PNG_MAX_CODE_LENGTH_SYMBOLS] = {
        16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
    };
    png_bit_reader_t reader;
    usize produced = 0;
    int final_block = 0;
    if (!input || !output || !output_length || input_length < 6u) return -1;
    if ((input[0] & 0x0Fu) != 8u || (input[0] >> 4) > 7u ||
        (((u16)input[0] << 8) | input[1]) % 31u != 0u || (input[1] & 0x20u)) return -1;
    reader.data = input + 2u;
    reader.length = input_length - 6u;
    reader.position = 0;
    reader.bits = 0;
    reader.bit_count = 0;

    while (!final_block) {
        u32 final_value, block_type;
        if (png_read_bits(&reader, 1, &final_value) < 0 ||
            png_read_bits(&reader, 2, &block_type) < 0) return -1;
        final_block = (int)final_value;
        if (block_type == 0u) {
            u32 length, complement;
            reader.bits = 0;
            reader.bit_count = 0;
            if (reader.position + 4u > reader.length) return -1;
            length = (u32)reader.data[reader.position] |
                     ((u32)reader.data[reader.position + 1u] << 8);
            complement = (u32)reader.data[reader.position + 2u] |
                         ((u32)reader.data[reader.position + 3u] << 8);
            reader.position += 4u;
            if ((length ^ 0xFFFFu) != complement ||
                length > output_capacity - produced ||
                length > reader.length - reader.position) return -1;
            for (u32 i = 0; i < length; ++i) output[produced++] = reader.data[reader.position++];
        } else if (block_type == 1u || block_type == 2u) {
            u8 literal_lengths[PNG_MAX_LITERAL_SYMBOLS];
            u8 distance_lengths[PNG_MAX_DISTANCE_SYMBOLS];
            png_huffman_t literal_tree, distance_tree;
            if (block_type == 1u) {
                for (u16 i = 0; i <= 287u; ++i) {
                    if (i <= 143u) literal_lengths[i] = 8;
                    else if (i <= 255u) literal_lengths[i] = 9;
                    else if (i <= 279u) literal_lengths[i] = 7;
                    else literal_lengths[i] = 8;
                }
                for (u16 i = 0; i < 32u; ++i) distance_lengths[i] = 5;
                if (png_build_huffman(&literal_tree, literal_lengths, 288u) < 0 ||
                    png_build_huffman(&distance_tree, distance_lengths, 32u) < 0) return -1;
            } else {
                u32 hlit, hdist, hclen;
                u8 code_lengths[PNG_MAX_CODE_LENGTH_SYMBOLS] = {0};
                png_huffman_t code_tree;
                u16 total, used = 0;
                if (png_read_bits(&reader, 5, &hlit) < 0 ||
                    png_read_bits(&reader, 5, &hdist) < 0 ||
                    png_read_bits(&reader, 4, &hclen) < 0) return -1;
                hlit += 257u; hdist += 1u; hclen += 4u;
                if (hlit > PNG_MAX_LITERAL_SYMBOLS || hdist > PNG_MAX_DISTANCE_SYMBOLS) return -1;
                for (u32 i = 0; i < hclen; ++i) {
                    u32 bits;
                    if (png_read_bits(&reader, 3, &bits) < 0) return -1;
                    code_lengths[code_order[i]] = (u8)bits;
                }
                if (png_build_huffman(&code_tree, code_lengths, PNG_MAX_CODE_LENGTH_SYMBOLS) < 0) return -1;
                total = (u16)(hlit + hdist);
                while (used < total) {
                    u16 symbol;
                    if (png_decode_symbol(&reader, &code_tree, &symbol) < 0) return -1;
                    if (symbol <= 15u) {
                        if (used < hlit) literal_lengths[used] = (u8)symbol;
                        else distance_lengths[used - hlit] = (u8)symbol;
                        ++used;
                    } else {
                        u32 extra, repeat;
                        u8 value = 0;
                        if (symbol == 16u) {
                            if (!used || png_read_bits(&reader, 2, &extra) < 0) return -1;
                            repeat = extra + 3u;
                            value = used <= hlit ? literal_lengths[used - 1u] : distance_lengths[used - hlit - 1u];
                        } else if (symbol == 17u) {
                            if (png_read_bits(&reader, 3, &extra) < 0) return -1;
                            repeat = extra + 3u;
                        } else if (symbol == 18u) {
                            if (png_read_bits(&reader, 7, &extra) < 0) return -1;
                            repeat = extra + 11u;
                        } else return -1;
                        if (repeat > (u32)(total - used)) return -1;
                        while (repeat--) {
                            if (used < hlit) literal_lengths[used] = value;
                            else distance_lengths[used - hlit] = value;
                            ++used;
                        }
                    }
                }
                if (!literal_lengths[256u] ||
                    png_build_huffman(&literal_tree, literal_lengths, (u16)hlit) < 0 ||
                    png_build_huffman(&distance_tree, distance_lengths, (u16)hdist) < 0) return -1;
            }
            if (png_inflate_codes(&reader, &literal_tree, &distance_tree,
                                  output, output_capacity, &produced) < 0) return -1;
        } else return -1;
    }
    if (reader.position != reader.length) return -1;
    {
        u32 s1 = 1u, s2 = 0u;
        for (usize i = 0; i < produced; ++i) {
            s1 = (s1 + output[i]) % 65521u;
            s2 = (s2 + s1) % 65521u;
        }
        u32 adler = (s2 << 16) | s1;
        if (adler != png_read_be32(input + input_length - 4u)) return -1;
    }
    *output_length = produced;
    return 0;
}

static u8 png_paeth(u8 left, u8 up, u8 upper_left) {
    int p = (int)left + (int)up - (int)upper_left;
    int pa = p - (int)left; if (pa < 0) pa = -pa;
    int pb = p - (int)up; if (pb < 0) pb = -pb;
    int pc = p - (int)upper_left; if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return left;
    return pb <= pc ? up : upper_left;
}

int png_decode_rgb32(const u8 *data, usize data_size,
                     u32 *pixels, usize pixel_capacity,
                     u8 *workspace, usize workspace_capacity,
                     u32 *width_out, u32 *height_out) {
    static const u8 signature[8] = {137,80,78,71,13,10,26,10};
    u8 palette[256][4];
    usize position = 8u, compressed_length = 0;
    u32 width = 0, height = 0;
    u8 bit_depth = 0, color_type = 0, channels = 0;
    u16 palette_entries = 0, transparency_length = 0;
    u8 transparency[256];
    int have_ihdr = 0, have_iend = 0, have_idat = 0, idat_closed = 0, have_palette = 0;
    if (!data || !pixels || !workspace || !width_out || !height_out ||
        data_size < sizeof(signature) || workspace_capacity < 64u) return -1;
    for (usize i = 0; i < sizeof(signature); ++i) if (data[i] != signature[i]) return -1;
    for (u16 i = 0; i < 256u; ++i) { palette[i][0] = palette[i][1] = palette[i][2] = 0; palette[i][3] = 255; transparency[i] = 255; }

    while (position + 12u <= data_size) {
        u32 chunk_length = png_read_be32(data + position);
        const u8 *type = data + position + 4u;
        const u8 *chunk = data + position + 8u;
        u32 stored_crc;
        if ((usize)chunk_length > data_size - position - 12u) return -1;
        stored_crc = png_read_be32(chunk + chunk_length);
        if (png_crc32_chunk(type, chunk, chunk_length) != stored_crc) return -1;
        if (type[0] == 'I' && type[1] == 'H' && type[2] == 'D' && type[3] == 'R') {
            if (have_ihdr || position != 8u || chunk_length != 13u) return -1;
            width = png_read_be32(chunk); height = png_read_be32(chunk + 4u);
            bit_depth = chunk[8]; color_type = chunk[9];
            if (!width || !height || bit_depth != 8u || chunk[10] != 0u ||
                chunk[11] != 0u || chunk[12] != 0u) return -1;
            if (color_type == 0u) channels = 1u;
            else if (color_type == 2u) channels = 3u;
            else if (color_type == 3u) channels = 1u;
            else if (color_type == 4u) channels = 2u;
            else if (color_type == 6u) channels = 4u;
            else return -1;
            have_ihdr = 1;
        } else if (type[0] == 'P' && type[1] == 'L' && type[2] == 'T' && type[3] == 'E') {
            if (!have_ihdr || have_idat ||
                (color_type != 3u && color_type != 2u && color_type != 6u) ||
                chunk_length == 0u || chunk_length > 768u || chunk_length % 3u) return -1;
            palette_entries = (u16)(chunk_length / 3u);
            for (u16 i = 0; i < palette_entries; ++i) {
                palette[i][0] = chunk[i * 3u];
                palette[i][1] = chunk[i * 3u + 1u];
                palette[i][2] = chunk[i * 3u + 2u];
            }
            have_palette = 1;
        } else if (type[0] == 't' && type[1] == 'R' && type[2] == 'N' && type[3] == 'S') {
            if (!have_ihdr || have_idat) return -1;
            if (color_type == 3u) {
                if (!have_palette || chunk_length > palette_entries) return -1;
                transparency_length = (u16)chunk_length;
                for (u16 i = 0; i < transparency_length; ++i) {
                    palette[i][3] = chunk[i]; transparency[i] = chunk[i];
                }
            } else if (color_type == 0u || color_type == 2u) {
                if (chunk_length != (color_type == 0u ? 2u : 6u)) return -1;
                transparency_length = (u16)chunk_length;
                for (u16 i = 0; i < transparency_length; ++i) transparency[i] = chunk[i];
            } else return -1;
        } else if (type[0] == 'I' && type[1] == 'D' && type[2] == 'A' && type[3] == 'T') {
            if (!have_ihdr || idat_closed || (color_type == 3u && !have_palette) ||
                chunk_length > workspace_capacity - compressed_length) return -1;
            for (u32 i = 0; i < chunk_length; ++i) workspace[compressed_length + i] = chunk[i];
            compressed_length += chunk_length;
            have_idat = 1;
        } else if (type[0] == 'I' && type[1] == 'E' && type[2] == 'N' && type[3] == 'D') {
            if (!have_idat || chunk_length != 0u) return -1;
            have_iend = 1;
            position += (usize)chunk_length + 12u;
            break;
        } else {
            if (have_idat) idat_closed = 1;
            /* Unknown critical chunks change image interpretation and are rejected. */
            if (type[0] >= 'A' && type[0] <= 'Z') return -1;
        }
        if (have_idat && !(type[0] == 'I' && type[1] == 'D' && type[2] == 'A' && type[3] == 'T'))
            idat_closed = 1;
        position += (usize)chunk_length + 12u;
    }
    if (!have_ihdr || !have_idat || !have_iend || position != data_size ||
        (color_type == 3u && !have_palette)) return -1;
    {
        usize pixel_count = (usize)width * height;
        usize row_bytes;
        usize raw_length;
        usize inflated_length = 0;
        u8 *raw;
        if (width && pixel_count / width != height) return -1;
        if (width > pixel_capacity || height > pixel_capacity || pixel_count > pixel_capacity) return -1;
        row_bytes = (usize)width * channels;
        if (row_bytes >= workspace_capacity - compressed_length ||
            height > (workspace_capacity - compressed_length) / (row_bytes + 1u)) return -1;
        raw_length = (row_bytes + 1u) * height;
        raw = workspace + compressed_length;
        if (png_inflate_zlib(workspace, compressed_length, raw,
                             raw_length, &inflated_length) < 0 || inflated_length != raw_length) return -1;
        for (u32 y = 0; y < height; ++y) {
            u8 *row = raw + (usize)y * (row_bytes + 1u) + 1u;
            u8 *previous = y ? row - (row_bytes + 1u) : 0;
            u8 filter = row[-1];
            if (filter > 4u) return -1;
            for (usize x = 0; x < row_bytes; ++x) {
                u8 left = x >= channels ? row[x - channels] : 0u;
                u8 up = previous ? previous[x] : 0u;
                u8 upper_left = previous && x >= channels ? previous[x - channels] : 0u;
                u8 predictor = 0;
                if (filter == 1u) predictor = left;
                else if (filter == 2u) predictor = up;
                else if (filter == 3u) predictor = (u8)(((u16)left + up) / 2u);
                else if (filter == 4u) predictor = png_paeth(left, up, upper_left);
                row[x] = (u8)(row[x] + predictor);
            }
        }
        for (usize i = 0; i < pixel_count; ++i) {
            const u8 *row = raw + (i / width) * (row_bytes + 1u) + 1u;
            const u8 *sample = row + (i % width) * channels;
            u32 red, green, blue, alpha = 255u;
            if (color_type == 0u) {
                red = green = blue = sample[0];
                if (transparency_length == 2u && sample[0] == png_read_be16(transparency)) alpha = 0u;
            } else if (color_type == 2u) {
                red = sample[0]; green = sample[1]; blue = sample[2];
                if (transparency_length == 6u &&
                    sample[0] == png_read_be16(transparency) &&
                    sample[1] == png_read_be16(transparency + 2u) &&
                    sample[2] == png_read_be16(transparency + 4u)) alpha = 0u;
            } else if (color_type == 3u) {
                u8 index = sample[0];
                if (index >= palette_entries) return -1;
                red = palette[index][0]; green = palette[index][1]; blue = palette[index][2]; alpha = palette[index][3];
            } else if (color_type == 4u) {
                red = green = blue = sample[0]; alpha = sample[1];
            } else {
                red = sample[0]; green = sample[1]; blue = sample[2]; alpha = sample[3];
            }
            /* The graphics API consumes RGB rather than alpha; composite over white. */
            if (alpha != 255u) {
                red = (red * alpha + 255u * (255u - alpha) + 127u) / 255u;
                green = (green * alpha + 255u * (255u - alpha) + 127u) / 255u;
                blue = (blue * alpha + 255u * (255u - alpha) + 127u) / 255u;
            }
            pixels[i] = (red << 16) | (green << 8) | blue;
        }
    }
    *width_out = width;
    *height_out = height;
    return 0;
}
