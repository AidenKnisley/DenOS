/* Minimal freestanding C runtime symbols required by BearSSL. Kept in a
 * separate translation unit and compiled only into TLS-enabled kernels. */
#ifdef DENOS_ENABLE_TLS
#include "types.h"

void *memcpy(void *destination, const void *source, usize count) {
    u8 *d = (u8 *)destination;
    const u8 *s = (const u8 *)source;
    for (usize i = 0; i < count; ++i) d[i] = s[i];
    return destination;
}

void *memmove(void *destination, const void *source, usize count) {
    u8 *d = (u8 *)destination;
    const u8 *s = (const u8 *)source;
    if (d < s) {
        for (usize i = 0; i < count; ++i) d[i] = s[i];
    } else if (d > s) {
        while (count) { --count; d[count] = s[count]; }
    }
    return destination;
}

void *memset(void *destination, int value, usize count) {
    u8 *d = (u8 *)destination;
    for (usize i = 0; i < count; ++i) d[i] = (u8)value;
    return destination;
}

int memcmp(const void *left, const void *right, usize count) {
    const u8 *a = (const u8 *)left;
    const u8 *b = (const u8 *)right;
    for (usize i = 0; i < count; ++i) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    }
    return 0;
}

usize strlen(const char *text) {
    usize n = 0;
    if (!text) return 0;
    while (text[n]) ++n;
    return n;
}

int strcmp(const char *left, const char *right) {
    while (*left && *left == *right) { ++left; ++right; }
    return (int)(u8)*left - (int)(u8)*right;
}

int strncmp(const char *left, const char *right, usize count) {
    for (usize i = 0; i < count; ++i) {
        u8 a = (u8)left[i], b = (u8)right[i];
        if (a != b || !a || !b) return (int)a - (int)b;
    }
    return 0;
}

char *strchr(const char *text, int character) {
    char c = (char)character;
    while (*text && *text != c) ++text;
    return *text == c ? (char *)text : 0;
}

char *strrchr(const char *text, int character) {
    const char *last = 0;
    char c = (char)character;
    do { if (*text == c) last = text; } while (*text++);
    return (char *)last;
}
#endif
