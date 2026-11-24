#include "bootargs.h"
#include "kstring.h"

static char arguments[BOOTARGS_CAP];

void bootargs_init(const void *info, u32 total_size) {
    const u8 *bytes = (const u8 *)info;
    u32 offset = 8u;
    arguments[0] = '\0';
    if (!bytes || total_size < 16u) return;
    while (offset <= total_size - 8u) {
        u32 type = *(const u32 *)(bytes + offset);
        u32 size = *(const u32 *)(bytes + offset + 4u);
        if (size < 8u || size > total_size - offset) return;
        if (type == 0u) return;
        if (type == 1u && size > 8u) {
            usize length = (usize)(size - 8u);
            if (length >= sizeof(arguments)) length = sizeof(arguments) - 1u;
            for (usize i = 0; i < length; ++i) {
                char c = (char)bytes[offset + 8u + i];
                if (!c) break;
                arguments[i] = c;
                arguments[i + 1u] = '\0';
            }
            return;
        }
        u32 advance = (size + 7u) & ~7u;
        if (advance < size || advance > total_size - offset) return;
        offset += advance;
    }
}

const char *bootargs_get(void) { return arguments; }

static int token_matches(const char *token, usize length, const char *option) {
    usize n = kstrnlen(option, BOOTARGS_CAP);
    return n == length && kmemcmp(token, option, n) == 0;
}

int bootargs_has(const char *option) {
    usize i = 0;
    if (!option || !option[0]) return 0;
    while (arguments[i]) {
        while (arguments[i] == ' ' || arguments[i] == '\t') ++i;
        usize start = i;
        while (arguments[i] && arguments[i] != ' ' && arguments[i] != '\t') ++i;
        if (i > start && token_matches(arguments + start, i - start, option)) return 1;
    }
    return 0;
}

int bootargs_value(const char *key, char *out, usize capacity) {
    usize i = 0;
    if (!key || !key[0] || !out || !capacity) return -1;
    out[0] = '\0';
    usize key_length = kstrnlen(key, BOOTARGS_CAP);
    while (arguments[i]) {
        while (arguments[i] == ' ' || arguments[i] == '\t') ++i;
        usize start = i;
        while (arguments[i] && arguments[i] != ' ' && arguments[i] != '\t') ++i;
        usize end = i;
        if (end - start > key_length && arguments[start + key_length] == '=' &&
            kmemcmp(arguments + start, key, key_length) == 0) {
            usize length = end - start - key_length - 1u;
            if (length >= capacity) return -1;
            kmemcpy(out, arguments + start + key_length + 1u, length);
            out[length] = '\0';
            return 0;
        }
    }
    return -1;
}
