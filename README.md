# DenOS 0.5.7 — Terminal Edition

DenOS is an experimental 64-bit x86-64 operating system written in freestanding C and NASM. It boots directly to the VGA text console and keyboard-driven shell. There is no desktop, window manager, mouse-driven interface, framebuffer mode request, or GUI boot path.

The shell includes native text-mode apps: `browse` is a keyboard file manager with text preview, `notes` opens the built-in editor on the quick-notes file, `calc` evaluates integer expressions, `monitor` displays live kernel/storage statistics, and `arcade` launches Snake, Breakout, and Maze Runner rendered with colored ASCII characters. Standard commands such as `ls`, `tree`, `grep`, `cp`, `mv`, and `edit` remain available.

DenOS also includes DenFS v4 with 255-character paths and migration from v1/v2/v3, the VFS namespace, persistent ATA storage, cooperative services, IPC, E1000 Ethernet with ARP/IPv4/ICMP/UDP, DHCP, DNS A/CNAME lookup, a bounded TCP client, a scrollable terminal HTTP/HTTPS browser with keyboard link navigation, back/forward history, URL entry, basic HTML/CSS visibility rendering, literal document.write support, and PNG/QOI/BMP image viewing (HTTPS is enabled by default with BearSSL), a checked four-level x86-64 page-table manager, and Aiden C 5.3.0 with a larger AEX6 compiler/VM and expanded string/search/random built-ins. The legacy low-level graphics library remains in the source and test suite, but normal boot does not initialize it.

DenOS is still an experimental hobby OS, not a Linux replacement or a complete modern general-purpose operating system. Apps and Aiden C programs currently execute in kernel context: there is no ring-3 process isolation or syscall-backed userspace. Aiden C remains a substantial but incomplete C subset, not a full ISO C implementation.

## 0.5.7 — network reliability and kernel-safety audit

- Fixed TCP ACK processing so a valid acknowledgement is consumed even when the same segment carries out-of-order data; the receiver sends a duplicate cumulative ACK for the missing bytes. Added an emulated-peer regression for the previously stalling case.
- Improved BearSSL error-31 reporting. The serial log now distinguishes TCP receive timeout, peer reset, peer EOF without `close_notify`, unacknowledged TCP send, and closed receive window. Raised the synchronous TCP/TLS idle timeout to six seconds.
- Added bounded DNS CNAME-chain resolution (up to eight aliases), compressed-name parsing, minimum-chain TTL caching, and no caching for TTL-zero answers.
- Tightened DHCP option lengths and ACK server binding; validates that the selected server remains the source even when option 54 is omitted.
- HTTP now checks `Content-Length` so incomplete bodies are marked truncated, and RTC certificate time rejects invalid leap-second value 60.
- Hardened Aiden C lexer/parser handling of overlong identifiers, bounded operator-token copies, and protected global constant division/modulo by zero.
- Moved the preprocessor's 32 KiB scratch lines out of the 16 KiB bootstrap stack into static compiler workspace; exact-maximum-size sources now compile safely.
- Added a stack-usage regression target that compiles every kernel C unit and rejects unbounded dynamic frames or any single frame above 8 KiB.
- Added tests for out-of-order TCP ACKs, DNS CNAME/TTL behavior, DHCP server and option validation, HTTP framing/truncation, oversized identifiers, and maximum-sized Aiden C source.

## 0.5.6 freestanding BearSSL clock fix

- Disabled BearSSL's automatically detected Unix and Win32 clock backends (`BR_USE_UNIX_TIME=0`, `BR_USE_WIN32_TIME=0`) when compiling the TLS kernel.
- DenOS already supplies the UTC hardware RTC date explicitly through `br_x509_minimal_set_time()`. The upstream host `time()` fallback is therefore unnecessary and caused an unresolved `time` symbol in the freestanding kernel.
- Added a regression build probe that requires both platform clock backends to be disabled and checks that the resulting object has no unresolved `time()` reference.

## 0.5.4 HTTPS and TLS-enabled terminal browser

