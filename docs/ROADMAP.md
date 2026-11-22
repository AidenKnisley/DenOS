# DenOS roadmap

## Milestone 0.1 — bootable foundation
- [x] x86-64 long-mode entry (Multiboot2/GRUB)
- [x] VGA text output, COM1 debug output, and polling PS/2 input
- [x] Visible block editor cursor and active-line gutter/highlight
- [x] Interactive shell and familiar command names
- [x] Fixed-capacity in-memory file table (initial version)
- [x] Full-screen Vim-style editor with C syntax highlighting, navigation, search, save/quit, yank/paste, and undo/redo

## Milestone 0.2 — kernel services
- [x] Bootstrap heap allocator
- [x] PIT timer and IRQ0 path
- [x] IDT/PIC initialization
- [x] Fixed-capacity filesystem with ATA PIO disk backing
- [x] Persistent 8 MiB QEMU IDE disk image; changes survive reboot
- [ ] Physical page allocator and general virtual-memory manager
- [ ] Directories, variable-size file allocation, and filesystem journaling

## Milestone 0.3.1 — first in-OS C execution
- [x] Aiden C subset parser/compiler
- [x] AEX bytecode executable format
- [x] AEX virtual-machine loader/interpreter
- [x] Shell `cc source.c -o program` command
- [x] Shell `./program` execution command
- [x] Bundled hello, guess, calculator, tic-tac-toe, and Life demos
- [ ] Native x86-64 code generation and ELF64 loader
- [ ] Ring-3 execution, syscall ABI, scheduler, and process isolation

## Milestone 0.4 — broader C environment
- [ ] Expand language coverage (functions, pointers, structures, global data)
- [ ] Add floating-point support and a fuller C runtime
- [ ] Port binutils and a production C compiler to DenOS userspace
- [x] Persistent storage for source and compiled AEX files
- [ ] General filesystem and executable loading from disk in a separate process
- [ ] Full Vim compatibility: visual mode, multiple buffers, macros, split windows, and advanced editing commands
- [ ] Shell scripting and process pipelines
