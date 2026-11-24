# Changelog

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

Earlier releases introduced the text shell, filesystem, memory manager, VFS, scheduler, IPC, event queue, E1000 networking, system utilities, and the original graphics library. The default boot path in this branch remains text-only.