- Added a BearSSL-backed TLS 1.2 client for the `web https://HOST/PATH` command. It restricts negotiation to forward-secret ECDHE with AES-GCM or ChaCha20-Poly1305.
- Added SNI and certificate hostname checking, X.509 chain validation against build-generated trust anchors, and certificate date validation using the UTC hardware RTC. TLS fails closed when RDRAND, valid time, trust anchors, or certificate verification are unavailable.
- Enabled HTTPS in the normal `make build` and `make run` workflow. Set `TLS=0` for an HTTP-only image. BearSSL error 31 (`BR_ERR_IO`) now includes a COM1 transport-detail message to help isolate TCP timeout/reset/EOF/send failures. The TLS build uses the vendored BearSSL 0.6 source and generates trust anchors from the host's system CA bundle. The normal `make` now builds the TLS-enabled image and therefore requires the vendored BearSSL source and a host CA bundle. `make -f tests/Makefile check-stack-usage` checks per-function kernel stack use; it is included in `make -f tests/Makefile check`.
- Refused HTTPS-to-HTTP redirects and never retry a failed HTTPS request as plaintext. Added a test-only TLS API stub for strict adapter compilation.
- Limitations: BearSSL 0.6 is described upstream as beta-quality, implements TLS 1.2 rather than TLS 1.3, and its minimal X.509 verifier does not perform revocation checks. No live HTTPS handshake or ISO boot was possible in this build environment; see `docs/NETWORKING.md`.

## 0.5.3 networking and CLI browser

- Added a layered E1000 client stack: ARP cache, IPv4 validation/routing, ICMP, UDP, DHCP Discover/Offer/Request/Ack, DNS A-record lookup with TTL cache, and a bounded synchronous TCP client.
- Added the `web http://HOST[:PORT]/PATH` text browser for unencrypted HTTP. It follows one absolute HTTP redirect, handles basic chunked responses, strips HTML/script/style and prints readable text.
- Improved network input validation: ARP sender checks and zeroed request target hardware, DHCP unicast ACK validation and netmask checks, IPv4 fragment/multicast filtering, TCP sequence/RST checks, URL/header/body sanitization, and safer entity decoding.
- `ping` accepts either an IPv4 address or a DNS hostname. Added emulated-peer regression coverage for DNS, DHCP, TCP and HTTP.

## 0.5.2 CLI reliability and organization

- Split kernel-owned native terminal apps into `kernel/apps/files`, `tools`, `system`, and `games`; split Snake, Breakout, and Maze Runner into independent game modules.
- Grouped Aiden C sample sources under `userland/apps/examples`, `games`, and `utilities`; the build generates an installer that seeds them into matching `/home/user` folders on first boot without overwriting existing user files.
- Added `find [PATH] PATTERN`, `du [PATH]`, `which PROGRAM`, PATH-aware Aiden C execution, and `mkdir -p PATH`.
- Fixed checksum commands to process the complete 65,535-byte DenFS file limit, corrected relative `stat` paths, bounded file-browser previews, and rejected directory/file type mismatches in `touch` and writes.
- Fixed tree traversal to report overlong child paths rather than silently recursing into truncated paths; tightened shell argument validation and corrected Breakout wall rendering.
- Added storage regression coverage and a generator test for the app-seeding layout. Long web pages now use a keyboard-driven pager with PageUp/PageDown, arrow-key scrolling, Home/End navigation and Q to exit. The normal boot path remains VGA text-only.

## 0.5.1 terminal-first update

- Removed the automatic framebuffer desktop, the `desktop` shell command, and the graphical Files/Notes/Calculator/System Monitor/Paint/Arcade app implementation.
- Removed the GRUB framebuffer request and made the VGA text console the only normal boot interface.
- Added native terminal commands: `apps`, `browse [PATH]`, `calc [EXPR]`, `notes [FILE]`, `monitor`, and `arcade`.
- Added three colored-ASCII games: Snake, Breakout, and Maze Runner. Controls use the keyboard; no mouse or window system is required.
- Preserved DenFS v4 long paths/migration and the Aiden C 5.3.0 expansion from 0.5.0.


## 9.5.2 bootstrap assembly fix (previous release)

