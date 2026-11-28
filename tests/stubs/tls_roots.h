/* Test-only dummy anchor; never used in a real DenOS TLS build. */
static const br_x509_trust_anchor TAs[] = { { { 0 } } };
#define TAs_NUM (sizeof(TAs) / sizeof(TAs[0]))
