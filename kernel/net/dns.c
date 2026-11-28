#include "net.h"
#include "net_internal.h"
#include "kstring.h"
#include "timer.h"
#include "entropy.h"

#define DNS_PORT 53u
#define DNS_CACHE_COUNT 8u
#define DNS_PACKET_MAX 512u
#define DNS_DEFAULT_TTL 60u

typedef struct dns_cache_entry {
    char name[254];
    u8 address[4];
    u64 expires;
    int valid;
} dns_cache_entry_t;
static dns_cache_entry_t cache[DNS_CACHE_COUNT];
static u32 cache_cursor;

static u16 get16(const u8 *p) { return (u16)(((u16)p[0] << 8) | p[1]); }
static u32 get32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static char lower_ascii(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c; }
static void put16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static int name_equal(const char *a, const char *b) {
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z') x = (char)(x + ('a' - 'A'));
        if (y >= 'A' && y <= 'Z') y = (char)(y + ('a' - 'A'));
        if (x != y) return 0;
    }
    return *a == 0 && *b == 0;
}
static int normalize_name(const char *input, char output[254]) {
    usize n;
    if (!input || !output) return -1;
    n = kstrnlen(input, 254u);
    if (!n || n >= 254u) return -1;
    if (input[n - 1u] == '.') --n;
    if (!n || n >= 254u) return -1;
    for (usize i = 0; i < n; ++i) {
        char c = input[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '.')) return -1;
        if (c == '.' && (i == 0 || input[i - 1u] == '.')) return -1;
        if (c == '.' && input[i - 1u] == '-') return -1;
        if (c == '-' && (i == 0 || input[i - 1u] == '.')) return -1;
        output[i] = c;
    }
    if (output[n - 1u] == '.' || output[n - 1u] == '-') return -1;
    output[n] = 0;
    return 0;
}
static int encode_name(const char *name, u8 *packet, u16 capacity, u16 *offset) {
    u16 pos = *offset;
    usize start = 0;
    usize n = kstrlen(name);
    for (usize i = 0; i <= n; ++i) {
        if (name[i] == '.' || name[i] == 0) {
            usize label = i - start;
            if (!label || label > 63u || pos + label + 1u >= capacity) return -1;
            packet[pos++] = (u8)label;
            for (usize j = 0; j < label; ++j) packet[pos++] = (u8)name[start + j];
            start = i + 1u;
        }
    }
    if (pos >= capacity) return -1;
    packet[pos++] = 0;
    *offset = pos;
    return 0;
}
static int skip_name(const u8 *packet, u16 length, u16 *offset) {
    u16 pos = *offset;
    u16 resume = 0;
    int jumped = 0;
    u32 labels = 0;
    while (pos < length && labels++ < 128u) {
        u8 c = packet[pos++];
        if (!c) {
            *offset = jumped ? resume : pos;
            return 0;
        }
        if ((c & 0xC0u) == 0xC0u) {
            u16 pointer;
            if (pos >= length) return -1;
            pointer = (u16)(((u16)(c & 0x3Fu) << 8) | packet[pos++]);
            if (pointer >= length) return -1;
            if (!jumped) { resume = pos; jumped = 1; }
            pos = pointer;
            continue;
        }
        if (c & 0xC0u || c > 63u || pos + c > length) return -1;
        pos = (u16)(pos + c);
    }
    return -1;
}
static int record_name_matches(const u8 *packet, u16 length, u16 offset, const char *name) {
    u16 pos = offset;
    usize name_pos = 0, name_length = kstrlen(name);
    int equal = 1;
    for (u32 steps = 0; pos < length && steps < 128u; ++steps) {
        u8 label = packet[pos++];
        if (!label) return equal && name_pos == name_length;
        if ((label & 0xC0u) == 0xC0u) {
            u16 pointer;
            if (pos >= length) return 0;
            pointer = (u16)(((u16)(label & 0x3Fu) << 8) | packet[pos++]);
            if (pointer >= length) return 0;
            pos = pointer;
            continue;
        }
        if ((label & 0xC0u) || label > 63u || pos + label > length) return 0;
        if (name_pos) {
            if (name_pos >= name_length || name[name_pos] != '.') equal = 0;
            else ++name_pos;
        }
        for (u32 i = 0; i < label; ++i) {
            if (name_pos >= name_length || lower_ascii((char)packet[pos + i]) != lower_ascii(name[name_pos]))
                equal = 0;
            if (name_pos < name_length) ++name_pos;
        }
        if (name_pos < name_length && name[name_pos] != '.') equal = 0;
        pos = (u16)(pos + label);
    }
    return 0;
}


