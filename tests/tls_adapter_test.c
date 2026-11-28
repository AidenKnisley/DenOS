#include "net.h"
#include "cpu.h"
#include "rtc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bearssl.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static int random_fails;
static int rtc_fails;
static int rtc_invalid_second;
static int reset_succeeds = 1;
static int write_succeeds = 1;
static int read_result = 7;
static int engine_error;
static int tcp_read_result = 7;
static int tcp_write_result = 0;
static int invoke_read_callback;
static int invoke_write_callback;
static u32 last_tcp_read_timeout;
static u32 last_tcp_write_timeout;
static int (*saved_read_cb)(void *, unsigned char *, size_t);
static void *saved_read_ctx;
static int (*saved_write_cb)(void *, const unsigned char *, size_t);
static void *saved_write_ctx;
static char serial_capture[2048];
static size_t serial_used;
static int reset_calls;
static int close_calls;
static unsigned version_min, version_max;
static uint32_t engine_flags;
static size_t suite_count, injected_entropy_length, tls_buffer_length;
static int buffer_bidi;
static uint32_t validation_days, validation_seconds;
static char last_hostname[128];

int cpu_random_bytes(void *buffer, usize length) {
    if (random_fails || (!buffer && length)) return -1;
    memset(buffer, 0xA5, length);
    return 0;
}

int rtc_read(rtc_datetime_t *result) {
    if (rtc_fails || !result) return -1;
    result->year = 2026;
    result->month = 11;
    result->day = 27;
    result->hour = 12;
    result->minute = 34;
    result->second = rtc_invalid_second ? 60u : 56u;
    return 0;
}

int net_tcp_send(const void *data, u16 length, u32 timeout_ticks) {
    (void)data;
    last_tcp_write_timeout = timeout_ticks;
    if (tcp_write_result < 0) return tcp_write_result;
    return (int)length;
}
int net_tcp_receive(void *buffer, u16 capacity, u32 timeout_ticks) {
    last_tcp_read_timeout = timeout_ticks;
    if (tcp_read_result < 0) return tcp_read_result;
    if (capacity && buffer) memset(buffer, 'x', capacity < 7u ? capacity : 7u);
    if (tcp_read_result == 0) return 0;
    return capacity < 7u ? capacity : 7u;
}
void net_tcp_close(u32 timeout_ticks) { (void)timeout_ticks; close_calls++; }
void serial_write(const char *text) {
    while (*text && serial_used + 1u < sizeof(serial_capture))
        serial_capture[serial_used++] = *text++;
    serial_capture[serial_used] = 0;
}

void br_ssl_client_init_full(br_ssl_client_context *sc,
                             br_x509_minimal_context *xc,
                             const br_x509_trust_anchor *tas, size_t tas_num) {
    (void)sc; (void)xc;
    CHECK(tas != NULL);
    CHECK(tas_num == 1u);
}
void br_ssl_engine_set_versions(br_ssl_engine_context *cc, unsigned min, unsigned max) {
    (void)cc; version_min = min; version_max = max;
}
void br_ssl_engine_set_suites(br_ssl_engine_context *cc, const uint16_t *suites, size_t count) {
    (void)cc;
    CHECK(suites != NULL);
    CHECK(count == 4u);
    suite_count = count;
    CHECK(suites[0] == BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256);
    CHECK(suites[1] == BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256);
    CHECK(suites[2] == BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256);
    CHECK(suites[3] == BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256);
}
void br_ssl_engine_set_all_flags(br_ssl_engine_context *cc, uint32_t flags) {
    (void)cc; engine_flags = flags;
}
void br_ssl_engine_set_buffer(br_ssl_engine_context *cc, void *buffer, size_t length, int bidi) {
    (void)cc;
    CHECK(buffer != NULL);
    tls_buffer_length = length;
    buffer_bidi = bidi;
}
void br_x509_minimal_set_time(br_x509_minimal_context *ctx, uint32_t days, uint32_t seconds) {
    (void)ctx; validation_days = days; validation_seconds = seconds;
}
void br_ssl_engine_inject_entropy(br_ssl_engine_context *cc, const void *seed, size_t length) {
    (void)cc;
    CHECK(seed != NULL);
    CHECK(length == 32u);
    injected_entropy_length = length;
}
void br_sslio_init(br_sslio_context *ioc, br_ssl_engine_context *cc,
                   int (*read_cb)(void *, unsigned char *, size_t), void *read_ctx,
                   int (*write_cb)(void *, const unsigned char *, size_t), void *write_ctx) {
    (void)ioc; (void)cc;
    CHECK(read_cb != NULL && write_cb != NULL);
    saved_read_cb = read_cb; saved_read_ctx = read_ctx;
    saved_write_cb = write_cb; saved_write_ctx = write_ctx;
}
int br_ssl_client_reset(br_ssl_client_context *sc, const char *hostname, int resume) {
    (void)sc;
    CHECK(resume == 0);
    reset_calls++;
    strncpy(last_hostname, hostname ? hostname : "", sizeof(last_hostname) - 1u);
    last_hostname[sizeof(last_hostname) - 1u] = 0;
    return reset_succeeds;
}
int br_ssl_engine_last_error(const br_ssl_engine_context *cc) { (void)cc; return engine_error; }
int br_sslio_write_all(br_sslio_context *ioc, const void *data, size_t length) {
    (void)ioc;
    CHECK(data != NULL || length == 0u);
    if (!write_succeeds) { engine_error = BR_ERR_IO; return -1; }
    if (invoke_write_callback && length) {
        int sent = saved_write_cb(saved_write_ctx, (const unsigned char *)data, length);
        if (sent < 0) { engine_error = BR_ERR_IO; return -1; }
    }
    return 0;
}
int br_sslio_flush(br_sslio_context *ioc) { (void)ioc; return write_succeeds ? 0 : -1; }
int br_sslio_read(br_sslio_context *ioc, void *data, size_t length) {
    (void)ioc;
    if (invoke_read_callback && length) {
        int got = saved_read_cb(saved_read_ctx, (unsigned char *)data, length);
        if (got < 0) { engine_error = BR_ERR_IO; return -1; }
        if (got == 0) { engine_error = BR_ERR_IO; return -1; }
        engine_error = BR_ERR_OK;
        return got;
    }
    return read_result;
}
int br_sslio_close(br_sslio_context *ioc) { (void)ioc; return 0; }
void br_ssl_client_zero(br_ssl_client_context *sc) { (void)sc; }

