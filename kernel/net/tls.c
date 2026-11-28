#include "net.h"
#include "kstring.h"
#include "kformat.h"
#include "serial.h"

#ifdef DENOS_ENABLE_TLS
#include <bearssl.h>
#include "cpu.h"
#include "rtc.h"

#define TLS_RANDOM_BYTES 32u
#define TLS_HOSTNAME_MAX 127u
#define TLS_IO_TIMEOUT_TICKS 600u
#define TLS_TCP_WRITE_CHUNK 1024u

/* brssl emits a self-contained C trust-anchor declaration (usually static).
 * Include it here so those declarations are visible to this translation unit. */
#ifndef DENOS_TLS_ROOTS_HEADER
#error "TLS build must provide DENOS_TLS_ROOTS_HEADER"
#endif
#include DENOS_TLS_ROOTS_HEADER

static br_ssl_client_context tls_client;
static br_x509_minimal_context tls_x509;
static br_sslio_context tls_io;
static unsigned char tls_buffer[BR_SSL_BUFSIZE_BIDI];
static int tls_prepared;
static u32 tls_io_timeout_ticks = TLS_IO_TIMEOUT_TICKS;
static const char *tls_transport_issue;

static const uint16_t tls_suites[] = {
    BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
    BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
    BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
    BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256
};

static uint32_t days_before_year(uint32_t year) {
    /* Gregorian days from 0000-01-01 through the day before year. */
    return 365u * year + (year + 3u) / 4u - (year + 99u) / 100u +
           (year + 399u) / 400u;
}