static int decode_name(const u8 *packet, u16 length, u16 *offset,
                       char *output, usize capacity) {
    u16 pos, resume = 0;
    usize used = 0;
    int jumped = 0;
    if (!packet || !offset || !output || capacity < 2u || *offset >= length) return -1;
    pos = *offset;
    for (u32 steps = 0; steps < 128u && pos < length; ++steps) {
        u8 label = packet[pos++];
        if (!label) {
            *offset = jumped ? resume : pos;
            output[used] = 0;
            return 0;
        }
        if ((label & 0xC0u) == 0xC0u) {
            u16 pointer;
            if (pos >= length) return -1;
            pointer = (u16)(((u16)(label & 0x3Fu) << 8) | packet[pos++]);
            if (pointer >= length) return -1;
            if (!jumped) { resume = pos; jumped = 1; }
            pos = pointer;
            continue;
        }
        if ((label & 0xC0u) || label > 63u || (u32)pos + label > length) return -1;
        if (used) {
            if (used + 1u >= capacity) return -1;
            output[used++] = '.';
        }
        if (used + label >= capacity) return -1;
        for (u32 i = 0; i < label; ++i) {
            char c = lower_ascii((char)packet[pos + i]);
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return -1;
            output[used++] = c;
        }
        pos = (u16)(pos + label);
    }
    return -1;
}

static int question_matches(const u8 *packet, u16 length, const char *name) {
    u16 pos = 12;
    usize name_pos = 0;
    usize name_length = kstrlen(name);
    if (length < 17u || get16(packet + 4) != 1u) return 0;
    while (pos < length) {
        u8 label = packet[pos++];
        if (!label) break;
        if ((label & 0xC0u) || label > 63u || pos + label > length) return 0;
        if (name_pos) {
            if (name_pos >= name_length || name[name_pos] != '.') return 0;
            ++name_pos;
        }
        for (u32 i = 0; i < label; ++i) {
            char left = (char)packet[pos++];
            if (name_pos >= name_length || lower_ascii(left) != lower_ascii(name[name_pos])) return 0;
            ++name_pos;
        }
        if (name_pos < name_length && name[name_pos] != '.') return 0;
    }
    if (pos > length || name_pos != name_length || pos + 4u > length) return 0;
    return get16(packet + pos) == 1u && get16(packet + pos + 2) == 1u;
}

static int parse_response(const u8 *packet, u16 length, u16 id, const char *name,
                          u8 address[4], u32 *ttl) {
    u16 questions, answers, answers_start, pos = 12;
    char current_name[254];
    u32 chain_ttl = 0xFFFFFFFFu;
    if (!packet || !address || !ttl || length < 12u || get16(packet) != id) return -1;
    u16 flags = get16(packet + 2);
    if (!(flags & 0x8000u) || (flags & 0x7800u) || (flags & 0x0200u) || (flags & 0x000Fu) ||
        !question_matches(packet, length, name)) return -1;
    questions = get16(packet + 4);
    answers = get16(packet + 6);
    if (questions != 1u || answers > 64u || kstrlcpy(current_name, name, sizeof(current_name)) >= sizeof(current_name)) return -1;
    for (u16 i = 0; i < questions; ++i) {
        if (skip_name(packet, length, &pos) < 0 || pos + 4u > length) return -1;
        pos = (u16)(pos + 4u);
    }
    answers_start = pos;

    /* Follow a bounded CNAME chain. DNS answers often return an alias before
     * the final A record; treating only an A whose owner equals the question
     * as a hit breaks lookups for many real websites. Restart the answer scan
     * after each alias so records may appear in any order. */
    for (u32 depth = 0; depth <= 8u; ++depth) {
        int followed_alias = 0;
        pos = answers_start;
        for (u16 i = 0; i < answers; ++i) {
            u16 type, klass, data_len;
            u32 record_ttl;
            int owner_matches = record_name_matches(packet, length, pos, current_name);
            if (skip_name(packet, length, &pos) < 0 || pos + 10u > length) return -1;
            type = get16(packet + pos); klass = get16(packet + pos + 2);
            record_ttl = get32(packet + pos + 4); data_len = get16(packet + pos + 8);
            pos = (u16)(pos + 10u);
            if ((u32)pos + data_len > length) return -1;
            if (owner_matches && klass == 1u && type == 1u && data_len == 4u) {
                kmemcpy(address, packet + pos, 4);
                *ttl = chain_ttl < record_ttl ? chain_ttl : record_ttl;
                return 0;
            }
            if (owner_matches && klass == 1u && type == 5u) {
                char alias[254];
                u16 alias_end = pos;
                if (decode_name(packet, length, &alias_end, alias, sizeof(alias)) < 0 ||
                    alias_end != (u16)(pos + data_len) || name_equal(alias, current_name)) return -1;
                if (record_ttl < chain_ttl) chain_ttl = record_ttl;
                kstrlcpy(current_name, alias, sizeof(current_name));
                followed_alias = 1;
                break;
            }
            pos = (u16)(pos + data_len);
        }
        if (!followed_alias) return -1;
    }
    return -1;
}