Fixed NASM `.bss` alignment directives and the incorrectly scoped GDT pointer symbol. Run `make -f tests/Makefile boot-asm-check` for the regression check, then `make build` with NASM, GRUB rescue tools and Xorriso installed to assemble and link the bootable ISO.

## Previous release 9.5.0 highlights

- **Four-level virtual-memory manager:** `kernel/mm/paging.c` attaches to the existing bootstrap CR3 without replacing the identity map. It provides checked map, explicit remap, query, protect and unmap operations for 4 KiB pages; it recognizes and safely splits existing 2 MiB and 1 GiB mappings.
- **Permission and address checks:** validates canonical virtual addresses, physical alignment/address width, supported flags, NX/global-page capability, and writable+executable policy. Enables supervisor write-protect (CR0.WP), NXE where supported, and PGE where supported.
- **Page-table lifecycle:** allocates intermediate tables from PMM, rolls back newly-created empty tables on allocation failure, and reclaims empty dynamic page-table pages after unmap. Physical data frames remain caller-owned.
- **Kernel virtual allocator:** adds a 64 MiB high-half virtual window backed by individually allocated PMM frames. `kvalloc` returns contiguous virtual memory, `kvfree` verifies mappings and frame ownership, and failed rollback quarantines ranges instead of risking physical-frame aliasing. Existing `kmalloc` remains unchanged.
- **Diagnostics:** `paging` and `vmstat` report page-table allocation, mapping changes, failed operations, NX/WP/PGE protections, virtual allocator usage, and quarantined ranges.
- **New host coverage:** dedicated page-table and virtual-allocation tests exercise huge-page splitting, mapping replacement, permissions, high canonical addresses, OOM and partial-unmap rollback, quarantine, table reclamation, PMM ownership, and statistics.
- **Developer workflow:** `make -f tests/Makefile paging-test` and `make -f tests/Makefile vmalloc-test` run the memory suites alone; `make -f tests/Makefile check-kernel-c` checks every kernel C translation unit using the freestanding warning-as-error flags; `make -f tests/Makefile check` runs all host tests and the C check.

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
- **Large graphics validation corpus:** ten deterministic 1600×900 QOI test scenes with exact RGBA golden surfaces. `make -f tests/Makefile qoi-fixtures-test` decodes all ten images and compares every RGB pixel with the reference. The raw golden surfaces are real test data, not archive padding.
- **Separate QEMU window:** `make run` uses `-display gtk` by default; set `QEMU_DISPLAY=sdl` to use SDL where available.

## Build and run

On Debian/Ubuntu, install the host tools:

```sh
sudo apt update
sudo apt install build-essential nasm xorriso mtools grub-pc-bin grub-common qemu-system-x86 python3
```

Build the kernel and ISO:

```sh
make clean
make build
```

Run in a separate QEMU window:

```sh
make run
```

Choose SDL:

```sh
make run QEMU_DISPLAY=sdl
```

The project creates `denos-data.img` on first build. `make run` attaches it as the primary IDE master because the current ATA driver uses legacy IDE PIO (AHCI/SATA and NVMe are not yet supported). `make clean` preserves this persistent disk image. To discard it, stop the VM, verify your backups, and manually remove `denos-data.img`. Serial diagnostics are written to `build-tls/serial.log` by default (`build/serial.log` when `TLS=0`). If the kernel reports RAM fallback, run `diskinfo` and `bootlog` to see the exact ATA/mount error.

## Shell quick reference