static int rtc_to_bearssl_time(const rtc_datetime_t *dt,
                               uint32_t *days, uint32_t *seconds) {
    static const uint8_t month_days[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    uint32_t total;
    if (!dt || !days || !seconds || dt->year < 1970u || dt->month < 1u ||
        dt->month > 12u || dt->day < 1u || dt->hour > 23u ||
        dt->minute > 59u || dt->second > 59u) return -1;
    uint32_t mdays = month_days[dt->month - 1u];
    if (dt->month == 2u && (dt->year % 4u == 0u) &&
        ((dt->year % 100u != 0u) || (dt->year % 400u == 0u))) mdays = 29u;
    if (dt->day > mdays) return -1;
    total = days_before_year(dt->year);
    for (uint32_t month = 1u; month < dt->month; ++month) {
        total += month_days[month - 1u];
        if (month == 2u && (dt->year % 4u == 0u) &&
            ((dt->year % 100u != 0u) || (dt->year % 400u == 0u))) ++total;
    }
    total += (uint32_t)dt->day - 1u;
    *days = total;
    *seconds = (uint32_t)dt->hour * 3600u +
               (uint32_t)dt->minute * 60u + dt->second;
    return 0;
}

static int tls_transport_read(void *context, unsigned char *data, size_t length) {
    int got;
    (void)context;
    if (!data || !length) {
        tls_transport_issue = "invalid TLS receive buffer";
        return -1;
    }
    if (length > 8192u) length = 8192u;
    got = net_tcp_receive(data, (u16)length, tls_io_timeout_ticks);
    if (got > 0) {
        tls_transport_issue = 0;
        return got;
    }
    if (got == NET_TCP_IO_TIMEOUT)
        tls_transport_issue = "TCP receive timed out while waiting for TLS data";
    else if (got == NET_TCP_IO_RESET)
        tls_transport_issue = "TCP connection was reset by the peer";
    else if (got == 0)
        tls_transport_issue = "peer closed TCP without a TLS close_notify alert";
    else
        tls_transport_issue = "TCP receive failed or the connection is no longer established";
    return -1; /* BearSSL's br_sslio callbacks require -1 for transport failure. */
}

static int tls_transport_write(void *context, const unsigned char *data, size_t length) {
    int sent;
    (void)context;
    if (!data || !length) {
        tls_transport_issue = "invalid TLS transmit buffer";
        return -1;
    }
    if (length > TLS_TCP_WRITE_CHUNK) length = TLS_TCP_WRITE_CHUNK;
    sent = net_tcp_send(data, (u16)length, tls_io_timeout_ticks);
    if (sent > 0) {
        tls_transport_issue = 0;
        return sent;
    }
    if (sent == -5)
        tls_transport_issue = "TCP data was not acknowledged after retransmission";
    else if (sent == -4)
        tls_transport_issue = "TCP connection was reset or closed while sending";
    else if (sent == -3)
        tls_transport_issue = "Ethernet/IP transmit failed while sending TCP data";
    else if (sent == -2)
        tls_transport_issue = "TCP peer receive window stayed closed";
    else
        tls_transport_issue = "TCP send failed or the connection is not established";
    return -1;
}

static void tls_wipe(void *buffer, size_t length) {
    volatile unsigned char *p = (volatile unsigned char *)buffer;
    while (length--) *p++ = 0;
}

static int tls_report_error(const char *operation) {
    unsigned error = (unsigned)br_ssl_engine_last_error(&tls_client.eng);
    char code[16];
    (void)ksnprintf(code, sizeof(code), "%u", error);
    serial_write("TLS: ");
    serial_write(operation);
    serial_write(" failed (BearSSL error ");
    serial_write(code);
    serial_write("). Connection aborted; verification is never bypassed.\n");
    if (error == BR_ERR_IO && tls_transport_issue) {
        serial_write("TLS transport detail: ");
        serial_write(tls_transport_issue);
        serial_write(".\n");
    }
    /* -1 means BearSSL's BR_ERR_IO; -2 means a TLS protocol/record/verify error. */
    return error == BR_ERR_IO ? -1 : -2;
}

int net_tls_available(void) {
    return 1;
}

int net_tls_connect(const char *hostname) {
    unsigned char random_seed[TLS_RANDOM_BYTES];
    rtc_datetime_t now;
    uint32_t days, seconds;
    if (!hostname || !*hostname || kstrlen(hostname) > TLS_HOSTNAME_MAX) return -1;
    if (tls_prepared) net_tls_close(20u);
    tls_io_timeout_ticks = TLS_IO_TIMEOUT_TICKS;
    tls_transport_issue = 0;
    if (TAs_NUM == 0u) {
        serial_write("TLS: no trusted root certificates were built in.\n");
        return -2;
    }
    if (cpu_random_bytes(random_seed, sizeof(random_seed)) < 0) {
        serial_write("TLS: this CPU has no working RDRAND source; refusing insecure randomness.\n");
        return -3;
    }
    if (rtc_read(&now) < 0 || rtc_to_bearssl_time(&now, &days, &seconds) < 0) {
        tls_wipe(random_seed, sizeof(random_seed));
        serial_write("TLS: system clock is invalid; set the hardware clock before HTTPS.\n");
        return -4;
    }

    br_ssl_client_init_full(&tls_client, &tls_x509, TAs, TAs_NUM);
    br_ssl_engine_set_versions(&tls_client.eng, BR_TLS12, BR_TLS12);
    br_ssl_engine_set_suites(&tls_client.eng, tls_suites,
                             sizeof(tls_suites) / sizeof(tls_suites[0]));
    br_ssl_engine_set_all_flags(&tls_client.eng, BR_OPT_NO_RENEGOTIATION);
    br_ssl_engine_set_buffer(&tls_client.eng, tls_buffer, sizeof(tls_buffer), 1);
    br_x509_minimal_set_time(&tls_x509, days, seconds);
    br_ssl_engine_inject_entropy(&tls_client.eng, random_seed, sizeof(random_seed));
    tls_wipe(random_seed, sizeof(random_seed));
    br_sslio_init(&tls_io, &tls_client.eng,
                  tls_transport_read, NULL, tls_transport_write, NULL);

    /* BearSSL copies the name for SNI and checks it against certificate SAN/CN. */
    if (!br_ssl_client_reset(&tls_client, hostname, 0)) {
        (void)tls_report_error("client initialisation");
        br_ssl_client_zero(&tls_client);
        tls_wipe(&tls_x509, sizeof(tls_x509));
        tls_wipe(tls_buffer, sizeof(tls_buffer));
        return -5;
    }
    tls_prepared = 1;
    return 0;
}

int net_tls_send(const void *data, u16 length, u32 timeout_ticks) {
    if (!tls_prepared || (!data && length)) return -1;
    tls_io_timeout_ticks = timeout_ticks ? timeout_ticks : TLS_IO_TIMEOUT_TICKS;
    tls_transport_issue = 0;
    if (br_sslio_write_all(&tls_io, data, length) < 0 ||
        br_sslio_flush(&tls_io) < 0)
        return tls_report_error("handshake or encrypted write");
    return (int)length;
}

int net_tls_receive(void *buffer, u16 capacity, u32 timeout_ticks) {
    int got;
    if (!tls_prepared || (!buffer && capacity)) return -1;
    if (!capacity) return 0;
    tls_io_timeout_ticks = timeout_ticks ? timeout_ticks : TLS_IO_TIMEOUT_TICKS;
    tls_transport_issue = 0;
    got = br_sslio_read(&tls_io, buffer, capacity);
    if (got >= 0) return got;
    if (br_ssl_engine_last_error(&tls_client.eng) == BR_ERR_OK) return 0;
    return tls_report_error("encrypted read");
}

void net_tls_close(u32 timeout_ticks) {
    if (tls_prepared) {
        /* A peer may close without close_notify; do not mistake that for a
         * verified TLS shutdown. The HTTP layer separately marks such reads
         * as truncated. Always tear down the underlying TCP connection. */
        (void)br_sslio_close(&tls_io);
        br_ssl_client_zero(&tls_client);
        tls_wipe(&tls_x509, sizeof(tls_x509));
        tls_wipe(&tls_io, sizeof(tls_io));
        tls_wipe(tls_buffer, sizeof(tls_buffer));
        tls_prepared = 0;
    }
    net_tcp_close(timeout_ticks);
}

#else

int net_tls_available(void) { return 0; }
int net_tls_connect(const char *hostname) {
    (void)hostname;
    serial_write("HTTPS support is not present in this image. Rebuild with 'make build TLS=1'.\n");
    return -1;
}
int net_tls_send(const void *data, u16 length, u32 timeout_ticks) {
    (void)data; (void)length; (void)timeout_ticks; return -1;
}
int net_tls_receive(void *buffer, u16 capacity, u32 timeout_ticks) {
    (void)buffer; (void)capacity; (void)timeout_ticks; return -1;
}
void net_tls_close(u32 timeout_ticks) { net_tcp_close(timeout_ticks); }

#endif
