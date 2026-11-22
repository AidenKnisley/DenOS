# DenOS changelog

## 0.3.1

- Centralized the release version so the startup banner, shell, editor, and build output agree.
- Added short shell aliases: `?`, `cls`, `dir`, `type`, `new`, `del`, `edit`, `sys`, `mem`, `disk`, `ver`, and `run`.
- Added the shorter compile form `cc source.c program`; the existing `cc source.c -o program` form remains supported.
- Rejected overlong shell filenames instead of silently truncating them.
- Fixed executable-name truncation in the AEX runner.
- Fixed the VM instruction-budget counter so endless loops stop reliably instead of wrapping the counter.
- Made guest integer arithmetic wrap consistently and made the compiler reject decimal literals that do not fit its 32-bit bytecode immediate.
- Added compiler/VM regression tests alongside ATA/filesystem tests, including signed-input overflow and malformed-comment cases.
- Expanded ATA mocks to test all four IDE master/slave positions.
- Saved and restored x87/SSE state during timer interrupts, and clear the direction flag before calling C interrupt code.
- Added COM1 detection and bounded transmit waits so a disabled serial port cannot hang VGA operation.
- Hardened bootstrap heap allocation against oversized requests.
- Corrected filesystem free-space reporting for its fixed-size per-file slots.

## Earlier releases

See the project history and roadmap for work from earlier 0.3 development snapshots.
