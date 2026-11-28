# DenOS changelog

## 0.5.7 — network reliability and kernel-safety audit

- Fixed a TCP client deadlock where a valid ACK in an out-of-order receive segment was discarded along with its payload. ACK state is now handled independently; future data still triggers a duplicate cumulative ACK because this client does not implement reassembly.
- Added TLS transport diagnostics for BearSSL error 31 (`BR_ERR_IO`) and increased per-operation TCP/TLS receive/send idle waits to six seconds. Diagnostics identify timeout, reset, peer EOF without TLS `close_notify`, unacknowledged send, and zero peer window.
- Added bounded DNS CNAME-chain support with compressed-name validation and minimum TTL propagation; TTL-zero records are not cached.
- Hardened DHCP option-length checks and selected-server identity validation, including ACKs without option 54.
- Added HTTP `Content-Length` truncation detection, rejection of duplicate/conflicting body-framing headers, and rejection of an invalid RTC second value before certificate-date checks.
- Fixed Aiden C overlong-identifier token lengths before parser copies, bounded operator copies, global constant divide/modulo by zero handling, and a bootstrap-stack overflow risk in the preprocessor. Its large scratch buffers now live in static compiler workspace, and the exact 32 KiB source boundary is tested.
- Added a build-time stack-usage audit across kernel C translation units; it rejects any single frame over 8 KiB or unbounded dynamic stack allocations.
- Added emulated network, compiler, maximum-source, and malformed-input regressions. Full host regression, ASan/UBSan compiler/network tests, and selected Clang static analysis passed.


## 0.5.6 — freestanding BearSSL clock fix

- Fixed the TLS kernel link error caused by BearSSL's Unix platform autodetection adding an unresolved `time()` reference from `x509_minimal.c`.
- Explicitly disable BearSSL's Unix and Win32 system-clock backends. DenOS uses its own UTC CMOS RTC and passes the validation time directly to `br_x509_minimal_set_time()`; no hosted OS time function is needed.
- Added a regression build probe to `bearssl-freestanding-check`. It verifies the compile-time configuration and ensures no unresolved `time` symbol is emitted.


## 0.5.5 — freestanding BearSSL link fix

- Fixed the optional TLS build failure caused by host glibc fortification emitting an unresolved `__memcpy_chk` reference from BearSSL (including `ecdsa_atr.c`).
- Explicitly compile BearSSL with `_FORTIFY_SOURCE` undefined and then set to `0`, while retaining freestanding and `-fno-builtin` flags. This keeps glibc-only checked-memory symbols out of the kernel; no fake `__memcpy_chk` shim is added.
- Make every BearSSL object depend on the Makefile so a change to the freestanding compiler flags forces stale third-party objects to rebuild.
- Added `bearssl-freestanding-check` to `make check`. It deliberately simulates `_FORTIFY_SOURCE=2`, compiles a bounded local-buffer `memcpy`, and verifies the object does not reference `__memcpy_chk`.

## 0.5.4 — optional HTTPS and TLS-enabled CLI browser

- Added an optional BearSSL 0.6 TLS 1.2 client for the terminal `web https://...` command, restricted to ECDHE with AES-128-GCM or ChaCha20-Poly1305.
- Added SNI and hostname validation, X.509 chain checks against build-generated system trust anchors, and certificate date checks using the hardware RTC.
- Added fail-closed RDRAND entropy handling, an optional pinned BearSSL download with SHA-256 verification, CA-root generation, and `make tls` / `make tls-run` targets.
- HTTPS-to-HTTP downgrade redirects are refused. TLS is never silently replaced with plaintext.
- Documented the TLS 1.2-only scope, RTC/RDRAND requirements, no-revocation limitation, and the need for a real QEMU/Internet handshake test. BearSSL is not included in the source archive.

## 0.5.3 — E1000 networking and text web client

- Added UDP checksums and a bounded datagram queue, DHCP Discover/Offer/Request/Ack with lease validation, DNS A-record resolution with a TTL cache, and a single synchronous TCP client.
- Added the `web` CLI page reader for plain HTTP. It supports GET, one absolute HTTP redirect, chunked transfer decoding, HTML/script/style stripping, basic entity decoding, and bounded terminal text rendering.
- Extended `ping` to accept hostnames as well as IPv4 addresses and expanded `net`/`help network` documentation.
- Hardened the packet path: validate ARP Ethernet/ARP sender consistency, zero ARP request target hardware addresses, reject invalid IPv4 fragments/multicast and malformed UDP lengths, validate DHCP unicast ACK/server/address/netmask data, and tighten TCP sequence/RST handling.
- Hardened web input/output: reject URL control characters and malformed ports/status codes, sanitize downloaded text/header output and numeric entities, and refuse unsupported content encodings.
- Added emulated-peer tests for DNS/cache, DHCP (including a unicast ACK), TCP handshake, HTTP HTML filtering, URL injection rejection, and HTTPS refusal. Host tests do not substitute for a real E1000/QEMU smoke test.
- HTTPS/TLS, certificate validation, IPv6, Wi-Fi, concurrent sockets, TCP server mode, automatic DHCP lease renewal, and IPv4 fragment reassembly remain unsupported.

