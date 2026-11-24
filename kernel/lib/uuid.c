#include "uuid.h"
#include "entropy.h"
int uuid_generate_v4(char output[37]) {
    static const char hex[] = "0123456789abcdef";
    u8 bytes[16];
    u64 a, b;
    usize j = 0;
    if (!output) return -1;
    a = entropy_next(); b = entropy_next();
    for (int i = 0; i < 8; ++i) { bytes[i] = (u8)(a >> (i * 8)); bytes[i + 8] = (u8)(b >> (i * 8)); }
    bytes[6] = (u8)((bytes[6] & 0x0Fu) | 0x40u);
    bytes[8] = (u8)((bytes[8] & 0x3Fu) | 0x80u);
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) output[j++] = '-';
        output[j++] = hex[bytes[i] >> 4];
        output[j++] = hex[bytes[i] & 15u];
    }
    output[j] = '\0';
    return 0;
}
