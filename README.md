# DenOS 0.4.9 — CLI Edition

DenOS is an experimental 64-bit x86-64 operating system written in freestanding C and NASM. It boots into a keyboard-driven VGA text console. **No desktop GUI starts during normal boot.** The kernel graphics/framebuffer/mouse library remains in the build for graphics development and is accompanied by a substantial QOI validation corpus.

The current codebase includes a checked four-level x86-64 page-table manager that attaches to the active CR3, queries 4 KiB/2 MiB/1 GiB mappings, splits huge pages for 4 KiB edits, rejects implicit replacement, enforces W^X for new writable mappings when NX is available, and reclaims empty dynamically allocated page-table levels. The prior release also added early CPU exception diagnostics, a dedicated double-fault IST stack, and complete GPR preservation in the PIT IRQ path. It continues to focus on the command-line workflow: a more capable shell editor, Vim-style C suggestions, structural source checks, and practical file inspection tools. It includes Aiden C 5.2.4's AEX6 compiler/VM, memory/VFS/storage/services, networking, and graphics work.

DenOS is still a hobby OS project, not a complete modern general-purpose operating system or Linux replacement. Important architectural gaps are listed below without hiding them.


## 0.4.9 editor and compiler update

- Set the operating-system release identity to DenOS 0.4.9 and the independent Aiden C compiler version to 5.2.4.
- Expanded Vim-style code suggestions to 24 results, prioritized identifiers in the current buffer, added the remaining Aiden C VM built-ins, and kept the selected result visible while cycling.
- Added a floating VS Code-inspired suggestion dropdown with highlighted selection, kind icons and labels. `Up`/`Down` or `Ctrl-N`/`Ctrl-P` cycle; `Tab` accepts; `Esc` dismisses.
- Hardened DenFS persistence: failed disk writes roll back the in-memory file, failed file-data reads abort partial mounts, and `diskinfo` reports the RAM-fallback reason. QEMU now attaches the data image explicitly to the primary legacy IDE master.

## 9.5.2 bootstrap assembly fix (previous release)

Fixed NASM `.bss` alignment directives and the incorrectly scoped GDT pointer symbol. Run `make boot-asm-check` for the regression check, then `make` with NASM, GRUB rescue tools and Xorriso installed to assemble and link the bootable ISO.

## Previous release 9.5.0 highlights

- **Four-level virtual-memory manager:** `kernel/mm/paging.c` attaches to the existing bootstrap CR3 without replacing the identity map. It provides checked map, explicit remap, query, protect and unmap operations for 4 KiB pages; it recognizes and safely splits existing 2 MiB and 1 GiB mappings.
- **Permission and address checks:** validates canonical virtual addresses, physical alignment/address width, supported flags, NX/global-page capability, and writable+executable policy. Enables supervisor write-protect (CR0.WP), NXE where supported, and PGE where supported.
- **Page-table lifecycle:** allocates intermediate tables from PMM, rolls back newly-created empty tables on allocation failure, and reclaims empty dynamic page-table pages after unmap. Physical data frames remain caller-owned.
- **Kernel virtual allocator:** adds a 64 MiB high-half virtual window backed by individually allocated PMM frames. `kvalloc` returns contiguous virtual memory, `kvfree` verifies mappings and frame ownership, and failed rollback quarantines ranges instead of risking physical-frame aliasing. Existing `kmalloc` remains unchanged.
- **Diagnostics:** `paging` and `vmstat` report page-table allocation, mapping changes, failed operations, NX/WP/PGE protections, virtual allocator usage, and quarantined ranges.
- **New host coverage:** dedicated page-table and virtual-allocation tests exercise huge-page splitting, mapping replacement, permissions, high canonical addresses, OOM and partial-unmap rollback, quarantine, table reclamation, PMM ownership, and statistics.
- **Developer workflow:** `make paging-test` and `make vmalloc-test` run the memory suites alone; `make check-kernel-c` checks every kernel C translation unit using the freestanding warning-as-error flags; `make check` runs all host tests and the C check.

This is a single-CR3 kernel mapping manager, not process isolation. The kernel still has no separate address spaces, demand paging, swap, TLB shootdowns, SMP page-table locking, or protected ring-3 execution. Do not use `VMM_USER` as a security boundary until those facilities exist. See [`docs/VIRTUAL-MEMORY.md`](docs/VIRTUAL-MEMORY.md).

## 9.4.1 highlights