## 0.5.2 — CLI reliability, app organization, and sample seeding

- Split native kernel terminal apps into `files/`, `tools/`, `system/`, and `games/`; Snake, Breakout, and Maze Runner now have independent game modules.
- Grouped bundled Aiden C samples into `examples/`, `games/`, and `utilities/`. Added a build-generated `apps_install()` that seeds these sources into matching home folders without overwriting user files.
- Added `find [PATH] PATTERN`, `du [PATH]`, `which PROGRAM`, PATH-aware Aiden C program lookup, and `mkdir -p PATH`.
- Fixed checksum truncation at 16 KiB, relative `stat` paths, overlong directory-tree path handling, oversized file-browser previews, and directory/file type mismatches in filesystem operations.
- Tightened shell argument validation, fixed Breakout wall rendering, updated docs and version strings, and expanded DenFS/app-installer regression tests. `make check-kernel-c` now also compiles the generated app-installer source.
- Kept the normal boot and all native interaction terminal-only. This remains a single-address-space kernel without ring-3 process isolation or POSIX compatibility.

## 0.5.1 — terminal-only native apps

- Removed the automatic framebuffer desktop, windowed desktop apps, and graphical arcade implementation. The normal boot path now enters the VGA text shell directly.
- Removed the Multiboot2 framebuffer request and made GRUB use its text console output.
- Added `apps`, `browse`, `calc`, `notes`, `monitor`, and `arcade` shell commands.
- Added colored-ASCII Snake, Breakout, and Maze Runner games with keyboard controls.
- Kept the lower-level graphics library and its host tests in the repository, but they are not initialized during normal boot.
- Preserved DenFS v4 and the expanded Aiden C 5.3.0 compiler/VM from 0.5.0.

## 0.5.0 — framebuffer desktop, DenFS v4, and Aiden C expansion

- Added a Multiboot2 request for a 1280×720×32 RGB framebuffer. When GRUB supplies a supported mode, DenOS opens a 640×360 logical desktop presented at 2× scale; the text shell remains available as a fallback or through the `text` boot argument.
- Added six kernel-native apps: Files, Notes, Calculator, System Monitor, Paint, and Arcade. Arcade includes graphical Snake, Breakout, and Maze Runner games with keyboard/mouse controls.
- Upgraded DenFS to v4 with 255-byte paths/names, larger directory records, `fs_rename`, `fs_exists`, and `fs_is_file`; recognized v1/v2/v3 volumes migrate forward. The first-boot tree now has lowercase home folders and clearer starter documents/projects while preserving legacy folders.
- Expanded Aiden C 5.3.0 to 32 KiB source, 2,048 instructions, 8 KiB data, 96 functions, 32 parameters, 1,024 global cells, 8,192 local cells, and 128 call frames. Added bounded string/search helpers and seeded deterministic `rand`/`srand` support.
- Expanded shell command lines and history to 512 bytes so long filesystem paths can be used from the shell; aligned executable lookup and directory listings with the 255-byte DenFS path limit.
- Added regression coverage for long filenames, filesystem migration, larger Aiden C source/code/function frames, new built-ins, framebuffer header configuration, and all prior host test suites.

## 0.4.9 — DenOS version reset and Vim code suggestions

- Set the DenOS release version consistently to 0.4.9 in the build, kernel banner, shell version commands, GRUB menu, and current user/developer documentation.
- Versioned the Aiden C compiler independently as 5.2.4; `cc --version` and `cinfo` report the compiler and OS versions separately.
- Expanded Vim-style completion to 24 suggestions and prioritized identifiers found in the active source buffer.
- Added all Aiden C VM built-ins to the completion catalog, including `isprint`, `isgraph`, and `ispunct`.
- Replaced the bottom-line completion strip with a floating VS Code-inspired suggestion dropdown: selected-row highlight, kind icons, right-aligned type labels, and a five-row viewport that follows the selected candidate.
- Added Up/Down navigation; `Esc` first dismisses the suggestion popup and a second press leaves insert mode. `Tab`, `Ctrl-N`, and `Ctrl-P` remain supported.
- Fixed filesystem transaction handling: failed persistent writes roll back the RAM cache, mark the disk unavailable for the current boot, and reject later writes instead of silently accepting more temporary data.
- Fixed partial-mount handling: if required file data cannot be read or a directory record is malformed, DenFS abandons the mount and clears the incomplete cache rather than exposing an incomplete filesystem as persistent.
- Updated `diskinfo` to show the exact RAM-fallback reason, and made the QEMU runner attach `denos-data.img` explicitly as the primary legacy IDE master.
- Documented the current storage limitation: IDE PIO is supported; AHCI/SATA and NVMe controllers are not yet supported by this driver.

## 9.5.2 — bootstrap assembly build fix

