#!/usr/bin/env python3
"""Regenerate the tiny deterministic PNG decoder fixtures."""
from pathlib import Path
import struct
import zlib

OUT = Path(__file__).parent / "fixtures" / "png"
OUT.mkdir(parents=True, exist_ok=True)


def chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def write_png(name, width, height, color_type, rows, *, palette=None, transparency=None, level=6):
    ihdr = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
    raw = b"".join(rows)
    compressed = zlib.compress(raw, level)
    image = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
    if palette is not None:
        image += chunk(b"PLTE", palette)
    if transparency is not None:
        image += chunk(b"tRNS", transparency)
    split = max(1, len(compressed) // 2)
    image += chunk(b"IDAT", compressed[:split])
    image += chunk(b"IDAT", compressed[split:])
    image += chunk(b"IEND", b"")
    (OUT / name).write_bytes(image)


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


# RGBA image: each row uses a different PNG filter (None, Sub, Up, Average, Paeth).
width, height = 3, 5
previous = bytes(width * 4)
rows = []
for y in range(height):
    raw = bytearray()
    for x in range(width):
        raw.extend(((x * 70 + y * 11 + 3) & 255,
                    (y * 40 + x * 9 + 5) & 255,
                    (x * 17 + y * 31 + 7) & 255,
                    255))
    filter_type = y
    encoded = bytearray([filter_type])
    for i, value in enumerate(raw):
        left = raw[i - 4] if i >= 4 else 0
        up = previous[i]
        upper_left = previous[i - 4] if i >= 4 else 0
        if filter_type == 0:
            predictor = 0
        elif filter_type == 1:
            predictor = left
        elif filter_type == 2:
            predictor = up
        elif filter_type == 3:
            predictor = (left + up) // 2
        else:
            predictor = paeth(left, up, upper_left)
        encoded.append((value - predictor) & 255)
    rows.append(bytes(encoded))
    previous = bytes(raw)
write_png("rgba-filters.png", width, height, 6, rows, level=9)

# Indexed-color palette with one partially transparent and one fully transparent entry.
palette = bytes([255, 0, 0, 0, 255, 0, 0, 0, 255, 20, 40, 60])
alpha = bytes([255, 128, 0, 255])
rows = [bytes([0, 0, 1, 2, 3]), bytes([0, 3, 2, 1, 0]), bytes([0, 1, 1, 2, 2])]
write_png("indexed-alpha.png", 4, 3, 3, rows, palette=palette, transparency=alpha)

# Grayscale with alpha, using Sub on the second row.
rows = [bytes([0, 40, 255, 200, 128]), bytes([1, 100, 64, 155, 192])]
write_png("gray-alpha.png", 2, 2, 4, rows, level=0)

# Repetitive but varied RGB data so zlib emits a dynamic-Huffman DEFLATE block.
width, height = 64, 48
rows = []
for y in range(height):
    row = bytearray([0])
    for x in range(width):
        row.extend((((x * 13 + y * 7) & 15) * 16,
                    ((x * 5 + y * 11 + 3) & 15) * 16,
                    ((x * 9 + y * 3 + 7) & 15) * 16))
    rows.append(bytes(row))
write_png("dynamic-rgb.png", width, height, 2, rows, level=9)
