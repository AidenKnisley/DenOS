# DenOS 0.3.1 — standalone x86-64 OS with an experimental C environment

DenOS is an independent x86-64 operating-system project written in C and NASM. It does not use the Linux kernel. It includes an in-kernel C-subset compiler, an executable bytecode virtual machine, a Vim-style editor, and a small persistent filesystem backed by a primary-master ATA/IDE hard disk.

## Build on Ubuntu/Debian

Install the host-side build dependencies:

```sh
sudo apt update
sudo apt install build-essential nasm grub-pc-bin grub-common xorriso mtools qemu-system-x86 python3
```

The Makefile exposes three build/run commands and a separate host-side test target:

```sh
make clean
make
make run
```

`make` is the default target and creates `build/denos.iso`. It also creates `denos-data.img`, an 8 MiB virtual hard disk, the first time it is needed. `make run` builds the ISO if needed and starts QEMU with that disk attached as the primary IDE master. The graphical QEMU window receives keyboard input; click it before typing. Exit QEMU using the window close control.

**Your files survive reboot while the data image is kept.** The `denos-data.img` file is your DenOS hard drive. `make clean` removes build output and both supported data-image filenames (`denos-data.img` and `denos-data.vdi`), so copy the image somewhere safe before cleaning when you want to preserve files. The next build creates a blank data image if one is not present. On the first boot, DenOS formats the blank image automatically. The filesystem has 48 fixed-size file slots, each storing up to 16,383 bytes of text. It uses a RAM cache, but every file update is written to the disk image before the command reports success. The kernel uses legacy ATA PIO, so the QEMU machine is configured as `pc` (with an IDE controller). The ATA driver probes master and slave devices on both the primary and secondary legacy IDE channels with `IDENTIFY DEVICE`, requires LBA support, reads the reported sector capacity and model string, and rejects out-of-range sector requests. `make run` attaches the data image explicitly as `ide.0`, unit 0 (the primary IDE master).

The x86-64 entry code enables the x87/SSE processor state before entering C code. Because optimized kernel code may use SSE2 instructions, the timer interrupt saves and restores the FPU/SSE state before calling its C handler. If you are updating from an earlier DenOS 0.3 source tree, remove only the build output with `rm -rf build` and then run `make run`. Do not use `make clean` unless you intend to delete the persistent data image too.

## ATA storage diagnostics and safety

The ATA driver uses the IDENTIFY DEVICE command rather than guessing that a drive exists from its status register. It stores the model name and LBA28 sector count, and validates every read/write LBA against the detected capacity. The current driver probes master and slave devices on both legacy IDE channels and supports 512-byte sectors with LBA28 PIO; it does not yet support AHCI/SATA controllers, DMA, or 48-bit LBA.

On first boot, the filesystem formats a disk only when sector zero is completely zeroed or filled with `0xFF`. If the sector contains an unknown filesystem signature, DenOS leaves it untouched and starts a temporary RAM session instead. This avoids silently destroying a disk that may contain data from another filesystem or a damaged DenOS volume.

Use `diskinfo` (or `disk`) to inspect the detected drive and `df` to inspect the DenOS file-data pool. The 8 MiB QEMU image is larger than the filesystem's current fixed layout; the extra sectors are not currently allocated to files.

For a host-side regression test of the real ATA driver and filesystem code, run:

```sh
make test
```

This runs two host-side suites: mock IDE devices exercise all four primary/secondary master/slave positions, IDENTIFY, model/capacity parsing, sector reads and writes, flush handling, bounds checks, missing-drive detection, filesystem format/remount, and non-destructive handling of unknown disks; the compiler/VM suite checks compilation, execution, formatted output, `scanf` range handling, malformed comments, integer-literal bounds, and endless-loop termination. It does not replace a real QEMU boot test. For persistent storage, start the ISO through `make run`; booting `build/denos.iso` by itself does not attach `denos-data.img`, so DenOS correctly starts in temporary RAM mode. When using VirtualBox, attach `denos-data.img` as a second hard disk on an IDE controller; attaching only the ISO will not provide persistent storage.

## Compile and run C programs inside DenOS

After boot, the hard-disk filesystem is seeded with `hello.c`, `guess.c`, `calc.c`, `ttt.c`, and `life.c` if those files do not already exist. Your edits to the examples are not overwritten on later boots.

```text
dir
cc hello.c hello
run hello
cc guess.c guess
run guess
cc calc.c calc
run calc
cc ttt.c ttt
run ttt
cc life.c life
run life
```

`cc source.c program` (or `cc source.c -o program`) compiles the source stored in the DenOS filesystem and saves an AEX executable as `program.ax`. `run program` (or `./program`) loads and executes that compiled program in the AEX virtual machine. Input is line-based; `scanf`-style input is supported for `%d` and `%c`. Both source files and compiled executables are persistent when the ATA disk is available. The `df` command reports filesystem usage (used, free and total file-data space) and identifies whether the mounted volume is persistent ATA storage or a temporary RAM fallback. `neofetch` shows the same storage capacity and usage alongside the system details. The volume can store up to 786,384 bytes of file contents across 48 fixed slots; unused space inside an already allocated file slot is reserved and cannot be reused by another file. `df` reports used, reserved, free-slot, and total capacity separately. `disk` (also `diskinfo`) reports the physical ATA device model and raw capacity, while `df` reports the mounted filesystem's usable file-data pool.

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

Preprocessor include lines are ignored; they do not load a hosted C library. Decimal integer literals are limited to the signed 32-bit bytecode immediate range, while VM arithmetic and variables use 64-bit values. Floating point, user-defined functions, structs, pointer arithmetic, multidimensional arrays, array initializers, macros, and most of the standard library are not implemented. The calculator demo therefore uses integer arithmetic.

AEX files contain compact bytecode, not x86-64 native machine code. The kernel executes that bytecode in an interpreter. This is real compile-to-executable behavior inside DenOS, but it is not a ring-3 process: programs run in the kernel's execution context and do not have process isolation, memory protection, or a POSIX ABI. Do not run untrusted programs.

## Current architecture

- Multiboot2 BIOS boot path and x86-64 long-mode entry.
- VGA text output, COM1 debug mirroring, and polling PS/2 keyboard input.
- IDT/PIC/PIT timer and a bounded bootstrap heap.
- Fixed-capacity filesystem with ATA PIO hard-disk backing and RAM cache; bundled sources are installed only when missing.
- Interactive root-style shell, retro VGA startup screen, and system-information display.
- Full-screen Vim-style editor with C syntax highlighting and a blue title/status treatment.
- Aiden C compiler (`cc`) and AEX bytecode virtual machine (`run program`).

## Still not implemented

- Ring-3 user mode, process scheduler, context switching, and process isolation.
- Native x86-64/ELF executable generation and loader.
- Full ISO C compiler, libc, general-purpose `scanf`/`printf`, floating point, and user-defined C functions.
- Physical page allocator and general virtual-memory manager.
- General-purpose filesystem features (directories, variable-size allocation, journaling), networking, USB, sound, and UEFI boot.
- Full Vim-compatible editor, shell scripting, pipes, and permissions.

The `toolchain/build-cross-gcc.sh` script is still a host-side cross-toolchain experiment. It is not the compiler used by the in-OS `cc` command.

## website

https://aidenknisley.github.io/denos-website/
