#!/usr/bin/env python3
"""Regression-test generated Aiden sample installation and edit preservation."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''#include <stdio.h>
#include <string.h>
#include "fs.h"
#include "apps_embedded.c"

typedef struct entry {
    char path[256];
    char data[4096];
    int directory;
    int used;
} entry_t;

static entry_t entries[64];

static int find_entry(const char *path) {
    for (int i = 0; i < 64; ++i)
        if (entries[i].used && strcmp(entries[i].path, path) == 0) return i;
    return -1;
}

static int add_entry(const char *path, const char *data, int directory) {
    if (find_entry(path) >= 0) return -1;
    for (int i = 0; i < 64; ++i) {
        if (!entries[i].used) {
            entries[i].used = 1;
            entries[i].directory = directory;
            snprintf(entries[i].path, sizeof(entries[i].path), "%s", path);
            snprintf(entries[i].data, sizeof(entries[i].data), "%s", data ? data : "");
            return 0;
        }
    }
    return -1;
}

int fs_is_directory(const char *path) {
    int slot = find_entry(path);
    return slot >= 0 && entries[slot].directory;
}

int fs_mkdir(const char *path) { return add_entry(path, "", 1); }
int fs_exists(const char *path) { return find_entry(path) >= 0; }
int fs_write(const char *path, const char *data) { return add_entry(path, data, 0); }

static int expect_file(const char *path) {
    int slot = find_entry(path);
    return slot >= 0 && !entries[slot].directory && entries[slot].data[0] != '\0';
}

int main(void) {
    int slot;
    if (add_entry("home", "", 1) || add_entry("home/user", "", 1) ||
        add_entry("home/user/examples", "", 1)) return 10;
    if (add_entry("home/user/examples/hello.c", "USER EDITED THIS", 0)) return 11;

    apps_install();
    slot = find_entry("home/user/examples/hello.c");
    if (slot < 0 || strcmp(entries[slot].data, "USER EDITED THIS") != 0) return 12;
    if (!fs_is_directory("home/user/games") ||
        !fs_is_directory("home/user/utilities")) return 13;
    if (!expect_file("home/user/games/guess.c") ||
        !expect_file("home/user/utilities/calc.c")) return 14;

    slot = find_entry("home/user/games/guess.c");
    if (slot < 0) return 15;
    snprintf(entries[slot].data, sizeof(entries[slot].data), "%s", "LOCAL GAME EDIT");
    apps_install();
    if (strcmp(entries[slot].data, "LOCAL GAME EDIT") != 0) return 16;

    puts("embedded apps install tests passed");
    return 0;
}
'''


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="denos-app-embed-") as temporary:
        temp = Path(temporary)
        generated = temp / "apps_embedded.c"
        harness = temp / "harness.c"
        binary = temp / "apps-embed-test"
        include = temp / "include"
        include.mkdir()
        (include / "apps.h").write_text("void apps_install(void);\n")
        (include / "fs.h").write_text(
            "int fs_is_directory(const char *);\n"
            "int fs_mkdir(const char *);\n"
            "int fs_exists(const char *);\n"
            "int fs_write(const char *, const char *);\n"
        )
        sources = [
            ROOT / "userland/apps/examples/hello.c",
            ROOT / "userland/apps/games/guess.c",
            ROOT / "userland/apps/utilities/calc.c",
        ]
        subprocess.run(
            [sys.executable, str(ROOT / "tools/embed_apps.py"), str(generated),
             *(str(source) for source in sources)],
            cwd=ROOT, check=True,
        )
        source = generated.read_text()
        required = (
            'fs_write("home/user/examples/hello.c"',
            'fs_write("home/user/games/guess.c"',
            'fs_write("home/user/utilities/calc.c"',
            'fs_mkdir("home/user/examples")',
            'fs_mkdir("home/user/games")',
            'fs_mkdir("home/user/utilities")',
        )
        for fragment in required:
            if fragment not in source:
                raise SystemExit(f"app embedding test: missing {fragment}")
        if 'fs_write("hello.c"' in source or 'fs_write("guess.c"' in source:
            raise SystemExit("app embedding test: source category was flattened")

        harness.write_text(HARNESS)
        subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-I", str(include), "-I", str(temp), str(harness), "-o", str(binary)],
            check=True,
        )
        subprocess.run([str(binary)], check=True)
    print("Aiden C app embedding tests passed: category paths, directory creation, and edit preservation.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