- **CPU fault diagnostics:** IDT vectors 0–31 are installed immediately on entry to `kernel_main`, before console initialization. Fault reports include the vector, error code, instruction/stack/frame pointers, flags, general-purpose registers, and page-fault CR2/access decoding. Double fault uses a dedicated 16 KiB TSS IST1 stack; PIT IRQ0 uses a separate 16 KiB IST2 stack.
- **Safer timer IRQ:** the PIT interrupt stub preserves every GPR and x87/SSE state before calling C. Frame pointers are retained in kernel C to support a bounded stack trace. See `docs/EXCEPTIONS-AND-PANICS.md`.

## 9.4.0 highlights

- **Vim-style C completion:** `.c` and `.h` buffers get a floating VS Code-inspired suggestion list with highlighted selection, kind icons and labels. `Up`/`Down` or `Ctrl-N`/`Ctrl-P` cycle; `Tab` accepts; `Esc` dismisses. The bounded collector includes keywords, types, Aiden C built-ins and same-file identifiers while skipping comments and quoted literals.
- **Editor checks:** `:check` detects unbalanced parentheses/brackets/braces, unterminated block comments, and unterminated string/character literals. It is a structural check, not a C parser.
- **Better shell input:** in-line editing with left/right, Home/End, Delete/Backspace, `Ctrl-A`, `Ctrl-E`, `Ctrl-U`, `Ctrl-K`, Up/Down history, and Tab completion for built-in commands/current-directory entries.
- **New CLI tools:** `grep`, `wc`, `head`, `tail`, `stat`, `hexdump`, and bounded recursive `tree`.
- **Aiden C preserved:** AEX6 compiler/VM, global data, scalar pointers, functions/recursion, switch/enums, scalar typedefs, object-like macros, C-string helpers, and existing sample applications remain intact. Three more Aiden C examples exercise the sieve, sorting, and number-theory features.
- **Graphics library preserved:** software renderer, framebuffer presentation, UI font, mouse/window helpers, polygons, text rendering, scaled/alpha image operations, and QOI decoder remain kernel components. They are not started on default boot.
- **Large graphics validation corpus:** ten deterministic 1600×900 QOI test scenes with exact RGBA golden surfaces. `make qoi-fixtures-test` decodes all ten images and compares every RGB pixel with the reference. The raw golden surfaces are real test data, not archive padding.
- **Separate QEMU window:** `make run` uses `-display gtk` by default; set `QEMU_DISPLAY=sdl` to use SDL where available.

## Build and run

On Debian/Ubuntu, install the host tools:

```sh
sudo apt update
sudo apt install build-essential nasm xorriso grub-pc-bin grub-common qemu-system-x86
```

Build the kernel and ISO:

```sh
make clean
make
```

Run in a separate QEMU window:

```sh
make run
```

Choose SDL:

```sh
make run QEMU_DISPLAY=sdl
```

The project creates `denos-data.img` on first build. `make run` attaches it as the primary IDE master because the current ATA driver uses legacy IDE PIO (AHCI/SATA and NVMe are not yet supported). `make clean` preserves this persistent disk image. `make clean-data` deletes data images; back them up before using it. Serial diagnostics are written to `build/serial.log`. If the kernel reports RAM fallback, run `diskinfo` and `bootlog` to see the exact ATA/mount error.

## Shell quick reference

| Command | Purpose |
|---|---|
| `help` / `help editor` / `help programming` | Command and subsystem help |
| `ls [PATH]`, `cd PATH`, `pwd`, `tree [PATH]` | Navigate directories |
| `cat FILE`, `edit FILE`, `write FILE TEXT` | Read, edit, and write text files |
| `cp SRC DST`, `mv SRC DST`, `rm FILE` | File operations |
| `grep PATTERN FILE`, `wc FILE` | Search and count text |
| `head [-n COUNT] FILE`, `tail [-n COUNT] FILE` | Preview file content |
| `stat PATH`, `hexdump FILE` | Inspect stored file metadata/content |
| `cc SOURCE.c [-o PROGRAM]`, `run PROGRAM`, `cinfo` | Compile and run Aiden C bytecode |
| `sysinfo`, `cpuinfo`, `uptime`, `free`, `pmm`, `paging`, `vmstat`, `tasks` | Inspect kernel and virtual-memory state |
| `devices`, `bootlog`, `df`, `diskinfo`, `partitions`, `mounts` | Inspect hardware and storage |
| `services`, `service start|stop NAME`, `ipc`, `events` | Inspect kernel services |
| `net`, `ping IPv4` | Inspect/test experimental networking |
| `sha256 FILE`, `crc32 FILE`, `uuid`, `random` | Checksums and identifiers |

