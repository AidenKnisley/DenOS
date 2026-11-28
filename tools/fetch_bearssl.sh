#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST_INPUT="${1:-third_party/BearSSL}"
if [[ "$DEST_INPUT" = /* ]]; then DEST="$DEST_INPUT"; else DEST="$ROOT/$DEST_INPUT"; fi
CACHE="$ROOT/third_party/.cache"
ARCHIVE="$CACHE/bearssl-0.6.tar.gz"
URL="https://bearssl.org/bearssl-0.6.tar.gz"
SHA256="6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14"

if [[ -f "$DEST/inc/bearssl.h" ]]; then
    echo "BearSSL source already present at $DEST"
else
    mkdir -p "$CACHE" "$ROOT/third_party"
    if [[ ! -f "$ARCHIVE" ]]; then
        echo "Downloading official BearSSL 0.6 source archive..."
        if command -v curl >/dev/null 2>&1; then
            curl --fail --location --proto '=https' --tlsv1.2 "$URL" -o "$ARCHIVE"
        elif command -v wget >/dev/null 2>&1; then
            wget --https-only --secure-protocol=TLSv1_2 "$URL" -O "$ARCHIVE"
        else
            echo "ERROR: install curl or wget to fetch BearSSL." >&2
            exit 1
        fi
    fi
    echo "$SHA256  $ARCHIVE" | sha256sum --check --status || {
        rm -f "$ARCHIVE"
        echo "ERROR: BearSSL archive SHA-256 mismatch; download removed." >&2
        exit 1
    }
    TMP="$(mktemp -d)"
    trap 'rm -rf "$TMP"' EXIT
    tar -xzf "$ARCHIVE" -C "$TMP"
    TOP="$(find "$TMP" -mindepth 1 -maxdepth 1 -type d -print -quit)"
    [[ -n "$TOP" && -f "$TOP/inc/bearssl.h" ]] || {
        echo "ERROR: archive layout did not contain inc/bearssl.h." >&2
        exit 1
    }
    DEST_PARENT="$(dirname "$DEST")"
    mkdir -p "$DEST_PARENT"
    STAGE="$(mktemp -d "$DEST_PARENT/.BearSSL.new.XXXXXX")"
    rmdir "$STAGE"
    cp -a "$TOP" "$STAGE"
    rm -rf "$DEST"
    mv "$STAGE" "$DEST"
    echo "Verified BearSSL 0.6 source installed at $DEST"
fi

# Build the host-side brssl utility used only to convert the system CA bundle
# into static X.509 trust-anchor declarations for this kernel build.
make -C "$DEST" build/brssl
