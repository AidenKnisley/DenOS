#!/usr/bin/env python3
"""Generate a freestanding C translation unit that seeds bundled app sources."""
from pathlib import Path
import sys


out = Path(sys.argv[1])
files = [Path(p) for p in sys.argv[2:]]

def c_string(data: bytes) -> str:
    chunks = []
    for byte in data:
        if byte == 0x22:
            chunks.append(r'\"')
        elif byte == 0x5c:
            chunks.append(r'\\')
        elif byte == 0x0a:
            chunks.append(r'\n')
        elif byte == 0x0d:
            chunks.append(r'\r')
        elif byte == 0x09:
            chunks.append(r'\t')
        elif 0x20 <= byte <= 0x7e:
            chunks.append(chr(byte))
        else:
            chunks.append("\\%03o" % byte)
    return '"' + ''.join(chunks) + '"'

declarations = ['#include "apps.h"', '#include "fs.h"']
install_calls = ['void apps_install(void) {']
for path in files:
    if not path.name.endswith('.c'):
        continue
    symbol = 'source_' + path.stem.replace('-', '_')
    declarations.append('static const char %s[] = %s;' % (symbol, c_string(path.read_bytes())))
    install_calls.append('    if (!fs_read("%s")) (void)fs_write("%s", %s);' % (path.name, path.name, symbol))
install_calls.append('}')
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text('\n'.join(declarations + install_calls) + '\n')
