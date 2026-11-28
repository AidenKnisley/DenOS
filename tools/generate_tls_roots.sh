#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: $0 BEARSSL_DIR OUTPUT_C_FILE CA_BUNDLE_OR_EMPTY" >&2
    exit 2
fi
BEARSSL_DIR="$1"
OUTPUT="$2"
CA_BUNDLE="$3"

if [[ -z "$CA_BUNDLE" ]]; then
    for candidate in \
        /etc/ssl/certs/ca-certificates.crt \
        /etc/pki/tls/certs/ca-bundle.crt \
        /etc/ssl/ca-bundle.pem; do
        if [[ -s "$candidate" ]]; then CA_BUNDLE="$candidate"; break; fi
    done
fi
if [[ -z "$CA_BUNDLE" || ! -s "$CA_BUNDLE" ]]; then
    echo "ERROR: no CA bundle found. Install ca-certificates or set TLS_CA_BUNDLE=/path/to/bundle.pem." >&2
    exit 1
fi
if [[ ! -x "$BEARSSL_DIR/build/brssl" ]]; then
    echo "ERROR: BearSSL brssl tool is missing. Run './tools/fetch_bearssl.sh $BEARSSL_DIR' first." >&2
    exit 1
fi
mkdir -p "$(dirname "$OUTPUT")"
TMP="$OUTPUT.tmp"
"$BEARSSL_DIR/build/brssl" ta "$CA_BUNDLE" > "$TMP"
if ! grep -q 'TAs' "$TMP" || ! grep -q 'TAs_NUM' "$TMP"; then
    rm -f "$TMP"
    echo "ERROR: brssl did not generate trust anchors." >&2
    exit 1
fi
mv "$TMP" "$OUTPUT"
echo "Generated TLS trust anchors from $CA_BUNDLE"
