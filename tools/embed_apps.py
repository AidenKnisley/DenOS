#!/usr/bin/env python3
"""Generate a freestanding installer for bundled Aiden C source applications.

Inputs under userland/apps/ keep their category in the installed home tree:
userland/apps/games/guess.c -> home/user/games/guess.c.
Existing files are deliberately left untouched so user edits survive boots.
"""
from pathlib import Path
import sys
import zlib


def c_string(data: bytes) -> str:
    """Return a C string literal with unambiguous, bounded byte escapes."""
    if b"\0" in data:
        raise ValueError("embedded NUL byte in C source")
    chunks: list[str] = []
    for byte in data:
        if byte == 0x22:
            chunks.append(r'\"')
        elif byte == 0x5C:
            chunks.append(r'\\')
        elif byte == 0x0A:
            chunks.append(r'\n')
        elif byte == 0x0D:
            chunks.append(r'\r')
        elif byte == 0x09:
            chunks.append(r'\t')
        elif 0x20 <= byte <= 0x7E:
            chunks.append(chr(byte))
        else:
            chunks.append("\\%03o" % byte)
    return '"' + "".join(chunks) + '"'


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print("usage: embed_apps.py OUTPUT.c SOURCE.c [SOURCE.c ...]", file=sys.stderr)
        return 2

    output = Path(argv[1])
    app_root = Path("userland/apps").resolve()
    source_files = [Path(item).resolve() for item in argv[2:]]
    declarations = ['#include "apps.h"', '#include "fs.h"', ""]
    install_calls = ["void apps_install(void) {"]
    directories: set[str] = set()
    installs: list[tuple[str, str]] = []

    for source in sorted(source_files, key=lambda item: item.as_posix()):
        if source.suffix != ".c":
            continue
        try:
            relative = source.relative_to(app_root)
        except ValueError:
            print(f"embed_apps.py: source must be under userland/apps/: {source}", file=sys.stderr)
            return 2
        if not relative.parts or any(part in ("", ".", "..") for part in relative.parts):
            print(f"embed_apps.py: invalid source path: {source}", file=sys.stderr)
            return 2

        destination = "home/user/" + relative.as_posix()
        parent = Path(destination).parent
        while parent.as_posix() not in (".", ""):
            directories.add(parent.as_posix())
            parent = parent.parent

        data = source.read_bytes()
        if len(data) >= 65535:
            print(f"embed_apps.py: source exceeds DenFS file limit: {source}", file=sys.stderr)
            return 2
        symbol_base = "source_" + "_".join(relative.parts).replace("-", "_").replace(".", "_")
        symbol = f"{symbol_base}_{zlib.crc32(relative.as_posix().encode()) & 0xFFFFFFFF:08x}"
        declarations.append(f"static const char {symbol}[] = {c_string(data)};")
        installs.append((destination, symbol))

    for directory in sorted(directories, key=lambda item: (item.count("/"), item)):
        install_calls.append(
            f'    if (!fs_is_directory("{directory}")) (void)fs_mkdir("{directory}");'
        )
    if directories:
        install_calls.append("")
    for destination, symbol in installs:
        install_calls.append(
            f'    if (!fs_exists("{destination}")) (void)fs_write("{destination}", {symbol});'
        )
    install_calls.append("}")

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(declarations + [""] + install_calls + [""]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
