
# DenOS 0.3.1 build: make, make run, make clean; make test runs host checks.

CC := gcc
LD := ld
NASM := nasm
QEMU := qemu-system-x86_64
GRUB := grub-mkrescue
PYTHON := python3
VERSION := 0.3.1

BUILD := build
ISO_DIR := $(BUILD)/iso

DATA_IMG := denos-data.img
DATA_VDI := denos-data.vdi
DATA_MIN_BYTES := 790528

CFLAGS := -MMD -MP -std=gnu11 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
          -fno-builtin -fno-asynchronous-unwind-tables -fno-unwind-tables \
          -m64 -mno-red-zone -mcmodel=small -Wall -Wextra -Werror -O2 \
          -Ikernel/include

LDFLAGS := -m elf_x86_64 -nostdlib -z max-page-size=0x1000 -T kernel/linker.ld

C_SOURCES := $(shell find kernel -name '*.c' -type f)
ASM_SOURCES := $(shell find kernel -name '*.asm' -type f)
APP_SOURCES := $(wildcard userland/apps/*.c)

C_OBJECTS := $(patsubst kernel/%.c,$(BUILD)/kernel/%.o,$(C_SOURCES))
ASM_OBJECTS := $(patsubst kernel/%.asm,$(BUILD)/kernel/%.o,$(ASM_SOURCES))

OBJECTS := $(C_OBJECTS) $(ASM_OBJECTS) $(BUILD)/embedded_apps.o
DEPFILES := $(C_OBJECTS:.o=.d) $(BUILD)/embedded_apps.d

.PHONY: all clean run test
.DEFAULT_GOAL := all

# Default build
all: $(BUILD)/denos.iso
	@if [ ! -f $(DATA_IMG) ]; then \
		echo "Creating persistent 8 MiB DenOS data disk..."; \
		dd if=/dev/zero of=$(DATA_IMG) bs=512 count=16384 status=none; \
	fi
	@echo "DenOS $(VERSION) build complete!"

# Remove build files AND persistent disk images
clean:
	@echo "Cleaning DenOS..."
	rm -rf $(BUILD)
	rm -f $(DATA_IMG) $(DATA_VDI)
	@echo "DenOS $(VERSION) clean complete!"

# Attach the writable data image explicitly to the PC's primary IDE master.
# Using an explicit ide-hd device makes the bus/unit placement unambiguous.
run: all
	@test -s $(DATA_IMG) || { echo "ERROR: $(DATA_IMG) is missing or empty."; exit 1; }
	@disk_bytes=$$(wc -c < $(DATA_IMG)); \
	  if [ "$$disk_bytes" -lt $(DATA_MIN_BYTES) ]; then \
	    echo "ERROR: $(DATA_IMG) is too small ($$disk_bytes bytes)."; \
	    echo "DenOS needs at least $(DATA_MIN_BYTES) bytes for its filesystem."; \
	    exit 1; \
	  fi
	@echo "DenOS ISO: $(BUILD)/denos.iso"
	@echo "DenOS data disk: $(DATA_IMG) ($$(wc -c < $(DATA_IMG)) bytes)"
	$(QEMU) -name DenOS -m 512M -cdrom $(BUILD)/denos.iso -boot order=d \
		-machine pc,accel=tcg -cpu qemu64 -vga std -display gtk -monitor none \
		-drive if=none,id=denos_data,file=$(DATA_IMG),format=raw,cache=writeback \
		-device ide-hd,drive=denos_data,bus=ide.0,unit=0 \
		-serial file:$(BUILD)/serial.log -no-reboot -no-shutdown

# Host-side driver test: exercises IDENTIFY and sector PIO without booting QEMU.
test: $(BUILD)/ata-mock-test $(BUILD)/vm-test
	./$(BUILD)/ata-mock-test
	./$(BUILD)/vm-test

$(BUILD)/ata-mock-test: kernel/drivers/ata.c kernel/fs/fs.c tests/ata_mock_test.c kernel/include/ata.h kernel/include/fs.h kernel/include/io.h kernel/include/apps.h kernel/include/types.h
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -DDENOS_TEST_IO -Ikernel/include \
		kernel/drivers/ata.c kernel/fs/fs.c tests/ata_mock_test.c -o $@

# Compile C kernel files
$(BUILD)/kernel/%.o: kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble ASM kernel files
$(BUILD)/kernel/%.o: kernel/%.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

# Embed user applications
$(BUILD)/embedded_apps.c: $(APP_SOURCES) tools/embed_apps.py
	@mkdir -p $(BUILD)
	$(PYTHON) tools/embed_apps.py $@ $(APP_SOURCES)

$(BUILD)/embedded_apps.o: $(BUILD)/embedded_apps.c
	$(CC) $(CFLAGS) -c $< -o $@

# Link kernel
$(BUILD)/denos.elf: $(OBJECTS) kernel/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

# Create bootable ISO
$(BUILD)/denos.iso: $(BUILD)/denos.elf boot/grub.cfg
	@mkdir -p $(ISO_DIR)/boot/grub
	cp $(BUILD)/denos.elf $(ISO_DIR)/boot/denos.elf
	cp boot/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUB) -o $@ $(ISO_DIR)

# Host-side compiler/VM tests. A lower budget keeps the endless-loop test quick.
$(BUILD)/vm-test: kernel/compiler.c kernel/vm.c tests/vm_test.c kernel/include/cvm.h kernel/include/fs.h kernel/include/serial.h kernel/include/types.h
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -DVM_INSTRUCTION_BUDGET=4096 -Ikernel/include \
		kernel/compiler.c kernel/vm.c tests/vm_test.c -o $@

# Rebuild C objects when a shared header changes.
-include $(DEPFILES)