void net_dns_reset(void) {
    kmemset(cache, 0, sizeof(cache));
    cache_cursor = 0;
}

int net_dns_resolve(const char *hostname, u8 address[4], u32 timeout_ticks) {
    char name[254];
    u8 packet[DNS_PACKET_MAX], response[DNS_PACKET_MAX], server[4];
    u16 offset, id, port, received, source_port;
    u8 source[4], answer[4];
    u32 ttl = DNS_DEFAULT_TTL;
    u64 start;
    u32 attempts;
    if (!address || normalize_name(hostname, name) < 0 || !net_is_ready()) return -1;
    /* Numeric addresses are useful for scripts and avoid unnecessary DNS. */
    {
        u32 parts[4] = {0, 0, 0, 0};
        u32 part = 0, count = 0, digits = 0;
        int valid = 1;
        for (usize i = 0; name[i]; ++i) {
            if (name[i] == '.') {
                if (!digits || count >= 3u) { valid = 0; break; }
                parts[count++] = part; part = 0; digits = 0;
            } else if (name[i] >= '0' && name[i] <= '9') {
                part = part * 10u + (u32)(name[i] - '0'); ++digits;
                if (part > 255u || digits > 3u) { valid = 0; break; }
            } else { valid = 0; break; }
        }
        if (valid && count == 3u && digits) {
            parts[3] = part;
            for (u32 i = 0; i < 4; ++i) address[i] = (u8)parts[i];
            return 0;
        }
    }
    for (u32 i = 0; i < DNS_CACHE_COUNT; ++i) {
        if (cache[i].valid && timer_ticks() >= cache[i].expires) cache[i].valid = 0;
        if (cache[i].valid && name_equal(cache[i].name, name)) {
            kmemcpy(address, cache[i].address, 4);
            return 0;
        }
    }
    net_get_ipv4_config(0, 0, 0, server);
    if (server[0] == 0 && server[1] == 0 && server[2] == 0 && server[3] == 0) return -2;
    id = (u16)entropy_u32();
    port = (u16)(49152u + (entropy_u32() & 0x3FFFu));
    kmemset(packet, 0, sizeof(packet));
    put16(packet, id);
    put16(packet + 2, 0x0100u); /* recursion desired */
    put16(packet + 4, 1);
    offset = 12;
    if (encode_name(name, packet, sizeof(packet), &offset) < 0 || offset + 4u > sizeof(packet)) return -1;
    put16(packet + offset, 1); put16(packet + offset + 2, 1); offset = (u16)(offset + 4u);
    attempts = timeout_ticks > 40u ? 2u : 1u;
    for (u32 attempt = 0; attempt < attempts; ++attempt) {
        u32 slice = timeout_ticks / attempts;
        if (!slice) slice = 1;
        if (net_udp_send(server, port, DNS_PORT, packet, offset) < 0) return -3;
        start = timer_ticks();
        while (timer_ticks() - start < slice) {
            int got;
            net_poll();
            got = net_udp_receive(port, source, &source_port, response, sizeof(response), &received);
            if (got > 0 && source_port == DNS_PORT && kmemcmp(source, server, 4) == 0 &&
                parse_response(response, received, id, name, answer, &ttl) == 0) {
                kmemcpy(address, answer, 4);
                if (ttl) {
                    dns_cache_entry_t *slot = &cache[cache_cursor++ % DNS_CACHE_COUNT];
                    kstrlcpy(slot->name, name, sizeof(slot->name));
                    kmemcpy(slot->address, answer, 4);
                    if (ttl > 86400u) ttl = 86400u;
                    slot->expires = timer_ticks() + (u64)ttl * 100u;
                    slot->valid = 1;
                }
                return 0;
            }
        }
    }
    return -4;
}