int main(void) {
    char response[16];

    CHECK(net_tls_available() == 1);
    CHECK(net_tls_connect("") < 0);

    random_fails = 1;
    CHECK(net_tls_connect("example.com") == -3);
    CHECK(reset_calls == 0);
    random_fails = 0;

    rtc_fails = 1;
    CHECK(net_tls_connect("example.com") == -4);
    CHECK(reset_calls == 0);
    rtc_fails = 0;

    rtc_invalid_second = 1;
    CHECK(net_tls_connect("example.com") == -4);
    CHECK(reset_calls == 0);
    rtc_invalid_second = 0;

    reset_succeeds = 0;
    CHECK(net_tls_connect("bad.example") == -5);
    CHECK(reset_calls == 1);
    reset_succeeds = 1;

    CHECK(net_tls_connect("example.com") == 0);
    CHECK(reset_calls == 2);
    CHECK(strcmp(last_hostname, "example.com") == 0);
    CHECK(version_min == BR_TLS12 && version_max == BR_TLS12);
    CHECK(engine_flags == BR_OPT_NO_RENEGOTIATION);
    CHECK(suite_count == 4u);
    CHECK(tls_buffer_length == BR_SSL_BUFSIZE_BIDI && buffer_bidi == 1);
    CHECK(injected_entropy_length == 32u);
    CHECK(validation_days > 0u);
    CHECK(validation_seconds == (12u * 3600u + 34u * 60u + 56u));

    CHECK(net_tls_send("hello", 5u, 1u) == 5);
    write_succeeds = 0;
    CHECK(net_tls_send("fail", 4u, 1u) < 0);
    write_succeeds = 1;

    CHECK(net_tls_receive(response, sizeof(response), 1u) == 7);
    read_result = -1;
    engine_error = BR_ERR_OK;
    CHECK(net_tls_receive(response, sizeof(response), 1u) == 0);
    engine_error = 42;
    CHECK(net_tls_receive(response, sizeof(response), 1u) == -2);

    /* Exercise the actual BearSSL callbacks and verify transport-specific
     * diagnostics for a TCP timeout instead of a generic error 31. */
    invoke_read_callback = 1;
    tcp_read_result = -2; /* NET_TCP_IO_TIMEOUT */
    serial_used = 0; serial_capture[0] = 0;
    CHECK(net_tls_receive(response, sizeof(response), 17u) == -1);
    CHECK(last_tcp_read_timeout == 17u);
    CHECK(strstr(serial_capture, "BearSSL error 31") != NULL);
    CHECK(strstr(serial_capture, "TCP receive timed out") != NULL);
    tcp_read_result = -3; /* NET_TCP_IO_RESET */
    serial_used = 0; serial_capture[0] = 0;
    CHECK(net_tls_receive(response, sizeof(response), 19u) == -1);
    CHECK(strstr(serial_capture, "TCP connection was reset") != NULL);
    tcp_read_result = 0; /* Bare TCP EOF is not a valid TLS close_notify. */
    serial_used = 0; serial_capture[0] = 0;
    CHECK(net_tls_receive(response, sizeof(response), 23u) == -1);
    CHECK(strstr(serial_capture, "without a TLS close_notify") != NULL);
    invoke_read_callback = 0;
    tcp_read_result = 7;

    invoke_write_callback = 1;
    tcp_write_result = -5; /* TCP retransmission/ACK timeout. */
    serial_used = 0; serial_capture[0] = 0;
    CHECK(net_tls_send("hello", 5u, 29u) == -1);
    CHECK(last_tcp_write_timeout == 29u);
    CHECK(strstr(serial_capture, "not acknowledged") != NULL);
    tcp_write_result = 0;
    invoke_write_callback = 0;

    net_tls_close(1u);
    CHECK(close_calls > 0);
    CHECK(net_tls_send("after close", 11u, 1u) < 0);

    puts("TLS adapter tests passed: fail-closed RNG/RTC, TLS 1.2-only suites, SNI hostname, trust anchor/time setup, encrypted I/O errors and teardown.");
    return 0;
}