This shell is not POSIX. It does not yet implement pipelines, redirection, job control, shell scripts, globbing, or process groups. Current file tools operate on the NUL-terminated text-file abstraction, not arbitrary binary files. See [`docs/CLI-TOOLS.md`](docs/CLI-TOOLS.md).

## Editing C with Vim-style suggestions

```text
edit main.c
```

Inside a C file:

- Enter insert mode with `i`.
- Type at least two letters of a keyword, type, built-in, or identifier already used in the file.
- A floating VS Code-inspired suggestion popup appears; `Up`/`Down` or `Ctrl-N`/`Ctrl-P` change selection, `Tab` accepts, and `Esc` dismisses the popup.
- Use `:set nocomplete` to turn suggestions off and `:set complete` to restore them.
- Use `:check` for a quick delimiter/comment/string-balance check.

The suggestion provider is small, bounded, and buffer-local. It is not a semantic language server and does not load include files or offer full compiler diagnostics. Full key reference: [`docs/EDITOR.md`](docs/EDITOR.md).

## Aiden C

Aiden C is DenOS's freestanding C-subset compiler. It emits AEX6 bytecode, which executes inside the kernel VM. It is not GCC, does not emit native x86-64 machine code, and does not provide process isolation. Current language limits, VM types, runtime functions, and unsupported syntax are listed in [`docs/AIDEN-C.md`](docs/AIDEN-C.md).

## Graphics library and test resources

The graphics API is in `kernel/include/gfx.h`; implementation is `kernel/drivers/graphics.c`. The QOI decoder is in `kernel/lib/qoi.c`. The deterministic test/resource corpus is in `resources/graphics-lab/fixtures/`; each `.qoi` input is paired with an exact `.rgba` golden reference. Regenerate all ten with:

```sh
cc -O3 -std=c11 tools/generate_gfx_fixtures.c -lm -o /tmp/gfx-fixtures
/tmp/gfx-fixtures resources/graphics-lab/fixtures
make qoi-fixtures-test
```

The fixtures are developer test data and are not loaded into the boot filesystem or kernel memory during normal startup.

## Tests and verification

Run all host-side regressions (including exception decoder coverage):

```sh
make test
```

This release includes **31 host test executables**, including all ten high-resolution QOI reference fixtures. Compile every freestanding kernel C translation unit with the production flags by building the corresponding `build/kernel/...o` targets or running `make` (which also assembles and links the boot image).

Host tests and object compilation do not replace a real ISO build, QEMU boot, keyboard interaction test, or hardware network smoke test. Boot validation requires NASM, GRUB utilities, Xorriso, and QEMU on the host machine.

## Project layout

```text
boot/                    GRUB configuration
kernel/arch/x86_64/      x86-64 entry and CPU helpers
kernel/core/             boot arguments, logs, clock, entropy
kernel/drivers/          ATA, PCI, E1000, serial, graphics, mouse
kernel/editor/           source completion engine
kernel/exec/             ELF64 structural validation helpers
kernel/fs/               DenFS and VFS
kernel/include/           kernel API headers
kernel/lib/               strings, formatting, crypto, QOI, parsers
kernel/mm/                heap and physical memory manager
kernel/net.c              experimental ARP/IPv4/ICMP networking
kernel/sys/               services, scheduler, IPC, events, devices
kernel/compiler.c         Aiden C to AEX6 compiler
kernel/vm.c               AEX1–AEX6 virtual machine
userland/apps/            bundled Aiden C samples
resources/graphics-lab/   deterministic QOI/RGBA rendering corpus
tests/                    host regression tests
tools/                    developer fixture generator
docs/                     subsystem, language, editor, and CLI manuals
```

## Important architectural gaps

- No ring-3 user processes, protected address spaces, syscall ABI, process isolation, or preemptive scheduling; applications execute in kernel context.
- Cooperative scheduler only; no SMP support.
- Aiden C is a bounded non-ISO subset with a VM-specific single-cell type model; no native compiler backend or full standard library.
- DenFS is a small fixed-slot filesystem over legacy ATA PIO, not a journaling filesystem. No AHCI/NVMe/USB storage stack.
- Networking is experimental static QEMU E1000 + ARP + IPv4 + ICMP. No DHCP, DNS, TCP/UDP sockets, IPv6, Wi-Fi or general routing.
- No POSIX shell, user/permission model, service manager isolation, package manager, UEFI boot path, sound stack, or modern power management.
- Graphics code is present, but no GUI or userspace graphics ABI is started by normal boot.

DenOS is a serious learning project. The next major milestone is a protected user-mode process model and versioned syscall boundary—not claiming completeness before those foundations exist.
