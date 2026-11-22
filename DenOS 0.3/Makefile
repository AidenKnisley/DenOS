
# DenOS: only three public commands: make clean, make, make run.

CC := gcc
LD := ld
NASM := nasm
QEMU := qemu-system-x86_64
GRUB := grub-mkrescue
PYTHON := python3

BUILD := build
ISO_DIR := $(BUILD)/iso

DATA_IMG := denos-data.img
DATA_VDI := denos-data.vdi

CFLAGS := -std=gnu11 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
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

.PHONY: all clean run
.DEFAULT_GOAL := all

# Default build
all: $(BUILD)/denos.iso
	@if [ ! -f $(DATA_IMG) ]; then \
		echo "Creating persistent 8 MiB DenOS data disk..."; \
		dd if=/dev/zero of=$(DATA_IMG) bs=512 count=16384 status=none; \
	fi
	@echo "DenOS build complete!"

# Remove build files AND persistent disk images
clean:
	@echo "Cleaning DenOS..."
	rm -rf $(BUILD)
	rm -f $(DATA_IMG) $(DATA_VDI)
	@echo "DenOS clean complete!"

# Build and launch
run: all
	$(QEMU) -name DenOS -m 512M -cdrom $(BUILD)/denos.iso -boot d \
		-machine pc -cpu qemu64 -vga std -display gtk -monitor none \
		-drive file=$(DATA_IMG),format=raw,if=ide,index=0 \
		-serial file:$(BUILD)/serial.log -no-reboot -no-shutdown

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