| Command | Purpose |
|---|---|
| `help` / `help editor` / `help programming` | Command and subsystem help |
| `ls [PATH]`, `cd PATH`, `pwd`, `tree [PATH]` | Navigate directories |
| `mkdir [-p] PATH`, `rmdir PATH`, `touch FILE` | Create directories/files |
| `cat FILE`, `edit FILE`, `write FILE TEXT` | Read, edit, and write text files |
| `cp SRC DST`, `mv SRC DST`, `rm FILE` | File operations |
| `grep PATTERN FILE`, `find [PATH] PATTERN`, `wc FILE` | Search names and file contents |
| `du [PATH]`, `which PROGRAM` | Inspect stored text size and find Aiden programs on PATH |
| `head [-n COUNT] FILE`, `tail [-n COUNT] FILE` | Preview file content |
| `stat PATH`, `hexdump FILE` | Inspect stored file metadata/content |
| `cc SOURCE.c [-o PROGRAM]`, `run PROGRAM`, `cinfo` | Compile and run Aiden C bytecode |
| `sysinfo`, `cpuinfo`, `uptime`, `free`, `pmm`, `paging`, `vmstat`, `tasks` | Inspect kernel and virtual-memory state |
| `devices`, `bootlog`, `df`, `diskinfo`, `partitions`, `mounts` | Inspect hardware and storage |
| `services`, `service start|stop NAME`, `ipc`, `events` | Inspect kernel services |
| `net`, `dhcp`, `dns HOST`, `ping HOST`, `web https://HOST/PATH` / `web http://HOST/PATH` | Inspect networking, configure DHCP, resolve/ping hosts, and read web text (HTTPS is enabled by default) |
| `sha256 FILE`, `crc32 FILE`, `uuid`, `random` | Checksums and identifiers |
| `apps`, `browse [PATH]`, `notes [FILE]`, `calc [EXPR]` | Native text-mode apps |
| `monitor`, `arcade` | Live system view and three ASCII games |

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

The graphics API is in `kernel/include/gfx.h`; implementation is `kernel/drivers/graphics.c`. The QOI decoder is in `kernel/lib/qoi.c`, and the bounded PNG decoder is in `kernel/lib/png.c`. The deterministic test/resource corpus is in `resources/graphics-lab/fixtures/`; each `.qoi` input is paired with an exact `.rgba` golden reference. Regenerate all ten with:

```sh
cc -O3 -std=c11 tools/generate_gfx_fixtures.c -lm -o /tmp/gfx-fixtures
/tmp/gfx-fixtures resources/graphics-lab/fixtures
make -f tests/Makefile qoi-fixtures-test
```

The fixtures are developer test data and are not loaded into the boot filesystem or kernel memory during normal startup.

## Tests and verification

Run all host-side regressions (including exception decoder coverage):

```sh
make -f tests/Makefile test
```

The test suite includes host test executables, all ten high-resolution QOI reference fixtures, and the freestanding compiler checks. `make -f tests/Makefile check` also compiles every kernel C translation unit with the production flags and checks stack-frame usage.

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
kernel/lib/               strings, formatting, crypto, QOI/PNG decoders, parsers
kernel/mm/                heap and physical memory manager
kernel/net.c              Ethernet, ARP, IPv4 and ICMP core
kernel/net/               UDP, DHCP, DNS, TCP, default BearSSL TLS and HTTP(S) text browser
kernel/sys/               services, scheduler, IPC, events, devices
kernel/compiler.c         Aiden C to AEX6 compiler
kernel/vm.c               AEX1–AEX6 virtual machine
kernel/apps/files/        native terminal file browser
kernel/apps/tools/        native terminal utilities
kernel/apps/system/       text-mode system monitor
kernel/apps/games/        Snake, Breakout, Maze Runner modules
userland/apps/examples/   bundled Aiden C examples
userland/apps/games/      bundled Aiden C game sources
userland/apps/utilities/  bundled Aiden C utilities
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
- Networking is an experimental E1000 client stack with ARP, IPv4, ICMP, UDP, DHCP, DNS, one synchronous TCP client and an HTTP/HTTPS text reader. HTTPS is enabled in the normal build and supports TLS 1.2 only; there is no TLS 1.3, certificate revocation checking, IPv6, Wi-Fi, concurrent sockets, TCP server, or fragment reassembly. See `docs/NETWORKING.md`.
- No POSIX shell, user/permission model, service manager isolation, package manager, UEFI boot path, sound stack, or modern power management.
- Native terminal apps are kernel-owned and single-session; no ring-3 userspace or isolated app processes exist yet.

DenOS is a serious learning project. The next major milestone is a protected user-mode process model and versioned syscall boundary—not claiming completeness before those foundations exist.
