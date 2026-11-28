# Third-party sources

DenOS can optionally build HTTPS support with BearSSL 0.6. The source is not
vendored in this repository; run `make tls-deps` to download the official
release archive over HTTPS, verify its SHA-256 checksum, and build the host
`brssl` trust-anchor generator. `make tls` then compiles the BearSSL library
freestanding into the TLS-enabled kernel and embeds trust anchors from the
build host's CA bundle.

BearSSL is distributed under the MIT license. Its license is included in the
upstream source archive and in the generated release's third-party notices.

BearSSL is compiled with its Unix and Win32 clock integrations disabled. DenOS
sets the X.509 validation time explicitly from its own UTC hardware RTC; this
avoids linking the host OS `time()` function into the freestanding kernel.
