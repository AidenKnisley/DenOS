# DenOS 0.4.9: CLI OS with checked four-level x86-64 page-table management.
CC := gcc
LD := ld
NASM := nasm
QEMU := qemu-system-x86_64
GRUB := grub-mkrescue
VERSION := 0.4.9
QEMU_DISPLAY ?= gtk

BUILD := build
ISO_DIR := $(BUILD)/iso
DATA_IMG := denos-data.img
DATA_VDI := denos-data.vdi
DATA_MIN_BYTES := 16796160

CFLAGS := -MMD -MP -std=gnu11 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
          -fno-builtin -fno-asynchronous-unwind-tables -fno-unwind-tables \
          -m64 -mno-red-zone -mcmodel=small -fno-omit-frame-pointer -Wall -Wextra -Werror -O2 \
          -Ikernel/include
LDFLAGS := -m elf_x86_64 -nostdlib -z max-page-size=0x1000 -T kernel/linker.ld

C_SOURCES := $(shell find kernel -name '*.c' -type f)
ASM_SOURCES := $(shell find kernel -name '*.asm' -type f)
C_OBJECTS := $(patsubst kernel/%.c,$(BUILD)/kernel/%.o,$(C_SOURCES))
ASM_OBJECTS := $(patsubst kernel/%.asm,$(BUILD)/kernel/%.o,$(ASM_SOURCES))
OBJECTS := $(C_OBJECTS) $(ASM_OBJECTS)
DEPFILES := $(C_OBJECTS:.o=.d)

.PHONY: all clean clean-data storage-upgrade run test check check-kernel-c exception-test paging-test libc-test boot-asm-check
.DEFAULT_GOAL := all

all: $(BUILD)/denos.iso
	@if [ ! -f $(DATA_IMG) ]; then \
		echo "Creating persistent DenOS data disk..."; \
		dd if=/dev/zero of=$(DATA_IMG) bs=512 count=65536 status=none; \
	fi
	@echo "DenOS $(VERSION) build complete!"

clean:
	@echo "Cleaning generated build products (personal files and data images are preserved)..."
	rm -rf $(BUILD)

clean-data:
	@echo "Removing DenOS data images..."
	rm -f $(DATA_IMG) $(DATA_VDI)

storage-upgrade:
	@if [ -f $(DATA_IMG) ]; then \
		current=$$(wc -c < $(DATA_IMG)); \
		if [ "$$current" -lt 33554432 ]; then \
			if [ ! -e $(DATA_IMG).backup ]; then cp $(DATA_IMG) $(DATA_IMG).backup; \
			else echo "Backup already exists: $(DATA_IMG).backup"; fi; \
			truncate -s 32M $(DATA_IMG); \
		else echo "$(DATA_IMG) is already at least 32 MiB; no resize needed."; fi; \
	else dd if=/dev/zero of=$(DATA_IMG) bs=1M count=32 status=none; fi
	@echo "Data image is ready for DenFS v3. Keep the backup until DenOS verifies migration."

run: all
	@test -s $(DATA_IMG) || { echo "ERROR: $(DATA_IMG) is missing or empty."; exit 1; }
	@disk_bytes=$$(wc -c < $(DATA_IMG)); \
	  if [ "$$disk_bytes" -lt $(DATA_MIN_BYTES) ]; then \
	    echo "ERROR: $(DATA_IMG) is too small ($$disk_bytes bytes)."; \
	    echo "DenOS needs at least $(DATA_MIN_BYTES) bytes for its filesystem."; exit 1; fi
	@echo "Opening DenOS in a separate QEMU window (display: $(QEMU_DISPLAY))."
	@echo "Persistent ATA image: $(abspath $(DATA_IMG)) (primary IDE master)."
	@echo "COM1 diagnostics: $(BUILD)/serial.log"
	$(QEMU) -name DenOS -m 512M -cdrom $(BUILD)/denos.iso -boot order=d \
		-machine pc,accel=tcg -cpu qemu64 -vga std -display $(QEMU_DISPLAY) -monitor none \
		-drive file=$(DATA_IMG),format=raw,if=ide,index=0,media=disk,cache=writeback \
		-netdev user,id=net0 -device e1000,netdev=net0 \
		-serial file:$(BUILD)/serial.log -no-reboot -no-shutdown

