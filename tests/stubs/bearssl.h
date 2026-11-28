#ifndef DENOS_TEST_BEARSSL_H
#define DENOS_TEST_BEARSSL_H
#include <stddef.h>
#include <stdint.h>
#define BR_SSL_BUFSIZE_BIDI 32768u
#define BR_TLS12 0x0303u
#define BR_OPT_NO_RENEGOTIATION 0x00000002u
#define BR_ERR_OK 0
#define BR_ERR_IO 31
#define BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 0xC02Bu
#define BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 0xC02Fu
#define BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256 0xCCA9u
#define BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256 0xCCA8u

typedef struct br_ssl_engine_context { unsigned char opaque[8]; } br_ssl_engine_context;
typedef struct br_ssl_client_context { br_ssl_engine_context eng; unsigned char opaque[8]; } br_ssl_client_context;
typedef struct br_x509_minimal_context { unsigned char opaque[8]; } br_x509_minimal_context;
typedef struct br_x509_trust_anchor { unsigned char opaque[8]; } br_x509_trust_anchor;
typedef struct br_sslio_context { unsigned char opaque[8]; } br_sslio_context;

void br_ssl_client_init_full(br_ssl_client_context *, br_x509_minimal_context *, const br_x509_trust_anchor *, size_t);
void br_ssl_engine_set_versions(br_ssl_engine_context *, unsigned, unsigned);
void br_ssl_engine_set_suites(br_ssl_engine_context *, const uint16_t *, size_t);
void br_ssl_engine_set_all_flags(br_ssl_engine_context *, uint32_t);
void br_ssl_engine_set_buffer(br_ssl_engine_context *, void *, size_t, int);
void br_x509_minimal_set_time(br_x509_minimal_context *, uint32_t, uint32_t);
void br_ssl_engine_inject_entropy(br_ssl_engine_context *, const void *, size_t);
void br_sslio_init(br_sslio_context *, br_ssl_engine_context *,
                   int (*)(void *, unsigned char *, size_t), void *,
                   int (*)(void *, const unsigned char *, size_t), void *);
int br_ssl_client_reset(br_ssl_client_context *, const char *, int);
int br_ssl_engine_last_error(const br_ssl_engine_context *);
int br_sslio_write_all(br_sslio_context *, const void *, size_t);
int br_sslio_flush(br_sslio_context *);
int br_sslio_read(br_sslio_context *, void *, size_t);
int br_sslio_close(br_sslio_context *);
void br_ssl_client_zero(br_ssl_client_context *);
#endif
