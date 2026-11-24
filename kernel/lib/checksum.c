#include "checksum.h"

u32 kcrc32(const void *data, usize length) {
    const u8 *bytes = (const u8 *)data;
    u32 crc = 0xFFFFFFFFu;
    if (!bytes && length) return 0;
    for (usize i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (u32 bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return crc ^ 0xFFFFFFFFu;
}

u32 kfnv1a32(const void *data, usize length) {
    const u8 *bytes = (const u8 *)data;
    u32 hash = 2166136261u;
    if (!bytes && length) return 0;
    for (usize i = 0; i < length; i++) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}