- Fixed the GDT descriptor pointer label: `lgdt` now references the explicitly named `gdt64_pointer` symbol rather than a NASM local label scoped beneath `gdt_tss_descriptor`.
- Changed all alignment directives in `.bss` to `alignb`, preventing NASM from attempting to emit initialized padding bytes in a NOBITS section.
- Added `make boot-asm-check` and included its static regression checks in `make check`. This check validates source-level symbol/alignment consistency; it does not replace NASM assembly or a QEMU boot test.
- Preserved the existing boot layout, Multiboot2 header, bootstrap identity map, GDT contents, and all 9.5.1 kernel/userland features.


## 9.5.0 — active-kernel virtual-memory foundation

- Added `kernel/mm/paging.c` and `kernel/include/paging.h`, a checked four-level x86-64 page-table manager attached to the active CR3.
- Preserved the GRUB bootstrap identity map; added 4 KiB map, explicit remap, query, protect, and unmap APIs.
- Added 2 MiB and 1 GiB huge-page querying and splitting while preserving sibling translations.
- Added canonical-address, physical-address-width, alignment, mapping ownership, and permission validation.
- Enabled NXE, CR0.WP, and CR4.PGE when supported; enforced W^X for newly mapped writable leaves on NX CPUs.
- Added PMM-backed intermediate page-table allocation, OOM rollback, and reclamation of empty dynamic table pages.
- Added a separate 64 MiB PMM-backed kernel virtual allocator with contiguous virtual ranges, ownership preflight, multi-page rollback, partial-unmap restoration, and quarantine on unrecoverable inconsistencies.
- Added a non-mutating PMM frame-ownership query and integrated `paging`/`vmstat` diagnostics for virtual allocation usage and quarantined ranges.
- Added `paging`/`vmstat`, `make paging-test`, `make check-kernel-c`, and `make check`.
- Added host tests for huge-page splitting, high canonical mappings, permission flags, replacement semantics, allocator exhaustion, rollback, and statistics.
- Added dedicated virtual-allocation tests for multi-page requests, PMM ownership, mapping corruption, allocation/unmap failure recovery, quarantine and reuse.
- Existing DenFS, shell commands, Aiden C/AEX formats, network baseline, and graphics library remain present.
- Limitations: one active CR3 only; no process address spaces, demand paging, swap, TLB shootdowns, SMP locking, or ring-3 security boundary.

# DenOS changelog

## 9.4.1 — CPU exception and interrupt reliability baseline

- Install exception IDT gates before normal kernel subsystem initialization.
- Add normalized exception stubs and register/error-code diagnostics.
- Add page-fault CR2/access-bit reporting and bounded frame-pointer stack traces.
- Add a 64-bit TSS with a dedicated IST1 stack for double fault and a separate IST2 stack for PIT IRQ0.
- Preserve all GPRs and x87/SSE state in the timer IRQ stub, running on its own IST2 stack.
- Add focused exception decoder regression tests.
- Preserve the existing CLI, DenFS/VFS, Aiden C/AEX6, services, networking and graphics APIs.


## 9.4.0 — CLI/editor and graphics validation expansion

- Added a bounded source completion provider for C/Aiden C editor buffers. Suggestions include C keywords/types, Aiden VM built-ins, and identifiers discovered in the current file; comments and quoted literals are skipped.
- Added `Tab` acceptance, `Ctrl-N`/`Ctrl-P` cycling, function-call parentheses insertion, and completion toggles.
- Added editor `:check` structural validation for delimiters, comments, and quoted literals.
- Reworked shell command-line editing to support cursor movement, Home/End, insertion, Delete/Backspace, Ctrl-A/E/U/K, command history, and Tab completion for commands/current-directory files.
- Added `grep`, `wc`, `head`, `tail`, `stat`, `hexdump`, and bounded `tree` shell commands.
- Added `help editor`, CLI/editing manuals, and updated release/build guides.
- Added three Aiden C sample applications covering prime sieving, bubble sorting/pointer traversal, and Euclid/primality algorithms.
- Added ten deterministic 1600×900 graphics/QOI fixtures with paired RGBA golden surfaces; added a host test that decodes every QOI and compares every RGB pixel.
- Preserved the Aiden C AEX6 compiler/VM, the no-GUI boot path, and the expanded kernel graphics library.

## 9.3.0 — expanded Aiden C and restored graphics library

- AEX6 globals, arrays, pointer cells and legacy AEX1–AEX5 compatibility.
- Restored the software renderer, framebuffer, mouse/window helpers, QOI decoder, modern UI font, geometry, text and alpha/scaled image operations.
- Expanded compiler/VM safety tests and documented the real supported C subset.

## 9.2.0 — Aiden C/QEMU update

- Separate-window QEMU display configuration and serial diagnostics.
- Aiden C user functions, recursive calls, AEX versions, runtime checks and improved formatted input/output.

## Earlier releases

Earlier releases introduced the text shell, filesystem, memory manager, VFS, scheduler, IPC, event queue, E1000 networking, system utilities, and the original graphics library. The default boot path changed to the framebuffer desktop in 0.5.0; earlier releases started in text mode.
