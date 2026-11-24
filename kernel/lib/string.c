#include "kstring.h"

usize kstrlen(const char *text) {
    usize length = 0;
    if (!text) return 0;
    while (text[length]) length++;
    return length;
}

usize kstrnlen(const char *text, usize limit) {
    usize length = 0;
    if (!text) return 0;
    while (length < limit && text[length]) length++;
    return length;
}

int kstrcmp(const char *left, const char *right) {
    if (!left || !right) return left == right ? 0 : (left ? 1 : -1);
    while (*left && *left == *right) {
        left++;
        right++;
    }
    return (int)(unsigned char)*left - (int)(unsigned char)*right;
}

int kstrncmp(const char *left, const char *right, usize limit) {
    if (!limit) return 0;
    if (!left || !right) return left == right ? 0 : (left ? 1 : -1);
    while (limit && *left && *left == *right) {
        left++;
        right++;
        limit--;
    }
    if (!limit) return 0;
    return (int)(unsigned char)*left - (int)(unsigned char)*right;
}

usize kstrlcpy(char *destination, const char *source, usize capacity) {
    usize source_length = 0;
    usize copied = 0;
    if (!source) source = "";
    while (source[source_length]) source_length++;
    if (destination && capacity) {
        while (copied + 1 < capacity && source[copied]) {
            destination[copied] = source[copied];
            copied++;
        }
        destination[copied] = '\0';
    }
    return source_length;
}

void *kmemset(void *destination, int value, usize count) {
    unsigned char *bytes = (unsigned char *)destination;
    for (usize i = 0; i < count; i++) bytes[i] = (unsigned char)value;
    return destination;
}

void *kmemcpy(void *destination, const void *source, usize count) {
    unsigned char *out = (unsigned char *)destination;
    const unsigned char *in = (const unsigned char *)source;
    for (usize i = 0; i < count; i++) out[i] = in[i];
    return destination;
}

void *kmemmove(void *destination, const void *source, usize count) {
    unsigned char *out = (unsigned char *)destination;
    const unsigned char *in = (const unsigned char *)source;
    if (out == in || !count) return destination;
    if ((usize)out < (usize)in) {
        for (usize i = 0; i < count; i++) out[i] = in[i];
    } else {
        while (count) {
            count--;
            out[count] = in[count];
        }
    }
    return destination;
}

int kmemcmp(const void *left, const void *right, usize count) {
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    for (usize i = 0; i < count; i++) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    }
    return 0;
}

char *kstrchr(const char *text, int character) {
    unsigned char wanted = (unsigned char)character;
    if (!text) return 0;
    for (;;) {
        if ((unsigned char)*text == wanted) return (char *)text;
        if (!*text) return 0;
        text++;
    }
}

char *kstrrchr(const char *text, int character) {
    const char *last = 0;
    unsigned char wanted = (unsigned char)character;
    if (!text) return 0;
    do {
        if ((unsigned char)*text == wanted) last = text;
    } while (*text++);
    return (char *)last;
}

char *kstrstr(const char *text, const char *pattern) {
    if (!text || !pattern) return 0;
    if (!*pattern) return (char *)text;
    for (; *text; text++) {
        usize i = 0;
        while (pattern[i] && text[i] && pattern[i] == text[i]) i++;
        if (!pattern[i]) return (char *)text;
    }
    return 0;
}
