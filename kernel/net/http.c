#include "net.h"
#include "net_internal.h"
#include "serial.h"
#include "kstring.h"
#include "kformat.h"
#include "parse.h"
#include "gfx.h"
#include "qoi.h"
#include "png.h"

#define HTTP_RESPONSE_CAP 65535u
#define HTTP_BODY_CAP 60000u
#define HTTP_TEXT_CAP 56000u
#define HTTP_HOST_CAP 128u
#define HTTP_PATH_CAP 768u
#define HTTP_IDLE_TIMEOUT 600u
#define WEB_VIEW_WIDTH 78u
#define WEB_VIEW_ROWS 18u
#define WEB_VIEW_STEP (WEB_VIEW_ROWS - 1u)
#define WEB_LINK_CAP 32u
#define WEB_IMAGE_CAP 16u
#define WEB_HISTORY_CAP 8u
#define WEB_IMAGE_PIXELS (640u * 480u)

static u8 response_buffer[HTTP_RESPONSE_CAP];
static u8 body_buffer[HTTP_BODY_CAP];
static char text_buffer[HTTP_TEXT_CAP];
static char request_buffer[HTTP_PATH_CAP + HTTP_HOST_CAP + 256u];
#ifndef DENOS_HTTP_TEST_MODE
typedef struct web_link_entry { char url[HTTP_PATH_CAP + HTTP_HOST_CAP + 32u]; } web_link_entry_t;
typedef struct web_image_entry {
    char url[HTTP_PATH_CAP + HTTP_HOST_CAP + 32u];
    char alt[80];
} web_image_entry_t;
static web_link_entry_t web_links[WEB_LINK_CAP];
static web_image_entry_t web_images[WEB_IMAGE_CAP];
static u16 web_link_count;
static u16 web_image_count;
static int web_selected_link = -1;
static int web_back_requested;
static int web_forward_requested;
static int web_go_requested;
static int web_exit_requested;
static char web_go_url[HTTP_PATH_CAP + HTTP_HOST_CAP + 32u];
static char web_history[WEB_HISTORY_CAP][HTTP_PATH_CAP + HTTP_HOST_CAP + 32u];
static u32 web_image_pixels[WEB_IMAGE_PIXELS];
static u8 web_png_workspace[HTTP_BODY_CAP + WEB_IMAGE_PIXELS * 4u + 1024u];
#endif
static char web_page_url[HTTP_PATH_CAP + HTTP_HOST_CAP + 32u];
static char web_hidden_selectors[24][48];
static u8 web_hidden_selector_count;
static u8 web_script_html[4096];
static char web_script_text[4096];
#ifndef DENOS_HTTP_TEST_MODE
static u16 web_line_starts[HTTP_TEXT_CAP];
static u8 web_line_lengths[HTTP_TEXT_CAP];
#endif

typedef struct http_url {
    char host[HTTP_HOST_CAP];
    char path[HTTP_PATH_CAP];
    u16 port;
    int secure;
} http_url_t;

static char lower_ascii(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c; }
static int starts_ci(const char *text, const char *prefix) {
    while (*prefix) {
        if (!*text || lower_ascii(*text++) != lower_ascii(*prefix++)) return 0;
    }
    return 1;
}
static int find_bytes(const u8 *data, u16 length, const char *needle, u16 needle_length) {
    if (!needle_length || needle_length > length) return -1;
    for (u16 i = 0; i <= (u16)(length - needle_length); ++i)
        if (kmemcmp(data + i, needle, needle_length) == 0) return (int)i;
    return -1;
}
static int parse_url(const char *input, http_url_t *url) {
    const char *p = input;
    usize host_len = 0, path_len = 0;
    if (!input || !url || !*input) return -1;
    url->secure = 0;
    if (starts_ci(p, "https://")) { url->secure = 1; p += 8; }
    else if (starts_ci(p, "http://")) p += 7;
    else if (kstrstr(p, "://")) return -3;
    url->port = (u16)(url->secure ? 443u : 80u);
    while (*p && *p != '/' && *p != '?' && *p != '#' && *p != ':') {
        if (host_len + 1u >= sizeof(url->host) || (u8)*p <= 32u || (u8)*p == 127u) return -1;
        url->host[host_len++] = *p++;
    }
    if (!host_len) return -1;
    url->host[host_len] = 0;
    if (*p == ':') {
        char port_text[6];
        u32 port;
        usize digits = 0;
        ++p;
        while (*p >= '0' && *p <= '9') {
            if (digits + 1u >= sizeof(port_text)) return -1;
            port_text[digits++] = *p++;
        }
        port_text[digits] = 0;
        if (!digits || parse_u32(port_text, &port) < 0 || port == 0 || port > 65535u) return -1;
        url->port = (u16)port;
    }
    if (*p && *p != '/' && *p != '?' && *p != '#') return -1;
    if (*p == '?') {
        url->path[path_len++] = '/';
        while (*p && *p != '#') {
            if ((u8)*p <= 32u || (u8)*p == 127u || path_len + 1u >= sizeof(url->path)) return -1;
            url->path[path_len++] = *p++;
        }
    } else if (*p == '/') {
        while (*p && *p != '#') {
            if ((u8)*p <= 32u || (u8)*p == 127u || path_len + 1u >= sizeof(url->path)) return -1;
            url->path[path_len++] = *p++;
        }
    }
    if (!path_len) url->path[path_len++] = '/';
    url->path[path_len] = 0;
    return 0;
}
static int copy_bounded(char *out, usize cap, const char *value) {
    usize n;
    if (!out || !cap || !value) return -1;
    n = kstrnlen(value, cap);
    if (n >= cap) { out[0] = 0; return -1; }
    kmemcpy(out, value, n + 1u);
    return 0;
}
static int build_origin(const http_url_t *url, char *out, usize cap) {
    int n;
    if (!url || !out || !cap) return -1;
    if ((!url->secure && url->port == 80u) || (url->secure && url->port == 443u))
        n = ksnprintf(out, cap, "%s://%s", url->secure ? "https" : "http", url->host);
    else
        n = ksnprintf(out, cap, "%s://%s:%u", url->secure ? "https" : "http",
                      url->host, (unsigned)url->port);
    return n < 0 || (usize)n >= cap ? -1 : 0;
}
static int web_normalize_path(char *path, usize cap) {
    u16 segment_starts[HTTP_PATH_CAP / 2u];
    usize segment_count = 0, read = 1, write = 0, path_end, query_start;
    usize original_length;
    int had_trailing_slash;
    if (!path || cap < 2u || path[0] != '/') return -1;
    original_length = kstrnlen(path, cap);
    if (original_length >= cap) return -1;
    query_start = original_length;
    for (usize i = 0; i < original_length; ++i) {
        if (path[i] == '?' || path[i] == '#') { query_start = i; break; }
    }
    path_end = query_start;
    had_trailing_slash = path_end > 1u && path[path_end - 1u] == '/';
    path[write++] = '/';
    while (read <= path_end) {
        usize start = read;
        while (read < path_end && path[read] != '/') ++read;
        usize length = read - start;
        if (length == 1u && path[start] == '.') {
            /* Current-directory segment: ignore. */
        } else if (length == 2u && path[start] == '.' && path[start + 1u] == '.') {
            if (segment_count) {
                write = segment_starts[--segment_count];
                if (write == 0) path[write++] = '/';
            }
        } else if (length) {
            usize previous = write;
            if (write > 1u && path[write - 1u] != '/') {
                if (write + 1u >= cap) return -1;
                path[write++] = '/';
            }
            if (write + length >= cap || segment_count >= (HTTP_PATH_CAP / 2u)) return -1;
            segment_starts[segment_count++] = (u16)previous;
            /* write never moves beyond read while removing dot segments. */
            for (usize i = 0; i < length; ++i) path[write + i] = path[start + i];
            write += length;
        }
        if (read == path_end) break;
        ++read;
    }
    if (had_trailing_slash && path[write - 1u] != '/') {
        if (write + 1u >= cap) return -1;
        path[write++] = '/';
    }
    /* Copy the saved query/fragment suffix left after normalizing the path.
       The destination never lies after the source, so a forward copy is safe. */
    for (usize i = query_start; i < original_length; ++i) {
        if (write >= cap - 1u) return -1;
        path[write++] = path[i];
    }
    path[write] = 0;
    return 0;
}

