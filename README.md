# DenOS

**A small, experimental 64-bit operating system written in C and NASM.**

DenOS is a from-scratch operating system development project focused on exploring how the parts of an operating system fit together. It is designed as a compact, understandable codebase rather than as a replacement for a general-purpose desktop operating system.

The project boots through GRUB into an x86-64 kernel and provides a text-based user interface, basic hardware support, shell environment, storage experiments, a built-in text editor, and an experimental C-subset compiler with a bytecode virtual machine.

DenOS is actively developed. Its features, interfaces, and level of completeness can differ between source snapshots and releases. This README describes the project in general and intentionally avoids documenting a fixed set of shell commands or key bindings that may not exist in every version.

## Project overview
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

DenOS is built to explore the core components of an operating system in a small codebase. The project has included work in the following areas:

- **64-bit kernel:** A freestanding x86-64 kernel written in C and NASM.
- **Boot process:** GRUB-based booting and setup for 64-bit long mode.
- **Text-based interface:** A classic VGA text-mode display with a blue and cyan color scheme, alongside serial output for debugging.
- **Hardware support:** Keyboard input, interrupt handling, and timer support.
- **Command shell:** An interactive environment for working with the system and its built-in tools. The available features vary by version.
- **Storage:** Experimental ATA/IDE disk support and a basic filesystem intended to provide persistent storage when a supported disk is detected. Some builds may use a temporary RAM-backed filesystem when persistent storage is unavailable.
- **Text editor:** A lightweight, Vim-inspired editor. Its capabilities and controls may change between versions.
- **C-subset compiler:** An experimental compiler that translates a limited subset of C into DenOS bytecode.
- **AEX virtual machine:** A bytecode interpreter for running programs produced by the experimental compiler.
- **Example programs:** The source has included small demonstration applications and programming examples. The exact collection may differ between releases.

Not every feature listed above is necessarily present or equally complete in every source version. Check the source snapshot you are using for its actual implementation and capabilities.

## Design goals

DenOS is developed around a few simple goals:

- **Understandability:** Keep the implementation approachable for someone learning about kernels and low-level programming.
- **Incremental development:** Build and improve operating-system components one step at a time.
- **Minimal dependencies at runtime:** Keep the kernel small and avoid relying on a full host operating system once it has booted.
- **Classic computing experience:** Use a straightforward text interface inspired by personal computers and operating systems from the 1980s.
- **Experimentation:** Provide room to explore hardware drivers, filesystems, language tools, and kernel architecture.

## Building and running

The repository contains the source code and build configuration for the project. Build requirements and available build targets can change between versions, so use the Makefile and any accompanying build notes in the specific source snapshot you have selected as the authoritative instructions.

A typical development setup uses a Linux environment with a C compiler, NASM, GRUB utilities, an ISO creation tool, and QEMU for emulation. The exact packages required depend on the build configuration in that version.

DenOS is primarily developed and tested in an emulator. When testing storage, make sure the virtual machine is configured with a supported IDE disk and that the corresponding writable disk image is attached. Booting an ISO alone does not necessarily provide a writable data disk.

## Storage

DenOS has experimental support for persistent storage through a legacy ATA/IDE device. The filesystem is intentionally small and limited compared with filesystems used by modern operating systems.

The storage implementation has evolved over time. Depending on the version, disk detection, supported IDE positions, filesystem capacity, diagnostics, and fallback behavior may differ. Some builds can use a temporary RAM-backed filesystem if a supported persistent disk is not detected. Files stored only in RAM-backed storage are not expected to survive a reboot.

For persistent storage, retain the virtual disk image used by your build and make sure it is attached to the emulator through a controller supported by that version's driver. Back up the image before performing any cleanup or filesystem changes. Do not assume that a cleanup target is harmless to disk images; inspect the relevant Makefile rule in the source snapshot first.

The current design is not intended to provide the full feature set of a modern storage stack. Features such as AHCI, SATA, DMA, journaling, and broad filesystem compatibility should not be assumed unless they are explicitly implemented in the version being used.

## Text interface and built-in tools

DenOS uses a keyboard-driven text interface. The shell and built-in tools are intended to make it possible to inspect the system, interact with files, and experiment with programs without requiring a graphical desktop.

The exact set of shell features and built-in tools changes as the project develops. The editor is inspired by Vim, but it should not be treated as a complete implementation of Vim. Consult the source and in-system help provided by the particular version for its supported interface.

## Compiler and virtual machine

Some DenOS versions include an experimental compiler for a limited subset of the C language. Rather than producing native x86-64 executables, the compiler translates supported source constructs into AEX bytecode, which is interpreted by the kernel's virtual machine.

The compiler is a learning project, not a replacement for GCC or a complete ISO C implementation. Language coverage, built-in functions, and runtime behavior are version-dependent. AEX programs execute inside the kernel's environment and should not be considered isolated user-mode processes. Avoid running untrusted programs.

## Current development status

DenOS is an early-stage operating system project. Its existing components are useful for experimentation, but the project does not yet provide all the facilities expected of a general-purpose operating system.

Areas for ongoing development include:

- More complete physical memory management and virtual memory.
- Processes, scheduling, context switching, and user-mode execution.
- Stronger process isolation and a real permissions model.
- A more capable filesystem with directories, flexible allocation, and improved crash safety.
- Broader hardware support and additional device drivers.
- Networking and other system services.
- A more complete compiler, runtime, and application environment.
- More extensive automated tests and repeatable emulator-based boot testing.

The roadmap is not a promise that every item is present in the current release. These are areas that may be explored as the project evolves.