test: $(BUILD)/event-test $(BUILD)/ipc-test $(BUILD)/vfs-test $(BUILD)/scheduler-test \
      $(BUILD)/compiler-vm-test $(BUILD)/compiler-app-test \
      $(BUILD)/ata-mock-test $(BUILD)/calculator-test $(BUILD)/system-core-test \
      $(BUILD)/hardware-mock-test $(BUILD)/pmm-test $(BUILD)/keyboard-poll-test \
      $(BUILD)/net-protocol-test $(BUILD)/crypto-utils-test $(BUILD)/parse-test \
      $(BUILD)/utf8-test $(BUILD)/bitmap-ringbuf-test $(BUILD)/env-history-test \
      $(BUILD)/bootargs-test $(BUILD)/partition-test $(BUILD)/elf64-test $(BUILD)/service-test \
      $(BUILD)/graphics-test $(BUILD)/graphics-mouse-test $(BUILD)/framebuffer-test $(BUILD)/qoi-test \
      $(BUILD)/completion-test $(BUILD)/qoi-fixtures-test $(BUILD)/exception-test $(BUILD)/paging-test $(BUILD)/vmalloc-test
	./$(BUILD)/compiler-vm-test
	./$(BUILD)/compiler-app-test
	./$(BUILD)/event-test
	./$(BUILD)/ipc-test
	./$(BUILD)/vfs-test
	./$(BUILD)/scheduler-test
	./$(BUILD)/ata-mock-test
	./$(BUILD)/calculator-test
	./$(BUILD)/system-core-test
	./$(BUILD)/hardware-mock-test
	./$(BUILD)/pmm-test
	./$(BUILD)/keyboard-poll-test
	./$(BUILD)/net-protocol-test
	./$(BUILD)/crypto-utils-test
	./$(BUILD)/parse-test
	./$(BUILD)/utf8-test
	./$(BUILD)/bitmap-ringbuf-test
	./$(BUILD)/env-history-test
	./$(BUILD)/bootargs-test
	./$(BUILD)/partition-test
	./$(BUILD)/elf64-test
	./$(BUILD)/service-test
	./$(BUILD)/graphics-test
	./$(BUILD)/graphics-mouse-test
	./$(BUILD)/framebuffer-test
	./$(BUILD)/qoi-test
	./$(BUILD)/completion-test
	./$(BUILD)/qoi-fixtures-test
	./$(BUILD)/exception-test
	./$(BUILD)/paging-test
	./$(BUILD)/vmalloc-test