static int web_resolve_url(const char *href, char *out, usize cap) {
    http_url_t base;
    char origin[HTTP_HOST_CAP + 32u];
    char target[HTTP_PATH_CAP];
    usize n, path_len;
    int result;
    if (!href || !*href || !out || !cap) return -1;
    if (starts_ci(href, "http://") || starts_ci(href, "https://"))
        return copy_bounded(out, cap, href);
    if (href[0] == '#' || starts_ci(href, "javascript:") || starts_ci(href, "data:") ||
        starts_ci(href, "mailto:") || starts_ci(href, "tel:")) return -1;
    if (parse_url(web_page_url, &base) < 0 || build_origin(&base, origin, sizeof(origin)) < 0) return -1;
    if (href[0] == '/' && href[1] == '/') {
        const char *scheme = base.secure ? "https:" : "http:";
        result = ksnprintf(out, cap, "%s%s", scheme, href);
        return result < 0 || (usize)result >= cap ? -1 : 0;
    }
    if (href[0] == '/') {
        result = ksnprintf(target, sizeof(target), "%s", href);
        if (result < 0 || (usize)result >= sizeof(target)) return -1;
    } else if (href[0] == '?') {
        usize base_path_length = kstrnlen(base.path, sizeof(base.path));
        for (usize i = 0; i < base_path_length; ++i) {
            if (base.path[i] == '?') { base_path_length = i; break; }
        }
        n = kstrnlen(href, sizeof(target));
        if (n >= sizeof(target) || base_path_length + n >= sizeof(target)) return -1;
        kmemcpy(target, base.path, base_path_length);
        kmemcpy(target + base_path_length, href, n + 1u);
    } else {
        const char *last = base.path;
        usize base_path_length = kstrnlen(base.path, sizeof(base.path));
        for (usize i = 0; i < base_path_length; ++i) {
            if (base.path[i] == '?') { base_path_length = i; break; }
        }
        for (usize i = 0; i < base_path_length; ++i)
            if (base.path[i] == '/') last = base.path + i;
        path_len = (usize)(last - base.path + 1);
        n = kstrnlen(href, sizeof(target));
        if (n >= sizeof(target) || path_len + n >= sizeof(target)) return -1;
        kmemcpy(target, base.path, path_len);
        kmemcpy(target + path_len, href, n + 1u);
    }
    for (n = 0; target[n]; ++n) if (target[n] == '#') { target[n] = 0; break; }
    if (!target[0] || web_normalize_path(target, sizeof(target)) < 0) return -1;
    result = ksnprintf(out, cap, "%s%s", origin, target);
    return result < 0 || (usize)result >= cap ? -1 : 0;
}
#ifdef DENOS_HTTP_TEST_MODE
int net_http_test_resolve_url(const char *base, const char *href, char *out, usize capacity) {
    if (copy_bounded(web_page_url, sizeof(web_page_url), base) < 0) return -1;
    return web_resolve_url(href, out, capacity);
}
#endif
static int entity_value(const char *entity, u16 length, char out[4]);

static int tag_attribute(const u8 *tag, u16 length, const char *wanted,
                         char *out, usize capacity) {
    u16 i = 0;
    usize wanted_len;
    if (!tag || !wanted || !out || !capacity) return 0;
    wanted_len = kstrlen(wanted);
    out[0] = 0;
    while (i < length && tag[i] != ' ' && tag[i] != '\t' &&
           tag[i] != '\r' && tag[i] != '\n' && tag[i] != '/') ++i;
    while (i < length) {
        u16 name_start, name_end, value_start, value_end;
        int matches;
        while (i < length && (tag[i] == ' ' || tag[i] == '\t' || tag[i] == '/' || tag[i] == '\r' || tag[i] == '\n')) ++i;
        if (i >= length || tag[i] == '>') break;
        name_start = i;
        while (i < length && tag[i] != '=' && tag[i] != ' ' && tag[i] != '\t' && tag[i] != '\r' && tag[i] != '\n' && tag[i] != '/' && tag[i] != '>') ++i;
        name_end = i;
        matches = (usize)(name_end - name_start) == wanted_len &&
                  starts_ci((const char *)tag + name_start, wanted);
        while (i < length && (tag[i] == ' ' || tag[i] == '\t' || tag[i] == '\r' || tag[i] == '\n')) ++i;
        if (i >= length || tag[i] != '=') {
            if (matches) return 1; /* Boolean attribute, e.g. hidden or disabled. */
            continue;
        }
        ++i;
        while (i < length && (tag[i] == ' ' || tag[i] == '\t' || tag[i] == '\r' || tag[i] == '\n')) ++i;
        if (i >= length) break;
        if (tag[i] == '\'' || tag[i] == '"') {
            u8 quote = tag[i++];
            value_start = i;
            while (i < length && tag[i] != quote) ++i;
            value_end = i;
            if (i < length) ++i;
        } else {
            value_start = i;
            while (i < length && tag[i] != ' ' && tag[i] != '\t' && tag[i] != '\r' && tag[i] != '\n' && tag[i] != '>') ++i;
            value_end = i;
        }
        if (matches) {
            usize source = value_start;
            usize written = 0;
            while (source < value_end) {
                if (tag[source] == '&') {
                    u16 entity_end = (u16)(source + 1u);
                    char decoded[4];
                    int decoded_length = 0;
                    while (entity_end < value_end && tag[entity_end] != ';' &&
                           (usize)(entity_end - source) <= 12u) ++entity_end;
                    if (entity_end < value_end && tag[entity_end] == ';')
                        decoded_length = entity_value((const char *)tag + source + 1u,
                            (u16)(entity_end - source - 1u), decoded);
                    if (decoded_length > 0) {
                        for (int d = 0; d < decoded_length; ++d) {
                            u8 c = (u8)decoded[d];
                            /* Never let encoded NUL/control bytes terminate or poison a URL. */
                            if (c < 0x20u || c == 0x7Fu) return 0;
                        }
                        if (written + (usize)decoded_length >= capacity) return 0;
                        for (int d = 0; d < decoded_length; ++d) out[written++] = decoded[d];
                        source = (usize)entity_end + 1u;
                        continue;
                    }
                }
                if (written + 1u >= capacity) return 0;
                out[written++] = (char)tag[source++];
            }
            out[written] = 0;
            return 1;
        }
    }
    return 0;
}
#ifdef DENOS_HTTP_TEST_MODE
int net_http_test_attribute(const char *tag, const char *name, char *out, usize capacity) {
    usize length = tag ? kstrnlen(tag, 0xFFFFu) : 0;
    if (!tag || length >= 0xFFFFu) return 0;
    return tag_attribute((const u8 *)tag, (u16)length, name, out, capacity);
}
#endif

#ifndef DENOS_HTTP_TEST_MODE
static int web_add_link(const char *href) {
    char resolved[HTTP_PATH_CAP + HTTP_HOST_CAP + 32u];
    if (web_link_count >= WEB_LINK_CAP || web_resolve_url(href, resolved, sizeof(resolved)) < 0) return -1;
    if (parse_url(resolved, &(http_url_t){0}) < 0) return -1;
    copy_bounded(web_links[web_link_count].url, sizeof(web_links[web_link_count].url), resolved);
    return (int)web_link_count++;
}
static int web_add_image(const char *src, const char *alt) {
    char resolved[HTTP_PATH_CAP + HTTP_HOST_CAP + 32u];
    u16 index;
    if (web_image_count >= WEB_IMAGE_CAP || web_resolve_url(src, resolved, sizeof(resolved)) < 0) return -1;
    if (parse_url(resolved, &(http_url_t){0}) < 0) return -1;
    index = web_image_count++;
    copy_bounded(web_images[index].url, sizeof(web_images[index].url), resolved);
    if (alt) copy_bounded(web_images[index].alt, sizeof(web_images[index].alt), alt);
    else web_images[index].alt[0] = 0;
    return (int)index;
}
#endif
static int header_token_is(const u8 *value, u16 length, const char *token) {
    u16 token_length = (u16)kstrlen(token);
    if (!value || length < token_length || !starts_ci((const char *)value, token)) return 0;
    while (token_length < length && (value[token_length] == ' ' || value[token_length] == '\t')) ++token_length;
    return token_length == length;
}
static u32 header_count(const u8 *headers, u16 length, const char *name) {
    u16 name_length = (u16)kstrlen(name);
    u32 count = 0;
    if (!headers || !name_length) return 0;
    for (u16 i = 0; i + name_length < length; ++i) {
        if ((i == 0 || headers[i - 1u] == '\n') &&
            starts_ci((const char *)headers + i, name) &&
            headers[i + name_length] == ':')
            ++count;
    }
    return count;
}

static const u8 *header_value(const u8 *headers, u16 length, const char *name, u16 *value_length) {
    u16 name_length = (u16)kstrlen(name);
    for (u16 i = 0; i + name_length + 1u < length; ++i) {
        if ((i == 0 || (headers[i - 1u] == '\n')) && starts_ci((const char *)headers + i, name) && headers[i + name_length] == ':') {
            u16 start = (u16)(i + name_length + 1u);
            u16 end;
            while (start < length && (headers[start] == ' ' || headers[start] == '\t')) ++start;
            end = start;
            while (end < length && headers[end] != '\r' && headers[end] != '\n') ++end;
            *value_length = (u16)(end - start);
            return headers + start;
        }
    }
    *value_length = 0;
    return 0;
}
static int parse_decimal_header(const u8 *value, u16 length, u32 *number) {
    char text[11];
    if (!value || !length || length >= sizeof(text) || !number) return -1;
    for (u16 i = 0; i < length; ++i) {
        if (value[i] < '0' || value[i] > '9') return -1;
        text[i] = (char)value[i];
    }
    text[length] = 0;
    return parse_u32(text, number);
}

