#!/usr/bin/env bash
set -euo pipefail

if [[ $# -eq 0 ]]; then
    mapfile -t sources < <(find kernel -name '*.c' -type f | sort)
else
    sources=("$@")
fi

if [[ ${#sources[@]} -eq 0 ]]; then
    echo "stack usage check: no C sources supplied" >&2
    exit 1
fi

cc=${CC:-gcc}
read -r -a cflags <<< "${CFLAGS:--std=gnu11 -ffreestanding -fno-builtin -Wall -Wextra -Werror -O2 -Ikernel/include}"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

index=0
for source in "${sources[@]}"; do
    [[ -f "$source" ]] || { echo "stack usage check: missing source: $source" >&2; exit 1; }
    object="$tmp/$index.o"
    "$cc" "${cflags[@]}" -fstack-usage -c "$source" -o "$object"
    ((index += 1))
done

python3 - "$tmp" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
entries = []
unknown_dynamic = []
for path in root.glob("*.su"):
    for line in path.read_text(errors="replace").splitlines():
        columns = line.split("\t")
        if len(columns) < 3:
            continue
        try:
            size = int(columns[1])
        except ValueError:
            continue
        location = columns[0]
        kind = columns[2]
        entries.append((size, location, kind))
        if kind == "dynamic":
            unknown_dynamic.append(location)

if not entries:
    print("stack usage check: compiler produced no .su records", file=sys.stderr)
    raise SystemExit(1)

entries.sort(reverse=True)
print("Largest kernel C stack frames:")
for size, location, kind in entries[:10]:
    print(f"  {size:5d} bytes ({kind}): {location}")

limit = 8192
oversized = [entry for entry in entries if entry[0] > limit]
if unknown_dynamic:
    print("Unbounded dynamic stack frames:", file=sys.stderr)
    for location in unknown_dynamic:
        print(f"  {location}", file=sys.stderr)
if oversized:
    print(f"Stack frame exceeds {limit}-byte safety budget:", file=sys.stderr)
    for size, location, kind in oversized:
        print(f"  {size} bytes ({kind}): {location}", file=sys.stderr)
if unknown_dynamic or oversized:
    raise SystemExit(1)
print(f"Stack usage check passed: {len(entries)} frames, none above {limit} bytes.")
PY
