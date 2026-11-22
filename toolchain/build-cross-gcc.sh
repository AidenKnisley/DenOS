#!/usr/bin/env bash
# Builds a host-runnable x86_64-elf cross compiler for kernel development.
# This is NOT a native compiler running inside DenOS.
set -euo pipefail
PREFIX="${PREFIX:-$HOME/opt/cross}"
TARGET="x86_64-elf"
JOBS="${JOBS:-$(nproc)}"
mkdir -p "$HOME/src" "$PREFIX"
cd "$HOME/src"
if [ ! -d binutils ]; then git clone --depth 1 https://sourceware.org/git/binutils-gdb.git binutils; fi
if [ ! -d gcc ]; then git clone --depth 1 https://gcc.gnu.org/git/gcc.git gcc; fi
mkdir -p build-binutils build-gcc
cd build-binutils
../binutils/configure --target="$TARGET" --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
make -j"$JOBS"
make install
cd ../build-gcc
../gcc/configure --target="$TARGET" --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers --disable-hosted-libstdcxx
make all-gcc -j"$JOBS"
make install-gcc
printf '\nCross compiler installed in %s/bin\n' "$PREFIX"
printf 'Add %s/bin to PATH.\n' "$PREFIX"