$(BUILD)/event-test: kernel/sys/event.c kernel/lib/string.c tests/event_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@
$(BUILD)/ipc-test: kernel/sys/ipc.c kernel/lib/string.c tests/ipc_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@
$(BUILD)/vfs-test: kernel/fs/vfs.c kernel/fs/path.c kernel/lib/string.c kernel/lib/format.c tests/vfs_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@
$(BUILD)/scheduler-test: kernel/sys/scheduler.c kernel/sys/ipc.c kernel/sys/event.c kernel/lib/string.c tests/scheduler_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@
$(BUILD)/ata-mock-test: kernel/drivers/ata.c kernel/fs/fs.c kernel/lib/string.c kernel/fs/path.c tests/ata_mock_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -DDENOS_TEST_IO -Ikernel/include $^ -o $@
$(BUILD)/calculator-test: kernel/lib/calculator.c kernel/lib/string.c tests/calculator_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@
$(BUILD)/system-core-test: kernel/lib/string.c kernel/lib/format.c kernel/lib/checksum.c kernel/fs/path.c kernel/sys/device.c kernel/core/boot_status.c kernel/core/log.c kernel/arch/x86_64/cpu.c kernel/mm/heap.c kernel/mm/heap_diag.c tests/system_core_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@
$(BUILD)/hardware-mock-test: kernel/drivers/pic.c kernel/drivers/rtc.c kernel/drivers/speaker.c tests/hardware_mock_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -DDENOS_TEST_IO -Ikernel/include $^ -o $@
$(BUILD)/pmm-test: kernel/mm/pmm.c kernel/lib/string.c tests/pmm_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -DDENOS_PMM_TESTING -Ikernel/include $^ -o $@
$(BUILD)/keyboard-poll-test: kernel/drivers/serial.c tests/keyboard_poll_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -DDENOS_TEST_IO -Ikernel/include $^ -o $@
$(BUILD)/net-protocol-test: kernel/net.c kernel/lib/string.c tests/net_protocol_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@
$(BUILD)/crypto-utils-test: kernel/lib/sha256.c kernel/lib/crc32.c kernel/lib/uuid.c kernel/core/entropy.c kernel/lib/string.c tests/crypto_utils_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@
$(BUILD)/parse-test: kernel/lib/parse.c tests/parse_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@
$(BUILD)/utf8-test: kernel/lib/utf8.c tests/utf8_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@
$(BUILD)/bitmap-ringbuf-test: kernel/lib/bitmap.c kernel/lib/ringbuf.c tests/bitmap_ringbuf_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@
$(BUILD)/env-history-test: kernel/sys/env.c kernel/sys/history.c kernel/lib/string.c tests/env_history_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@
$(BUILD)/bootargs-test: kernel/core/bootargs.c kernel/lib/string.c tests/bootargs_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@
$(BUILD)/partition-test: kernel/drivers/partition.c kernel/lib/string.c tests/partition_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@
$(BUILD)/elf64-test: kernel/exec/elf64.c tests/elf64_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@

$(BUILD)/kernel/%.o: kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/kernel/%.o: kernel/%.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@
$(BUILD)/denos.elf: $(OBJECTS) kernel/linker.ld
	$(LD) $(LDFLAGS) -Map=$(BUILD)/denos.map -o $@ $(OBJECTS)
$(BUILD)/denos.iso: $(BUILD)/denos.elf boot/grub.cfg
	@mkdir -p $(ISO_DIR)/boot/grub
	cp $(BUILD)/denos.elf $(ISO_DIR)/boot/denos.elf
	cp boot/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUB) -o $@ $(ISO_DIR)
-include $(DEPFILES)

$(BUILD)/service-test: kernel/sys/service.c kernel/lib/string.c tests/service_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@

# Aiden C compiler and AEX virtual machine tests.
$(BUILD)/compiler-vm-test: kernel/compiler.c kernel/vm.c tests/compiler_vm_test.c tests/fixtures/aex3-functions.h kernel/include/cvm.h
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -DVM_INSTRUCTION_BUDGET=4096 -Ikernel/include $^ -o $@