static int decode_chunked(const u8 *input, u16 length, u8 *output, u16 capacity, u16 *output_length) {
    u16 in = 0, out = 0;
    while (in < length) {
        u32 chunk = 0;
        int digits = 0;
        while (in < length && input[in] != '\r' && input[in] != '\n' && input[in] != ';') {
            u8 c = input[in++];
            u32 value;
            if (c >= '0' && c <= '9') value = c - '0';
            else if (c >= 'a' && c <= 'f') value = c - 'a' + 10u;
            else if (c >= 'A' && c <= 'F') value = c - 'A' + 10u;
            else return -1;
            if (chunk > (0xFFFFFFFFu - value) / 16u) return -1;
            chunk = chunk * 16u + value;
            ++digits;
        }
        if (!digits) return -1;
        if (in < length && input[in] == ';') while (in < length && input[in] != '\r') ++in;
        if (in + 1u >= length || input[in] != '\r' || input[in + 1u] != '\n') return -1;
        in += 2;
        if (chunk == 0) { *output_length = out; return 0; }
        if (chunk > (u32)(capacity - out) || chunk > (u32)(length - in)) return -1;
        kmemcpy(output + out, input + in, chunk);
        out = (u16)(out + chunk);
        in = (u16)(in + chunk);
        if (in + 1u >= length || input[in] != '\r' || input[in + 1u] != '\n') return -1;
        in += 2;
    }
    return -1;
}
static int entity_value(const char *entity, u16 length, char out[4]) {
    u32 value = 0;
    if (length == 3 && kmemcmp(entity, "amp", 3) == 0) value = '&';
    else if (length == 2 && kmemcmp(entity, "lt", 2) == 0) value = '<';
    else if (length == 2 && kmemcmp(entity, "gt", 2) == 0) value = '>';
    else if (length == 4 && kmemcmp(entity, "quot", 4) == 0) value = '"';
    else if (length == 4 && kmemcmp(entity, "apos", 4) == 0) value = '\'';
    else if (length == 4 && kmemcmp(entity, "nbsp", 4) == 0) value = ' ';
    else if (length > 1 && entity[0] == '#') {
        u16 i = 1;
        u32 base = 10;
        if (i < length && (entity[i] == 'x' || entity[i] == 'X')) { base = 16; ++i; }
        if (i == length) return 0;
        for (; i < length; ++i) {
            u32 d;
            char c = entity[i];
            if (c >= '0' && c <= '9') d = (u32)(c - '0');
            else if (base == 16 && c >= 'a' && c <= 'f') d = (u32)(c - 'a' + 10);
            else if (base == 16 && c >= 'A' && c <= 'F') d = (u32)(c - 'A' + 10);
            else return 0;
            if (value > (0x10FFFFu - d) / base) return 0;
            value = value * base + d;
        }
    } else return 0;
    if (value < 0x80u) { out[0] = (char)value; return 1; }
    if (value < 0x800u) {
        out[0] = (char)(0xC0u | (value >> 6)); out[1] = (char)(0x80u | (value & 0x3Fu)); return 2;
    }
    if (value < 0x10000u && !(value >= 0xD800u && value <= 0xDFFFu)) {
        out[0] = (char)(0xE0u | (value >> 12)); out[1] = (char)(0x80u | ((value >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (value & 0x3Fu)); return 3;
    }
    return 0;
}
static int tag_name_is(const u8 *tag, u16 length, const char *name) {
    u16 i = 0;
    while (i < length && (tag[i] == '/' || tag[i] == ' ' || tag[i] == '\t')) ++i;
    while (*name && i < length) if (lower_ascii((char)tag[i++]) != *name++) return 0;
    return !*name && (i == length || tag[i] == ' ' || tag[i] == '\t' || tag[i] == '\r' || tag[i] == '\n' || tag[i] == '/' || tag[i] == '>');
}
static void append_char(char *out, u16 cap, u16 *used, char c) {
    if (*used + 1u < cap) out[(*used)++] = c;
}
static void append_newline(char *out, u16 cap, u16 *used) {
    if (*used && out[*used - 1u] != '\n') append_char(out, cap, used, '\n');
}
static void append_text(char *out, u16 cap, u16 *used, const char *text) {
    if (!text) return;
    while (*text && *used + 1u < cap) append_char(out, cap, used, *text++);
}
static int contains_ci(const char *text, const char *needle) {
    usize n = kstrlen(needle);
    if (!text || !needle || !n) return 0;
    for (usize i = 0; text[i]; ++i)
        if (starts_ci(text + i, needle)) return 1;
    return 0;
}
static void css_add_selector(const u8 *selector, u16 length) {
    while (length && (*selector == ' ' || *selector == '\t' || *selector == '\r' || *selector == '\n')) { ++selector; --length; }
    while (length && (selector[length - 1u] == ' ' || selector[length - 1u] == '\t' ||
                      selector[length - 1u] == '\r' || selector[length - 1u] == '\n')) --length;
    if (!length || length >= sizeof(web_hidden_selectors[0]) || web_hidden_selector_count >= 24u) return;
    /* Keep this deliberately small: complex combinators and pseudo-selectors are unsupported. */
    for (u16 i = 0; i < length; ++i)
        if (selector[i] == ' ' || selector[i] == '>' || selector[i] == ':' || selector[i] == '[') return;
    kmemcpy(web_hidden_selectors[web_hidden_selector_count], selector, length);
    web_hidden_selectors[web_hidden_selector_count][length] = 0;
    ++web_hidden_selector_count;
}
static void css_collect_hidden(const u8 *html, u16 length) {
    u16 i = 0;
    web_hidden_selector_count = 0;
    while (i < length && web_hidden_selector_count < 24u) {
        if (html[i] == '<' && starts_ci((const char *)html + i, "<style")) {
            u16 open_end = i;
            while (open_end < length && html[open_end] != '>') ++open_end;
            if (open_end >= length) break;
            u16 close = open_end + 1u;
            while (close + 7u < length && !(html[close] == '<' && starts_ci((const char *)html + close, "</style"))) ++close;
            u16 rule = (u16)(open_end + 1u);
            while (rule < close && web_hidden_selector_count < 24u) {
                u16 brace = rule, finish;
                while (brace < close && html[brace] != '{') ++brace;
                if (brace >= close) break;
                finish = (u16)(brace + 1u);
                while (finish < close && html[finish] != '}') ++finish;
                if (finish >= close) break;
                {
                    char declarations[256];
                    u16 dlen = (u16)(finish - brace - 1u);
                    if (dlen >= sizeof(declarations)) dlen = sizeof(declarations) - 1u;
                    kmemcpy(declarations, html + brace + 1u, dlen); declarations[dlen] = 0;
                    if (contains_ci(declarations, "display:none") || contains_ci(declarations, "display: none") ||
                        contains_ci(declarations, "visibility:hidden") || contains_ci(declarations, "visibility: hidden")) {
                        u16 part = rule;
                        while (part < brace) {
                            u16 comma = part;
                            while (comma < brace && html[comma] != ',') ++comma;
                            css_add_selector(html + part, (u16)(comma - part));
                            part = (u16)(comma + 1u);
                        }
                    }
                }
                rule = (u16)(finish + 1u);
            }
            i = close < length ? (u16)(close + 8u) : length;
        } else ++i;
    }
}
static int css_selector_matches(const u8 *tag, u16 length, const char *selector) {
    char value[128];
    u16 name_end = 0;
    while (name_end < length && tag[name_end] != ' ' && tag[name_end] != '\t' && tag[name_end] != '\r' && tag[name_end] != '\n' &&
           tag[name_end] != '/' && tag[name_end] != '>') ++name_end;
    if (!selector[0]) return 0;
    if (selector[0] == '.') {
        if (!tag_attribute(tag, length, "class", value, sizeof(value))) return 0;
        usize wanted = kstrlen(selector + 1);
        for (usize i = 0; value[i];) {
            while (value[i] == ' ' || value[i] == '\t') ++i;
            usize start = i;
            while (value[i] && value[i] != ' ' && value[i] != '\t') ++i;
            if (i - start == wanted && kmemcmp(value + start, selector + 1, wanted) == 0) return 1;
        }
        return 0;
    }
    if (selector[0] == '#') {
        if (!tag_attribute(tag, length, "id", value, sizeof(value))) return 0;
        return kstrcmp(value, selector + 1) == 0;
    }
    return (usize)name_end == kstrlen(selector) && starts_ci((const char *)tag, selector);
}
static int css_is_hidden(const u8 *tag, u16 length) {
    for (u8 i = 0; i < web_hidden_selector_count; ++i)
        if (css_selector_matches(tag, length, web_hidden_selectors[i])) return 1;
    return 0;
}

static u16 html_to_text_impl(const u8 *html, u16 length, char *out, u16 capacity, int script_depth);

static u16 js_collect_document_writes(const u8 *script, u16 length, u8 *out, u16 capacity) {
    u16 i = 0, used = 0;
    while (i < length && used + 2u < capacity) {
        int writeln = 0;
        if (!starts_ci((const char *)script + i, "document.write")) { ++i; continue; }
        i = (u16)(i + 14u);
        if (starts_ci((const char *)script + i, "ln")) { writeln = 1; i = (u16)(i + 2u); }
        while (i < length && (script[i] == ' ' || script[i] == '\t' || script[i] == '\r' || script[i] == '\n')) ++i;
        if (i >= length || script[i++] != '(') continue;
        while (i < length && (script[i] == ' ' || script[i] == '\t' || script[i] == '\r' || script[i] == '\n')) ++i;
        if (i >= length || (script[i] != '\'' && script[i] != '"')) continue;
        {
            u8 quote = script[i++];
            while (i < length && script[i] != quote && used + 1u < capacity) {
                u8 c = script[i++];
                if (c == '\\' && i < length) {
                    u8 escaped = script[i++];
                    if (escaped == 'n') c = '\n';
                    else if (escaped == 'r') c = '\r';
                    else if (escaped == 't') c = '\t';
                    else c = escaped;
                }
                out[used++] = c;
            }
            if (i < length && script[i] == quote) ++i;
            if (writeln && used + 1u < capacity) out[used++] = '\n';
        }
    }
    out[used] = 0;
    return used;
}
static int find_script_close(const u8 *html, u16 length, u16 start, u16 *close_start, u16 *close_end) {
    for (u16 i = start; i + 8u < length; ++i) {
        if (html[i] == '<' && starts_ci((const char *)html + i, "</script")) {
            u16 end = i;
            while (end < length && html[end] != '>') ++end;
            if (end < length) { *close_start = i; *close_end = end; return 0; }
            return -1;
        }
    }
    return -1;
}

static u16 html_to_text_impl(const u8 *html, u16 length, char *out, u16 capacity, int script_depth) {
    u16 i = 0, used = 0;
    int skip_script = 0, skip_style = 0, skip_head = 0;
    int pending_space = 0, in_pre = 0, ordered_list = 0;
    u16 ordered_item = 0;
#ifndef DENOS_HTTP_TEST_MODE
    int active_link = -1;
    web_link_count = 0;
    web_image_count = 0;
#endif
    while (i < length && used + 8u < capacity) {
        if (html[i] == '<') {
            u16 start, end;
            int closing;
            char attribute[HTTP_PATH_CAP];
            while (i < length && html[i] == '<') {
                start = (u16)(i + 1u);
                end = start;
                while (end < length && html[end] != '>') ++end;
                if (end >= length) { append_char(out, capacity, &used, '<'); ++i; break; }
                closing = start < end && html[start] == '/';
                if (tag_name_is(html + start, (u16)(end - start), "script")) {
                    if (!closing) {
                        u16 close_start, close_end;
                        char source_attribute[32];
                        int external_script = tag_attribute(html + start, (u16)(end - start), "src",
                                                            source_attribute, sizeof(source_attribute));
                        if (script_depth == 0 && !external_script &&
                            find_script_close(html, length, (u16)(end + 1u), &close_start, &close_end) == 0) {
                            u16 generated = js_collect_document_writes(html + end + 1u,
                                (u16)(close_start - end - 1u), web_script_html, sizeof(web_script_html));
                            if (generated) {
                                u16 fragment = html_to_text_impl(web_script_html, generated,
                                    web_script_text, sizeof(web_script_text), script_depth + 1);
                                append_text(out, capacity, &used, web_script_text);
                                if (fragment) pending_space = 1;
                            }
                            i = (u16)(close_end + 1u);
                            continue;
                        }
                        if (find_script_close(html, length, (u16)(end + 1u), &close_start, &close_end) == 0)
                            i = (u16)(close_end + 1u);
                        else i = (u16)(end + 1u);
                        continue;
                    }
                    i = (u16)(end + 1u); continue;
                }
                if (tag_name_is(html + start, (u16)(end - start), "style")) {
                    skip_style = !closing; i = (u16)(end + 1u); continue;
                }
                if (tag_name_is(html + start, (u16)(end - start), "head")) {
                    skip_head = !closing; i = (u16)(end + 1u); continue;
                }
                if (skip_script || skip_style || skip_head) { i = (u16)(end + 1u); continue; }
                if (!closing && (tag_name_is(html + start, (u16)(end - start), "a"))) {
#ifndef DENOS_HTTP_TEST_MODE
                    if (tag_attribute(html + start, (u16)(end - start), "href", attribute, sizeof(attribute)))
                        active_link = web_add_link(attribute);
                    else active_link = -1;
#endif
                } else if (closing && tag_name_is(html + start, (u16)(end - start), "a")) {
#ifndef DENOS_HTTP_TEST_MODE
                    if (active_link >= 0) {
                        char marker[12];
                        (void)ksnprintf(marker, sizeof(marker), " [%u]", (unsigned)(active_link + 1));
                        append_text(out, capacity, &used, marker);
                    }
                    active_link = -1;
#endif
                }
                if (!closing && (tag_name_is(html + start, (u16)(end - start), "img"))) {
                    char alt[80];
                    int have_alt = tag_attribute(html + start, (u16)(end - start), "alt", alt, sizeof(alt));
#ifndef DENOS_HTTP_TEST_MODE
                    char src[HTTP_PATH_CAP];
                    int have_src = tag_attribute(html + start, (u16)(end - start), "src", src, sizeof(src));
#endif
                    append_text(out, capacity, &used, "[image: ");
                    if (have_alt && alt[0]) append_text(out, capacity, &used, alt);
                    else append_text(out, capacity, &used, "no description");
                    append_char(out, capacity, &used, ']');
#ifndef DENOS_HTTP_TEST_MODE
                    if (have_src) (void)web_add_image(src, have_alt ? alt : "");
#endif
                    i = (u16)(end + 1u); pending_space = 1; continue;
                }
                if (!closing && tag_name_is(html + start, (u16)(end - start), "input")) {
                    char type[24], value[64], placeholder[64];
                    int have_type = tag_attribute(html + start, (u16)(end - start), "type", type, sizeof(type));
                    int have_value = tag_attribute(html + start, (u16)(end - start), "value", value, sizeof(value));
                    int have_placeholder = tag_attribute(html + start, (u16)(end - start), "placeholder", placeholder, sizeof(placeholder));
                    append_text(out, capacity, &used, "[ ");
                    if (have_type && type[0]) append_text(out, capacity, &used, type);
                    else append_text(out, capacity, &used, "input");
                    if (have_value && value[0]) { append_text(out, capacity, &used, ": "); append_text(out, capacity, &used, value); }
                    else if (have_placeholder && placeholder[0]) { append_text(out, capacity, &used, ": "); append_text(out, capacity, &used, placeholder); }
                    append_text(out, capacity, &used, " ]");
                    i = (u16)(end + 1u); pending_space = 1; continue;
                }
                {
                    int hidden_attribute = !closing && tag_attribute(html + start, (u16)(end - start), "hidden", attribute, sizeof(attribute));
                    int hidden_style = !closing && tag_attribute(html + start, (u16)(end - start), "style", attribute, sizeof(attribute)) &&
                        (contains_ci(attribute, "display:none") || contains_ci(attribute, "display: none") ||
                         contains_ci(attribute, "visibility:hidden") || contains_ci(attribute, "visibility: hidden"));
                    int hidden_css = !closing && css_is_hidden(html + start, (u16)(end - start));
                    if (hidden_attribute || hidden_style || hidden_css) {
                    /* Inline display:none: suppress this element's textual contents. */
                    char tag_name[24];
                    u16 name_end = start;
                    usize name_length;
                    while (name_end < end && html[name_end] != ' ' && html[name_end] != '\t' && html[name_end] != '/') ++name_end;
                    name_length = (usize)(name_end - start);
                    if (name_length >= sizeof(tag_name)) name_length = sizeof(tag_name) - 1u;
                    kmemcpy(tag_name, html + start, name_length); tag_name[name_length] = 0;
                    i = (u16)(end + 1u);
                    if (end == start || html[end - 1u] != '/') {
                        while (i < length) {
                            if (html[i] == '<' && i + 2u < length && html[i + 1u] == '/' &&
                                tag_name_is(html + i + 2u, (u16)(length - i - 2u), tag_name)) {
                                while (i < length && html[i] != '>') ++i;
                                if (i < length) ++i;
                                break;
                            }
                            ++i;
                        }
                    }
                    continue;
                    }
                }
                if (tag_name_is(html + start, (u16)(end - start), "strong") ||
                    tag_name_is(html + start, (u16)(end - start), "b")) {
                    append_text(out, capacity, &used, closing ? "**" : "**");
                } else if (tag_name_is(html + start, (u16)(end - start), "em") ||
                           tag_name_is(html + start, (u16)(end - start), "i")) {
                    append_char(out, capacity, &used, '*');
                } else if (tag_name_is(html + start, (u16)(end - start), "code")) {
                    append_char(out, capacity, &used, '`');
                } else if (tag_name_is(html + start, (u16)(end - start), "del") ||
                           tag_name_is(html + start, (u16)(end - start), "s")) {
                    append_text(out, capacity, &used, "~~");
                }
                if (!closing && (tag_name_is(html + start, (u16)(end - start), "h1") ||
                    tag_name_is(html + start, (u16)(end - start), "h2") ||
                    tag_name_is(html + start, (u16)(end - start), "h3") ||
                    tag_name_is(html + start, (u16)(end - start), "h4") ||
                    tag_name_is(html + start, (u16)(end - start), "h5") ||
                    tag_name_is(html + start, (u16)(end - start), "h6"))) {
                    append_newline(out, capacity, &used); append_newline(out, capacity, &used);
                    append_text(out, capacity, &used,
                        tag_name_is(html + start, (u16)(end - start), "h1") ? "# " :
                        tag_name_is(html + start, (u16)(end - start), "h2") ? "## " : "### ");
                    pending_space = 0;
                } else if (closing && (tag_name_is(html + start, (u16)(end - start), "h1") ||
                    tag_name_is(html + start, (u16)(end - start), "h2") ||
                    tag_name_is(html + start, (u16)(end - start), "h3") ||
                    tag_name_is(html + start, (u16)(end - start), "h4") ||
                    tag_name_is(html + start, (u16)(end - start), "h5") ||
                    tag_name_is(html + start, (u16)(end - start), "h6"))) {
                    append_newline(out, capacity, &used); append_newline(out, capacity, &used); pending_space = 0;
                } else if (tag_name_is(html + start, (u16)(end - start), "br")) {
                    append_newline(out, capacity, &used); pending_space = 0;
                } else if (tag_name_is(html + start, (u16)(end - start), "hr")) {
                    append_newline(out, capacity, &used);
                    append_text(out, capacity, &used, "------------------------------------------------------------");
                    append_newline(out, capacity, &used); pending_space = 0;
                } else if (tag_name_is(html + start, (u16)(end - start), "p") ||
                    tag_name_is(html + start, (u16)(end - start), "div") ||
                    tag_name_is(html + start, (u16)(end - start), "section") ||
                    tag_name_is(html + start, (u16)(end - start), "article") ||
                    tag_name_is(html + start, (u16)(end - start), "header") ||
                    tag_name_is(html + start, (u16)(end - start), "footer") ||
                    tag_name_is(html + start, (u16)(end - start), "nav") ||
                    tag_name_is(html + start, (u16)(end - start), "main")) {
                    append_newline(out, capacity, &used);
                    if (closing) append_newline(out, capacity, &used);
                    pending_space = 0;
                } else if (tag_name_is(html + start, (u16)(end - start), "li") && !closing) {
                    append_newline(out, capacity, &used);
                    if (ordered_list) {
                        char marker[12];
                        (void)ksnprintf(marker, sizeof(marker), "  %u. ", (unsigned)++ordered_item);
                        append_text(out, capacity, &used, marker);
                    } else append_text(out, capacity, &used, "  * ");
                    pending_space = 0;
                } else if (tag_name_is(html + start, (u16)(end - start), "ol")) {
                    ordered_list = !closing;
                    if (!closing) ordered_item = 0;
                    append_newline(out, capacity, &used);
                } else if (tag_name_is(html + start, (u16)(end - start), "ul") && !closing) {
                    append_newline(out, capacity, &used);
                } else if (tag_name_is(html + start, (u16)(end - start), "pre")) {
                    if (!closing) { append_newline(out, capacity, &used); in_pre = 1; }
                    else { in_pre = 0; append_newline(out, capacity, &used); }
                    pending_space = 0;
                } else if (tag_name_is(html + start, (u16)(end - start), "blockquote")) {
                    if (!closing) { append_newline(out, capacity, &used); append_text(out, capacity, &used, "> "); }
                    else append_newline(out, capacity, &used);
                    pending_space = 0;
                } else if (tag_name_is(html + start, (u16)(end - start), "tr")) {
                    if (closing) append_newline(out, capacity, &used);
                    else append_newline(out, capacity, &used);
                    pending_space = 0;
                } else if (tag_name_is(html + start, (u16)(end - start), "td") ||
                           tag_name_is(html + start, (u16)(end - start), "th")) {
                    if (used && out[used - 1u] != '\n') append_text(out, capacity, &used, " | ");
                    pending_space = 0;
                }
                i = (u16)(end + 1u);
                break;
            }
            continue;
        }
        if (skip_script || skip_style || skip_head) { ++i; continue; }
        if (html[i] == '&') {
            u16 end = (u16)(i + 1u);
            char decoded[4];
            int count;
            while (end < length && (u16)(end - i) <= 12u && html[end] != ';' && html[end] != ' ' && html[end] != '<') ++end;
            if (end < length && html[end] == ';' && (u16)(end - i) <= 12u &&
                (count = entity_value((const char *)html + i + 1u, (u16)(end - i - 1u), decoded)) > 0) {
                if (pending_space && used && out[used - 1u] != '\n' && !in_pre) append_char(out, capacity, &used, ' ');
                pending_space = 0;
                for (int d = 0; d < count; ++d) {
                    u8 value = (u8)decoded[d];
                    if ((value == '\r' || value == '\t') && !in_pre) append_char(out, capacity, &used, ' ');
                    else if (value == '\n') append_newline(out, capacity, &used);
                    else if (value >= 32u && value != 127u) append_char(out, capacity, &used, decoded[d]);
                }
                i = (u16)(end + 1u);
                continue;
            }
        }
        {
            char c = (char)html[i++];
            if (!in_pre && (c == '\r' || c == '\n' || c == '\t' || c == ' ')) { pending_space = 1; continue; }
            if ((u8)c < 32u && c != '\n' && c != '\t') continue;
            if (!in_pre && pending_space && used && out[used - 1u] != '\n') append_char(out, capacity, &used, ' ');
            pending_space = 0;
            append_char(out, capacity, &used, c);
        }
    }
    while (used && (out[used - 1u] == ' ' || out[used - 1u] == '\n')) --used;
    out[used] = 0;
    return used;
}
static u16 html_to_text(const u8 *html, u16 length, char *out, u16 capacity) {
#ifndef DENOS_HTTP_TEST_MODE
    web_link_count = 0;
    web_image_count = 0;
#endif
    css_collect_hidden(html, length);
    return html_to_text_impl(html, length, out, capacity, 0);
}
#ifdef DENOS_HTTP_TEST_MODE
/* Keep host transport tests non-interactive. The real kernel uses the pager
 * below, while tests capture the rendered text without waiting for keys. */
static void print_wrapped(const char *text) {
    u16 column = 0;
    for (usize i = 0; text[i]; ++i) {
        char c = text[i];
        if (c == '\n') { serial_putc('\n'); column = 0; continue; }
        if (column >= WEB_VIEW_WIDTH && c == ' ') { serial_putc('\n'); column = 0; continue; }
        serial_putc(c);
        if (++column >= WEB_VIEW_WIDTH && text[i + 1u] && text[i + 1u] != '\n') {
            if (text[i + 1u] == ' ') ++i;
            serial_putc('\n'); column = 0;
        }
    }
    serial_putc('\n');
}
#else
static u16 web_prepare_lines(const char *text) {
    usize length = kstrnlen(text, sizeof(text_buffer));
    usize position = 0;
    u16 count = 0;
    while (position < length && count < HTTP_TEXT_CAP) {
        usize start = position;
        usize width = 0;
        while (position < length && text[position] != '\n' && width < WEB_VIEW_WIDTH) {
            ++position;
            ++width;
        }
        web_line_starts[count] = (u16)start;
        web_line_lengths[count] = (u8)width;
        ++count;
        if (position < length && text[position] == '\n') {
            ++position;
        } else if (position < length && text[position] == ' ') {
            /* Avoid starting the next visual row with a wrap-only space. */
            ++position;
        }
    }
    if (!count) {
        web_line_starts[0] = 0;
        web_line_lengths[0] = 0;
        count = 1;
    }
    return count;
}

static void web_view_draw(const char *title, u16 first_line, u16 line_count,
                          int truncated) {
    char footer[112];
    u16 last_line = (u16)(first_line + WEB_VIEW_ROWS);
    if (last_line > line_count) last_line = line_count;

    serial_write("\033[2J\033[H\033[1;36m");
    for (u16 i = 0; title[i] && i < WEB_VIEW_WIDTH; ++i) serial_putc(title[i]);
    serial_write("\033[0m\n");
    serial_write("PgUp/PgDn scroll | Up/Down line | L links | I images | G go | B back | F forward | Q exit\n");

    for (u16 row = 0; row < WEB_VIEW_ROWS; ++row) {
        u16 line = (u16)(first_line + row);
        if (line < line_count) {
            u16 start = web_line_starts[line];
            u8 length = web_line_lengths[line];
            u8 column = 0;
            int heading = length && text_buffer[start] == '#';
            if (heading) {
                while (column < length && text_buffer[start + column] == '#') ++column;
                if (column < length && text_buffer[start + column] == ' ') ++column;
                serial_write("\033[1;36m");
            }
            while (column < length) {
                char c = text_buffer[start + column];
                if (c == '[' && column + 2u < length &&
                    text_buffer[start + column + 1u] >= '1' &&
                    text_buffer[start + column + 1u] <= '9' &&
                    text_buffer[start + column + 2u] == ']') {
                    serial_write("\033[1;33m[");
                    serial_putc(text_buffer[start + column + 1u]);
                    serial_write("]\033[1;36m");
                    column = (u8)(column + 3u);
                    continue;
                }
                if (c == '*' && column + 1u < length && text_buffer[start + column + 1u] == '*') {
                    serial_write("\033[1m");
                    column = (u8)(column + 2u);
                    continue;
                }
                serial_putc(c);
                ++column;
            }
        }
        /* Keep inline emphasis and heading/link colors scoped to this row. */
        serial_write("\033[0m");
        serial_putc('\n');
    }

    (void)ksnprintf(footer, sizeof(footer),
        "Lines %u-%u/%u | %u links | %u images%s",
        (unsigned)(line_count ? first_line + 1u : 0u), (unsigned)last_line,
        (unsigned)line_count, (unsigned)web_link_count, (unsigned)web_image_count,
        truncated ? " | INCOMPLETE" : "");
    serial_write("\033[25;1H\033[7m");
    for (u16 i = 0; footer[i] && i < WEB_VIEW_WIDTH; ++i) serial_putc(footer[i]);
    serial_write("\033[0m");
}

static void web_links_view(void) {
    u16 selected = 0, first = 0;
    for (;;) {
        serial_write("\033[2J\033[H\033[1;36mDENOS LINKS\033[0m\n");
        serial_write("Up/Down select | Enter open | Esc return | Q exit\n\n");
        for (u16 row = 0; row < 17u; ++row) {
            u16 index = (u16)(first + row);
            if (index < web_link_count) {
                char line[WEB_VIEW_WIDTH + 1u];
                (void)ksnprintf(line, sizeof(line), "%c%2u  %.72s",
                    index == selected ? '>' : ' ', (unsigned)(index + 1u), web_links[index].url);
                serial_write(line);
            }
            serial_putc('\n');
        }
        serial_write("\033[25;1H\033[7m");
        serial_write(web_link_count ? "Enter: navigate   Esc: page" : "No links on this page");
        serial_write("\033[0m");
        {
            int key = serial_getkey();
            if (key == 27 || key == 'q' || key == 'Q') return;
            if (key == KEY_UP && selected > 0) --selected;
            else if (key == KEY_DOWN && selected + 1u < web_link_count) ++selected;
            else if (key == KEY_PAGEUP) selected = selected > 17u ? (u16)(selected - 17u) : 0;
            else if (key == KEY_PAGEDOWN) {
                if (web_link_count && selected + 17u < web_link_count) selected = (u16)(selected + 17u);
                else if (web_link_count) selected = (u16)(web_link_count - 1u);
            } else if ((key == '\n' || key == '\r') && web_link_count) {
                web_selected_link = selected;
                return;
            }
            if (selected < first) first = selected;
            else if (selected >= first + 17u) first = (u16)(selected - 16u);
        }
    }
}

#ifndef DENOS_HTTP_TEST_MODE
static void web_show_image(u16 index);
#endif
static void web_images_view(void) {
    u16 selected = 0, first = 0;
    if (!web_image_count) {
        serial_write("\033[2J\033[HNo images were found in this page. Press any key to return.");
        (void)serial_getkey(); return;
    }
    for (;;) {
        serial_write("\033[2J\033[H\033[1;36mDENOS IMAGES\033[0m\n");
        serial_write("Up/Down select | Enter view image | Esc return\n\n");
        for (u16 row = 0; row < 17u; ++row) {
            u16 index = (u16)(first + row);
            if (index < web_image_count) {
                char line[WEB_VIEW_WIDTH + 1u];
                (void)ksnprintf(line, sizeof(line), "%c%2u  %.66s",
                    index == selected ? '>' : ' ', (unsigned)(index + 1u),
                    web_images[index].alt[0] ? web_images[index].alt : web_images[index].url);
                serial_write(line);
            }
            serial_putc('\n');
        }
        serial_write("\033[25;1H\033[7mPNG, QOI and BMP supported\033[0m");
        {
            int key = serial_getkey();
            if (key == 27 || key == 'q' || key == 'Q') return;
            if (key == KEY_UP && selected > 0) --selected;
            else if (key == KEY_DOWN && selected + 1u < web_image_count) ++selected;
            else if (key == KEY_PAGEUP) selected = selected > 17u ? (u16)(selected - 17u) : 0;
            else if (key == KEY_PAGEDOWN) {
                if (selected + 17u < web_image_count) selected = (u16)(selected + 17u);
                else selected = (u16)(web_image_count - 1u);
            } else if (key == '\n' || key == '\r') {
                web_show_image(selected);
            }
            if (selected < first) first = selected;
            else if (selected >= first + 17u) first = (u16)(selected - 16u);
        }
    }
}

static void web_view_page(const char *title, const char *text, int truncated) {
    u16 line_count = web_prepare_lines(text);
    u16 first_line = 0;
    web_selected_link = -1;
    web_back_requested = 0;
    web_exit_requested = 0;
    for (;;) {
        web_view_draw(title, first_line, line_count, truncated);
        {
            int key = serial_getkey();
            if (key == 'q' || key == 'Q') { web_exit_requested = 1; break; }
            if (key == 'b' || key == 'B') { web_back_requested = 1; break; }
            if (key == 'f' || key == 'F') { web_forward_requested = 1; break; }
            if (key == 'g' || key == 'G') {
                serial_write("\nOpen URL (http:// or https://): ");
                serial_readline(web_go_url, sizeof(web_go_url));
                if (web_go_url[0]) web_go_requested = 1;
                break;
            }
            if (key >= '1' && key <= '9' && (u16)(key - '1') < web_link_count) {
                web_selected_link = key - '1'; break;
            }
            if (key == 'l' || key == 'L') {
                web_links_view();
                if (web_selected_link >= 0) break;
                continue;
            }
            if (key == 'i' || key == 'I') { web_images_view(); continue; }
            if (key == KEY_HOME) first_line = 0;
            else if (key == KEY_END)
                first_line = line_count > WEB_VIEW_ROWS ? (u16)(line_count - WEB_VIEW_ROWS) : 0;
            else if (key == KEY_PAGEDOWN || key == ' ') {
                u16 max_first = line_count > WEB_VIEW_ROWS ?
                    (u16)(line_count - WEB_VIEW_ROWS) : 0;
                if (first_line < max_first) {
                    u32 next = (u32)first_line + WEB_VIEW_STEP;
                    first_line = next < max_first ? (u16)next : max_first;
                }
            } else if (key == KEY_PAGEUP) {
                first_line = first_line > WEB_VIEW_STEP ?
                    (u16)(first_line - WEB_VIEW_STEP) : 0;
            } else if (key == KEY_DOWN || key == 'j') {
                if ((u32)first_line + WEB_VIEW_ROWS < line_count) ++first_line;
            } else if (key == KEY_UP || key == 'k') {
                if (first_line) --first_line;
            }
        }
    }
    serial_putc('\n');
}
#endif

static int http_stream_send(int secure, const void *data, u16 length, u32 timeout) {
    return secure ? net_tls_send(data, length, timeout) : net_tcp_send(data, length, timeout);
}
static int http_stream_receive(int secure, void *data, u16 capacity, u32 timeout) {
    return secure ? net_tls_receive(data, capacity, timeout) : net_tcp_receive(data, capacity, timeout);
}
static void http_stream_close(int secure, u32 timeout) {
    if (secure) net_tls_close(timeout);
    else net_tcp_close(timeout);
}

#ifndef DENOS_HTTP_TEST_MODE
static u32 image_read_le32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static u32 image_read_be32(const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}
static int http_fetch_image(const char *url_text, u16 *downloaded) {
    http_url_t url;
    u8 address[4];
    u16 response_length = 0, header_end, body_length;
    const u8 *encoding, *transfer, *content_length_header;
    u16 encoding_length, transfer_length, content_length_size;
    u32 expected = 0;
    int has_length = 0;
    if (!url_text || !downloaded || parse_url(url_text, &url) < 0) return -1;
    if (url.secure && !net_tls_available()) return -1;
    if (net_dns_resolve(url.host, address, 300u) < 0 ||
        net_tcp_connect(address, url.port, HTTP_IDLE_TIMEOUT) < 0) return -1;
    if (url.secure && net_tls_connect(url.host) < 0) {
        net_tcp_close(50u); return -1;
    }
    if ((!url.secure && url.port == 80u) || (url.secure && url.port == 443u))
        (void)ksnprintf(request_buffer, sizeof(request_buffer),
            "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: DenOS/0.5.7\r\nAccept: image/png,image/qoi,image/bmp,image/*;q=0.5,*/*;q=0.1\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
            url.path, url.host);
    else
        (void)ksnprintf(request_buffer, sizeof(request_buffer),
            "GET %s HTTP/1.0\r\nHost: %s:%u\r\nUser-Agent: DenOS/0.5.7\r\nAccept: image/png,image/qoi,image/bmp,image/*;q=0.5,*/*;q=0.1\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
            url.path, url.host, (unsigned)url.port);
    {
        usize request_length = kstrlen(request_buffer);
        if (request_length > 0xFFFFu ||
            http_stream_send(url.secure, request_buffer, (u16)request_length, HTTP_IDLE_TIMEOUT) < 0) {
            http_stream_close(url.secure, 50u); return -1;
        }
    }
    while (response_length < sizeof(response_buffer) - 1u) {
        u16 room = (u16)((sizeof(response_buffer) - 1u) - response_length);
        u16 ask = room > 1024u ? 1024u : room;
        int got = http_stream_receive(url.secure, response_buffer + response_length, ask, HTTP_IDLE_TIMEOUT);
        if (got == 0) break;
        if (got < 0) { http_stream_close(url.secure, 50u); return -1; }
        response_length = (u16)(response_length + got);
    }
    http_stream_close(url.secure, 100u);
    if (response_length >= sizeof(response_buffer) - 1u || response_length < 12u ||
        !starts_ci((const char *)response_buffer, "HTTP/")) return -1;
    {
        u16 i = 0;
        while (i < response_length && response_buffer[i] != ' ') ++i;
        while (i < response_length && response_buffer[i] == ' ') ++i;
        if (i + 2u >= response_length || response_buffer[i] != '2' ||
            response_buffer[i + 1u] != '0' || response_buffer[i + 2u] != '0') return -1;
    }
    {
        int marker = find_bytes(response_buffer, response_length, "\r\n\r\n", 4);
        if (marker < 0) return -1;
        header_end = (u16)(marker + 4);
    }
    if (header_count(response_buffer, header_end, "Content-Length") > 1u ||
        header_count(response_buffer, header_end, "Transfer-Encoding") > 1u) return -1;
    encoding = header_value(response_buffer, header_end, "Content-Encoding", &encoding_length);
    transfer = header_value(response_buffer, header_end, "Transfer-Encoding", &transfer_length);
    content_length_header = header_value(response_buffer, header_end, "Content-Length", &content_length_size);
    if (encoding && !header_token_is(encoding, encoding_length, "identity")) return -1;
    if (content_length_header && transfer) return -1;
    if (content_length_header) {
        if (parse_decimal_header(content_length_header, content_length_size, &expected) < 0 || expected > HTTP_BODY_CAP) return -1;
        has_length = 1;
    }
    body_length = (u16)(response_length - header_end);
    if (transfer) {
        u16 decoded = 0;
        if (!header_token_is(transfer, transfer_length, "chunked") ||
            decode_chunked(response_buffer + header_end, body_length, body_buffer, sizeof(body_buffer), &decoded) < 0)
            return -1;
        body_length = decoded;
    } else {
        if (body_length > HTTP_BODY_CAP || (has_length && (u32)body_length < expected)) return -1;
        if (has_length && (u32)body_length > expected) body_length = (u16)expected;
        kmemcpy(body_buffer, response_buffer + header_end, body_length);
    }
    if (body_length < 16u) return -1;
    *downloaded = body_length;
    return 0;
}
static int image_decode_bmp(const u8 *data, u16 length, u32 *width, u32 *height) {
    u32 pixel_offset, dib_size, row_size, total_size;
    i32 signed_width, signed_height;
    u16 bits;
    if (!data || length < 54u || data[0] != 'B' || data[1] != 'M') return -1;
    pixel_offset = image_read_le32(data + 10);
    dib_size = image_read_le32(data + 14);
    signed_width = (i32)image_read_le32(data + 18);
    signed_height = (i32)image_read_le32(data + 22);
    bits = (u16)(data[28] | ((u16)data[29] << 8));
    if (dib_size < 40u || signed_width <= 0 || signed_height == 0 ||
        signed_width > 640 || signed_height > 480 || signed_height < -480 ||
        data[26] != 1u || data[27] != 0u || (bits != 24u && bits != 32u) ||
        image_read_le32(data + 30) != 0u) return -1;
    *width = (u32)signed_width;
    *height = (u32)(signed_height < 0 ? -signed_height : signed_height);
    row_size = (((u32)*width * bits + 31u) / 32u) * 4u;
    total_size = row_size * *height;
    if (pixel_offset < 14u + dib_size || pixel_offset > length ||
        total_size > (u32)length - pixel_offset ||
        (u64)*width * *height > WEB_IMAGE_PIXELS) return -1;
    for (u32 y = 0; y < *height; ++y) {
        u32 source_y = signed_height > 0 ? (*height - 1u - y) : y;
        const u8 *row = data + pixel_offset + source_y * row_size;
        for (u32 x = 0; x < *width; ++x) {
            const u8 *pixel = row + x * (bits / 8u);
            web_image_pixels[y * *width + x] = ((u32)pixel[2] << 16) |
                ((u32)pixel[1] << 8) | (u32)pixel[0];
        }
    }
    return 0;
}
static int image_decode(const u8 *data, u16 length, u32 *width, u32 *height) {
    if (!data || length < 16u || !width || !height) return -1;
    if (data[0] == 137u && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        u32 png_width, png_height;
        if (length < 24u) return -1;
        png_width = image_read_be32(data + 16u);
        png_height = image_read_be32(data + 20u);
        if (!png_width || !png_height || png_width > 640u || png_height > 480u) return -1;
        return png_decode_rgb32(data, length, web_image_pixels, WEB_IMAGE_PIXELS,
                                web_png_workspace, sizeof(web_png_workspace), width, height);
    }
    if (data[0] == 'q' && data[1] == 'o' && data[2] == 'i' && data[3] == 'f') {
        *width = image_read_be32(data + 4);
        *height = image_read_be32(data + 8);
        if (!*width || !*height || *width > 640u || *height > 480u ||
            (u64)*width * *height > WEB_IMAGE_PIXELS) return -1;
        return qoi_decode_rgb32(data, length, web_image_pixels, WEB_IMAGE_PIXELS, *width, *height);
    }
    return image_decode_bmp(data, length, width, height);
}
static void web_show_image(u16 index) {
    u16 downloaded = 0;
    u32 width = 0, height = 0;
    long max_width, max_height, draw_width, draw_height;
    const char *label;
    if (index >= web_image_count) return;
    serial_write("\nDownloading image...\n");
    if (http_fetch_image(web_images[index].url, &downloaded) < 0) {
        serial_write("Image download failed or the server returned an unsupported response.\n");
        (void)serial_getkey(); return;
    }
    if (image_decode(body_buffer, downloaded, &width, &height) < 0) {
        serial_write("Unsupported image. DenOS currently displays PNG, QOI, and uncompressed 24/32-bit BMP images.\n");
        (void)serial_getkey(); return;
    }
    if (!gfx_start()) { serial_write("Graphics mode could not be started.\n"); return; }
    gfx_clear(gfx_rgb(18, 22, 30));
    label = web_images[index].alt[0] ? web_images[index].alt : "Web image";
    gfx_text_ui_bold(10, 8, label, gfx_rgb(235, 240, 250));
    max_width = gfx_width() - 20;
    max_height = gfx_height() - 48;
    draw_width = (long)width;
    draw_height = (long)height;
    if (draw_width > max_width || draw_height > max_height) {
        if (draw_width * max_height > draw_height * max_width) {
            draw_height = draw_height * max_width / draw_width;
            draw_width = max_width;
        } else {
            draw_width = draw_width * max_height / draw_height;
            draw_height = max_height;
        }
    }
    if (draw_width < 1) draw_width = 1;
    if (draw_height < 1) draw_height = 1;
    gfx_blit_scaled_rgb32((gfx_width() - draw_width) / 2, 30,
                          width, height, draw_width, draw_height,
                          web_image_pixels, width);
    gfx_text_centered(0, gfx_height() - 14, gfx_width(), "Press any key to return", gfx_rgb(200, 210, 225));
    (void)gfx_wait_key();
    gfx_stop();
}
#endif

static int http_get_internal(const char *url_text, u32 redirect_depth) {
    http_url_t url;
    u8 address[4];
    u16 response_length = 0;
    u16 header_end, body_length;
    const u8 *content_type, *transfer_encoding, *content_encoding, *location, *content_length_header;
    u16 content_type_length, transfer_length, encoding_length, location_length, content_length_size;
    u32 expected_content_length = 0;
    int has_content_length = 0;
    u32 timeout = 600u;
    int parsed, status = 0, truncated = 0;
    if (!net_is_ready()) { serial_write("web: network interface is unavailable\n"); return -1; }
    parsed = parse_url(url_text, &url);
    if (parsed < 0) { serial_write("web: invalid URL; use http[s]://host[:port]/path\n"); return -3; }
#ifndef DENOS_HTTP_TEST_MODE
    {
        char origin[HTTP_HOST_CAP + 32u];
        int page_url_length;
        if (build_origin(&url, origin, sizeof(origin)) < 0 ||
            (page_url_length = ksnprintf(web_page_url, sizeof(web_page_url), "%s%s", origin, url.path)) < 0 ||
            (usize)page_url_length >= sizeof(web_page_url)) {
            serial_write("web: URL is too long for browser navigation.\n"); return -3;
        }
    }
#endif
    if (url.secure && !net_tls_available()) {
        serial_write("web: HTTPS is not enabled in this image; rebuild with the default 'make build' (TLS=1).\n");
        return -2;
    }
    serial_write("Resolving "); serial_write(url.host); serial_write("...\n");
    if (net_dns_resolve(url.host, address, 300u) < 0) {
        serial_write("web: DNS lookup failed; try 'net' and 'dns HOST'.\n"); return -4;
    }
    if (net_tcp_connect(address, url.port, timeout) < 0) {
        serial_write("web: could not connect to host (TCP timeout/refused).\n"); return -5;
    }
    if (url.secure) {
        if (net_tls_connect(url.host) < 0) {
            net_tcp_close(50u);
            serial_write("web: TLS setup failed; no unverified fallback was attempted.\n");
            return -6;
        }
    }
    if ((!url.secure && url.port == 80u) || (url.secure && url.port == 443u))
        (void)ksnprintf(request_buffer, sizeof(request_buffer),
            "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: DenOS-CLI/0.5.7\r\nAccept: text/html,text/plain;q=0.9,*/*;q=0.1\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
            url.path, url.host);
    else
        (void)ksnprintf(request_buffer, sizeof(request_buffer),
            "GET %s HTTP/1.0\r\nHost: %s:%u\r\nUser-Agent: DenOS-CLI/0.5.7\r\nAccept: text/html,text/plain;q=0.9,*/*;q=0.1\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
            url.path, url.host, (unsigned)url.port);
    {
        usize request_length = kstrlen(request_buffer);
        if (request_length > 0xFFFFu || http_stream_send(url.secure, request_buffer, (u16)request_length, HTTP_IDLE_TIMEOUT) < 0) {
            http_stream_close(url.secure, 50u);
            serial_write("web: failed to send request (TLS handshake/certificate check may have failed).\n"); return -7;
        }
    }
    while (response_length < sizeof(response_buffer) - 1u) {
        u16 room = (u16)((sizeof(response_buffer) - 1u) - response_length);
        u16 ask = room > 1024u ? 1024u : room;
        int got = http_stream_receive(url.secure, response_buffer + response_length, ask, HTTP_IDLE_TIMEOUT);
        if (got == 0) break;
        if (got < 0) {
            http_stream_close(url.secure, 50u);
            if (url.secure && got == -2) {
                serial_write("web: TLS protocol, record-authentication, or certificate verification failed; response discarded.\n");
                return -11;
            }
            if (!response_length) {
                serial_write("web: transport failed before an HTTP response arrived; see the TCP/TLS diagnostic above.\n");
                return -8;
            }
            /* A previously authenticated TLS record may be displayed, but
             * a broken transport must never be presented as a complete page. */
            truncated = 1;
            break;
        }
        response_length = (u16)(response_length + got);
    }
    if (response_length == sizeof(response_buffer) - 1u) truncated = 1;
    response_buffer[sizeof(response_buffer) - 1u] = 0;
    http_stream_close(url.secure, 100u);
    {
        int marker = find_bytes(response_buffer, response_length, "\r\n\r\n", 4);
        if (marker < 0) { serial_write("web: malformed HTTP response (missing header terminator).\n"); return -8; }
        header_end = (u16)(marker + 4);
    }
    if (response_length < 12u || !starts_ci((const char *)response_buffer, "HTTP/")) {
        serial_write("web: server returned an invalid HTTP status line.\n"); return -8;
    }
    {
        u16 i = 0;
        while (i < header_end && response_buffer[i] != ' ' && response_buffer[i] != '\r') ++i;
        if (i >= header_end || response_buffer[i] != ' ') {
            serial_write("web: malformed HTTP status line.\n"); return -8;
        }
        while (i < header_end && response_buffer[i] == ' ') ++i;
        if (i + 3u >= header_end || response_buffer[i] < '0' || response_buffer[i] > '9' ||
            response_buffer[i + 1u] < '0' || response_buffer[i + 1u] > '9' ||
            response_buffer[i + 2u] < '0' || response_buffer[i + 2u] > '9' ||
            (response_buffer[i + 3u] != ' ' && response_buffer[i + 3u] != '\r')) {
            serial_write("web: malformed HTTP status code.\n"); return -8;
        }
        status = (response_buffer[i] - '0') * 100 +
                 (response_buffer[i + 1u] - '0') * 10 +
                 (response_buffer[i + 2u] - '0');
        if (status < 100 || status > 599) { serial_write("web: invalid HTTP status code.\n"); return -8; }
    }
    /* Conflicting framing headers make response boundaries ambiguous. This
     * page reader has no reason to accept duplicate Content-Length or
     * Transfer-Encoding fields, or a response containing both. */
    if (header_count(response_buffer, header_end, "Content-Length") > 1u ||
        header_count(response_buffer, header_end, "Transfer-Encoding") > 1u) {
        serial_write("web: duplicate HTTP body-framing header rejected.\n");
        return -8;
    }
    content_type = header_value(response_buffer, header_end, "Content-Type", &content_type_length);
    transfer_encoding = header_value(response_buffer, header_end, "Transfer-Encoding", &transfer_length);
    content_encoding = header_value(response_buffer, header_end, "Content-Encoding", &encoding_length);
    location = header_value(response_buffer, header_end, "Location", &location_length);
    content_length_header = header_value(response_buffer, header_end, "Content-Length", &content_length_size);
    if (content_length_header && transfer_encoding) {
        serial_write("web: conflicting Content-Length and Transfer-Encoding headers rejected.\n");
        return -8;
    }
    if (content_length_header) {
        if (transfer_encoding && header_token_is(transfer_encoding, transfer_length, "chunked")) {
            has_content_length = 0;
        } else {
            if (parse_decimal_header(content_length_header, content_length_size,
                                     &expected_content_length) < 0) {
                serial_write("web: invalid Content-Length header.\n");
                return -8;
            }
            has_content_length = 1;
        }
    }
    if ((status == 301 || status == 302 || status == 303 || status == 307 || status == 308) && location && location_length) {
        char redirect[HTTP_PATH_CAP];
        u16 n = location_length < sizeof(redirect) - 1u ? location_length : (u16)(sizeof(redirect) - 1u);
        kmemcpy(redirect, location, n); redirect[n] = 0;
        if (redirect_depth == 0 &&
            (starts_ci(redirect, "http://") || starts_ci(redirect, "https://"))) {
            if (url.secure && starts_ci(redirect, "http://")) {
                serial_write("web: refusing HTTPS-to-HTTP downgrade redirect.\n");
                return -9;
            }
            serial_write("Following redirect.\n\n");
            return http_get_internal(redirect, redirect_depth + 1u);
        }
        serial_write("HTTP redirect destination is not supported (only one absolute redirect is followed).\n");
        return -9;
    }
    body_length = (u16)(response_length - header_end);
    if (has_content_length) {
        if ((u32)body_length < expected_content_length) truncated = 1;
        if ((u32)body_length > expected_content_length)
            body_length = (u16)(expected_content_length > 0xFFFFu ? 0xFFFFu : expected_content_length);
        if (expected_content_length > HTTP_BODY_CAP) truncated = 1;
    }
    if (content_encoding && !header_token_is(content_encoding, encoding_length, "identity")) {
        serial_write("Compressed/encoded HTTP response is not supported by this text browser.\n");
        return 0;
    }
    if (transfer_encoding && !header_token_is(transfer_encoding, transfer_length, "chunked")) {
        serial_write("Unsupported HTTP transfer encoding.\n");
        return 0;
    }
    if (transfer_encoding && header_token_is(transfer_encoding, transfer_length, "chunked")) {
        u16 decoded = 0;
        if (decode_chunked(response_buffer + header_end, body_length, body_buffer, sizeof(body_buffer), &decoded) == 0) {
            kmemcpy(body_buffer + decoded, "", 1); body_length = decoded;
        } else { serial_write("web: invalid chunked transfer body.\n"); return -10; }
    } else {
        if (body_length > sizeof(body_buffer)) {
            body_length = sizeof(body_buffer);
            truncated = 1;
        }
        kmemcpy(body_buffer, response_buffer + header_end, body_length);
    }
    if (content_type && (starts_ci((const char *)content_type, "image/") ||
        starts_ci((const char *)content_type, "application/octet-stream") ||
        starts_ci((const char *)content_type, "application/pdf"))) {
        serial_write("This response is not a text/html or text/plain document.\n");
        return 0;
    }
    if (!content_type || starts_ci((const char *)content_type, "text/html")) {
        u16 used = html_to_text(body_buffer, body_length, text_buffer, sizeof(text_buffer));
        if (!used) kstrlcpy(text_buffer, "(The page contains no readable text.)", sizeof(text_buffer));
        else if (used >= sizeof(text_buffer) - 4u) truncated = 1;
    } else if (starts_ci((const char *)content_type, "text/plain")) {
        u16 n = body_length, used = 0;
        if (n >= sizeof(text_buffer)) {
            n = sizeof(text_buffer) - 1u;
            truncated = 1;
        }
        for (u16 i = 0; i < n; ++i) {
            u8 c = body_buffer[i];
            if (c == '\r') continue;
            if (c == '\n') text_buffer[used++] = (char)c;
            else if (c == '\t') text_buffer[used++] = ' ';
            else if (c >= 32u && c != 127u) text_buffer[used++] = (char)c;
        }
        text_buffer[used] = 0;
    } else {
        serial_write("Unsupported content type for text rendering.\n");
        return 0;
    }
    if (body_length >= HTTP_BODY_CAP) truncated = 1;
    {
        char title[HTTP_HOST_CAP + 48u];
        (void)ksnprintf(title, sizeof(title), "HTTP %u | %s://%s%s",
                        (unsigned)status, url.secure ? "https" : "http", url.host,
                        status >= 400 ? " | SERVER ERROR" : "");
#ifndef DENOS_HTTP_TEST_MODE
        web_view_page(title, text_buffer, truncated);
#else
        serial_write(title);
        serial_putc('\n');
        print_wrapped(text_buffer);
        if (truncated) serial_write("\n[Page incomplete: response ended early or exceeded the browser limit.]\n");
#endif
    }
    return status >= 400 ? 1 : 0;
}

#ifndef DENOS_HTTP_TEST_MODE
static int web_history_push(const char *url, u16 *history_count, u16 *history_index) {
    if (!url || !history_count || !history_index) return -1;
    if (*history_index + 1u < *history_count)
        *history_count = (u16)(*history_index + 1u);
    if (*history_count >= WEB_HISTORY_CAP) {
        for (u16 i = 1; i < WEB_HISTORY_CAP; ++i)
            (void)copy_bounded(web_history[i - 1u], sizeof(web_history[i - 1u]), web_history[i]);
        --*history_count;
        if (*history_index) --*history_index;
    }
    if (copy_bounded(web_history[*history_count], sizeof(web_history[*history_count]), url) < 0)
        return -1;
    *history_index = *history_count;
    ++*history_count;
    return 0;
}
#endif

int net_http_get(const char *url) {
    if (!url || !*url) { serial_write("usage: web https://example.com/  (or http:// for unencrypted sites)\n"); return -1; }
#ifdef DENOS_HTTP_TEST_MODE
    return http_get_internal(url, 0);
#else
    u16 history_count = 1, history_index = 0;
    int result;
    web_go_url[0] = 0;
    if (copy_bounded(web_history[0], sizeof(web_history[0]), url) < 0) {
        serial_write("web: URL is too long.\n"); return -1;
    }
    for (;;) {
        web_selected_link = -1;
        web_back_requested = 0;
        web_forward_requested = 0;
        web_go_requested = 0;
        web_exit_requested = 0;
        result = http_get_internal(web_history[history_index], 0);
        if (web_exit_requested) return result;
        if (web_back_requested) {
            if (history_index > 0) { --history_index; continue; }
        }
        if (web_forward_requested) {
            if (history_index + 1u < history_count) { ++history_index; continue; }
        }
        if (web_go_requested) {
            if (web_history_push(web_go_url, &history_count, &history_index) < 0) {
                serial_write("web: could not add URL to history.\n"); return result;
            }
            continue;
        }
        if (web_selected_link >= 0 && (u16)web_selected_link < web_link_count) {
            const char *next = web_links[web_selected_link].url;
            if (web_history_push(next, &history_count, &history_index) < 0) {
                serial_write("web: link URL is too long for browser history.\n"); return result;
            }
            continue;
        }
        return result;
    }
#endif
}
