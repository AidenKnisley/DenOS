# DenOS 0.3 — standalone x86-64 OS with an experimental C environment

DenOS is an independent x86-64 operating-system project written in C and NASM. It does not use the Linux kernel. It includes an in-kernel C-subset compiler, an executable bytecode virtual machine, a Vim-style editor, and a small persistent filesystem backed by a primary-master ATA/IDE hard disk.

## Build on Ubuntu/Debian

Install the host-side build dependencies:

```sh
sudo apt update
sudo apt install build-essential nasm grub-pc-bin grub-common xorriso mtools qemu-system-x86 python3
```

The Makefile exposes three user-facing commands:

```sh
make clean
make
make run
```

`make` is the default target and creates `build/denos.iso`. It also creates `denos-data.img`, an 8 MiB virtual hard disk, the first time it is needed. `make run` builds the ISO if needed and starts QEMU with that disk attached as the primary IDE master. The graphical QEMU window receives keyboard input; click it before typing. Exit QEMU using the window close control.

**Your files survive reboot.** The `denos-data.img` file is your DenOS hard drive. `make clean` removes build output but deliberately keeps this disk image. Do not delete it unless you want to erase all DenOS files. On the first boot, DenOS formats the blank image automatically. The filesystem has fixed capacity: 48 files, up to 16 KiB per file. It uses a RAM cache, but every file update is written to the disk image before the command reports success. The kernel uses legacy ATA PIO, so the QEMU machine is configured as `pc` (with an IDE controller).

The x86-64 entry code enables the x87/SSE processor state before entering C code. This is required because the optimized kernel build may use SSE2 instructions when clearing VM memory. If you are updating from an earlier DenOS 0.3 image, run `make clean` before `make run` so the boot image is rebuilt with the corrected CPU initialization.

## Compile and run C programs inside DenOS

After boot, the hard-disk filesystem is seeded with `hello.c`, `guess.c`, `calc.c`, `ttt.c`, and `life.c` if those files do not already exist. Your edits to the examples are not overwritten on later boots.

```text
ls
cc hello.c -o hello
./hello
cc guess.c -o guess
./guess
cc calc.c -o calc
./calc
cc ttt.c -o ttt
./ttt
cc life.c -o life
./life
```

`cc source.c -o program` compiles the source stored in the DenOS filesystem and saves an AEX executable as `program.ax`. `./program` loads and executes that compiled program in the AEX virtual machine. Input is line-based; `scanf`-style input is supported for `%d` and `%c`. Both source files and compiled executables are persistent when the ATA disk is available. The `df` command reports whether storage is using the ATA disk or the temporary RAM fallback.

## Vim-style editor

The built-in editor is a full-screen, keyboard-driven Vim-style editor with C syntax highlighting. A visible block cursor shows the insertion position, while a `>` gutter marker and blue current-line highlight show which source line is active. The status bar also shows the exact line and column. It supports normal, insert, and Ex-command modes; line numbers; arrow keys and `h/j/k/l`; search; one-step undo/redo; line yank/delete/paste; and the common save/quit commands.

```text
vim hello.c          open or create a C source file
```

Common keys:

| Key | Action |
|---|---|
| `i`, `a`, `I`, `A` | Enter insert mode at cursor, after cursor, first non-space, or end of line |
| `o`, `O` | Open a new line below or above |
| `Esc` | Return to normal mode |
| `h`, `j`, `k`, `l` or arrows | Move cursor |
| `0`, `$`, `w`, `b`, `gg`, `G` | Start/end of line, word movement, first/last line |
| `x`, `dd`, `D`, `J` | Delete character, line, to end of line, or join lines |
| `yy`, `p`, `P` | Yank line and paste below/above |
| `u`, `Ctrl-R` | Undo and redo the last change |
| `/text`, `?text`, `n`, `N` | Search forward/backward and repeat |
| `:w`, `:q`, `:q!`, `:wq`, `:x` | Save, quit, discard, save-and-quit |
| `:set number`, `:set nonumber` | Toggle line numbers |
| `:e filename` | Open another file (only with no unsaved changes) |

C highlighting recognizes preprocessor lines, comments, strings, character constants, numbers, common C keywords, types, and function names. The editor uses the PS/2 keyboard and VGA text screen; click the QEMU window before typing. This is a practical Vim-style subset, not a complete Vim implementation (for example, visual mode, macros, split windows, and full vi compatibility are not implemented yet).

## Aiden C language subset — important limitations

This is a small first-stage compiler, not GCC or a complete ISO C compiler. It accepts one `int main(void)` or `int main()` function; local `int`/`char`/`long`-style scalar variables; fixed-size one-dimensional arrays; integer and character constants; arithmetic, comparison and logical expressions; assignment; `if`/`else`; `while`; `for`; and `return`.

The bundled programs use these built-ins:

- `printf`: `%d`, `%i`, `%c`, `%s`, and `%%`
- `puts`, `putchar`, `getchar`
- `scanf`: `%d` and `%c` with scalar variable addresses such as `&value`

Preprocessor include lines are ignored; they do not load a hosted C library. Floating point, user-defined functions, structs, pointer arithmetic, multidimensional arrays, array initializers, macros, and most of the standard library are not implemented. The calculator demo therefore uses integer arithmetic.

AEX files contain compact bytecode, not x86-64 native machine code. The kernel executes that bytecode in an interpreter. This is real compile-to-executable behavior inside DenOS, but it is not a ring-3 process: programs run in the kernel's execution context and do not have process isolation, memory protection, or a POSIX ABI. Do not run untrusted programs.

## Current architecture

- Multiboot2 BIOS boot path and x86-64 long-mode entry.
- VGA text output, COM1 debug mirroring, and polling PS/2 keyboard input.
- IDT/PIC/PIT timer and a bounded bootstrap heap.
- Fixed-capacity filesystem with ATA PIO hard-disk backing and RAM cache; bundled sources are installed only when missing.
- Interactive shell and full-screen Vim-style editor with C syntax highlighting.
- Aiden C compiler (`cc`) and AEX bytecode virtual machine (`./program`).

## Still not implemented

- Ring-3 user mode, process scheduler, context switching, and process isolation.
- Native x86-64/ELF executable generation and loader.
- Full ISO C compiler, libc, general-purpose `scanf`/`printf`, floating point, and user-defined C functions.
- Physical page allocator and general virtual-memory manager.
- General-purpose filesystem features (directories, variable-size allocation, journaling), networking, USB, sound, and UEFI boot.
- Full Vim-compatible editor, shell scripting, pipes, and permissions.

The `toolchain/build-cross-gcc.sh` script is still a host-side cross-toolchain experiment. It is not the compiler used by the in-OS `cc` command.
