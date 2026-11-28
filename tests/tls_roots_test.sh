#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/bearssl/build"
printf '%s\n' 'test CA bundle' > "$TMP/ca.pem"
cat > "$TMP/bearssl/build/brssl" <<'SCRIPT'
#!/usr/bin/env sh
[ "$1" = "ta" ] || exit 2
[ -s "$2" ] || exit 3
printf '%s\n' '#include <bearssl.h>' \
  'static const br_x509_trust_anchor TAs[] = { { { 0 } } };' \
  '#define TAs_NUM (sizeof(TAs) / sizeof(TAs[0]))'
SCRIPT
chmod +x "$TMP/bearssl/build/brssl"
"$ROOT/tools/generate_tls_roots.sh" "$TMP/bearssl" "$TMP/generated/roots.c" "$TMP/ca.pem"
grep -q 'static const br_x509_trust_anchor TAs' "$TMP/generated/roots.c"
grep -q 'TAs_NUM' "$TMP/generated/roots.c"
if "$ROOT/tools/generate_tls_roots.sh" "$TMP/bearssl" "$TMP/no-roots.c" "$TMP/missing-ca.pem" >/dev/null 2>&1; then
    echo 'Expected missing CA bundle to fail.' >&2
    exit 1
fi
echo 'TLS trust-root generator tests passed: bundle selection, generated anchors, and missing-bundle rejection.'