# Compile every bundled C example with the exact freestanding Aiden C subset.
$(BUILD)/compiler-app-test: kernel/compiler.c tests/compiler_apps_test.c kernel/include/cvm.h userland/apps/*.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include kernel/compiler.c tests/compiler_apps_test.c -o $@

# Preserved and expanded low-level graphics library regression tests.
$(BUILD)/graphics-test: kernel/drivers/graphics.c tests/graphics_test.c kernel/include/gfx.h kernel/include/io.h kernel/include/serial.h kernel/include/timer.h kernel/include/font_ui.h
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -DDENOS_TEST_IO -DDENOS_GRAPHICS_TESTING -Ikernel/include kernel/drivers/graphics.c tests/graphics_test.c -o $@

$(BUILD)/graphics-mouse-test: kernel/drivers/graphics.c kernel/drivers/mouse.c tests/graphics_mouse_test.c kernel/include/gfx.h kernel/include/mouse.h kernel/include/io.h kernel/include/serial.h kernel/include/timer.h kernel/include/font_ui.h
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -DDENOS_TEST_IO -DDENOS_GRAPHICS_TESTING -Ikernel/include kernel/drivers/graphics.c kernel/drivers/mouse.c tests/graphics_mouse_test.c -o $@

$(BUILD)/framebuffer-test: kernel/drivers/graphics.c kernel/drivers/mouse.c tests/framebuffer_test.c kernel/include/gfx.h kernel/include/mouse.h kernel/include/io.h kernel/include/serial.h kernel/include/timer.h kernel/include/font_ui.h
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -DDENOS_TEST_IO -DDENOS_GRAPHICS_TESTING -DDENOS_GRAPHICS_HIRES_TEST -Ikernel/include kernel/drivers/graphics.c kernel/drivers/mouse.c tests/framebuffer_test.c -o $@

$(BUILD)/qoi-test: kernel/lib/qoi.c tests/qoi_test.c kernel/include/qoi.h
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include kernel/lib/qoi.c tests/qoi_test.c -o $@

# Vim-style source-aware Aiden C completion tests.
$(BUILD)/completion-test: kernel/editor/completion.c tests/completion_test.c kernel/include/completion.h
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@

$(BUILD)/qoi-fixtures-test: kernel/lib/qoi.c tests/qoi_fixtures_test.c kernel/include/qoi.h resources/graphics-lab/fixtures/*.qoi resources/graphics-lab/fixtures/*.rgba
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -Ikernel/include kernel/lib/qoi.c tests/qoi_fixtures_test.c -o $@

.PHONY: qoi-fixtures-test
qoi-fixtures-test: $(BUILD)/qoi-fixtures-test
	./$(BUILD)/qoi-fixtures-test

# CPU exception-name and page-fault error-code regression tests.
$(BUILD)/exception-test: kernel/core/exception.c kernel/lib/string.c tests/exception_test.c kernel/include/exception.h
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@

exception-test: $(BUILD)/exception-test
	./$(BUILD)/exception-test


# Four-level x86-64 page-table mapping and huge-page split tests.
$(BUILD)/paging-test: kernel/mm/paging.c kernel/lib/string.c tests/paging_test.c kernel/include/paging.h
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -DDENOS_PAGING_TESTING -Ikernel/include $^ -o $@


# Run the page-table manager suite independently.
paging-test: $(BUILD)/paging-test
	./$(BUILD)/paging-test

# Fast freestanding syntax/diagnostic pass for every kernel C translation unit.
# This intentionally does not claim to assemble NASM sources or link/boot the ISO.
check-kernel-c:
	@set -eu; count=0; for source in $(C_SOURCES); do \
	  $(CC) $(filter-out -MMD -MP,$(CFLAGS)) -fsyntax-only "$$source"; \
	  count=$$((count + 1)); \
	done; echo "Freestanding kernel C check passed: $$count translation units"

boot-asm-check:
	python3 tests/check_boot_asm.py

check: test check-kernel-c libc-test boot-asm-check

# PMM-backed kernel virtual-allocation lifecycle and rollback tests.
$(BUILD)/vmalloc-test: kernel/mm/vmalloc.c kernel/lib/string.c tests/vmalloc_test.c kernel/include/vmalloc.h kernel/include/paging.h
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include kernel/mm/vmalloc.c kernel/lib/string.c tests/vmalloc_test.c -o $@

.PHONY: vmalloc-test
vmalloc-test: $(BUILD)/vmalloc-test
	./$(BUILD)/vmalloc-test


# Hosted userland support library regression suite. These modules are kept
# separate from the kernel until DenOS has a stable ring-3 syscall/runtime ABI.
LIBC_SOURCES := $(wildcard userland/libc/src/*.c)
$(BUILD)/userland-libc-test: $(LIBC_SOURCES) userland/libc/tests/libc_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Iuserland/libc/include $^ -lm -o $@

libc-test: $(BUILD)/userland-libc-test
	./$(BUILD)/userland-libc-test
