# DenOS 0.5.7 — the normal user workflow is: make build, make run, make clean.
.DEFAULT_GOAL := build
.DELETE_ON_ERROR:

CC := gcc
LD := ld
NASM := nasm
GRUB := grub-mkrescue
QEMU := qemu-system-x86_64
AR := ar
PYTHON := python3
VERSION := 0.5.7

# HTTPS is enabled in the normal build. Set TLS=0 only for a minimal HTTP-only image.
TLS ?= 1
BEARSSL_DIR ?= third_party/BearSSL
TLS_CA_BUNDLE ?=
QEMU_DISPLAY ?= gtk

BUILD := $(if $(filter 1,$(TLS)),build-tls,build)
ISO_DIR := $(BUILD)/iso
DATA_IMG := denos-data.img
DATA_MIN_BYTES := 16845312
DATA_CREATE_BYTES := 33554432

CFLAGS := -MMD -MP -std=gnu11 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
          -fno-builtin -fno-asynchronous-unwind-tables -fno-unwind-tables \
          -m64 -mno-red-zone -mcmodel=small -fno-omit-frame-pointer \
          -Wall -Wextra -Werror -O2 -Ikernel/include
LDFLAGS := -m elf_x86_64 -nostdlib -z max-page-size=0x1000 -T kernel/linker.ld

# BearSSL must use freestanding flags. In particular, do not allow hosted
# time() or fortified glibc symbols into the kernel.
BEARSSL_CFLAGS := -std=c99 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
                  -fno-builtin -m64 -mno-red-zone -mcmodel=small -O2 -w \
                  -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
                  -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_TIME=0

ifeq ($(TLS),1)
  CFLAGS += -DDENOS_ENABLE_TLS -I$(BEARSSL_DIR)/inc \
            -DDENOS_TLS_ROOTS_HEADER='"$(abspath $(BUILD)/tls_roots.c)"'
  BEARSSL_C_SOURCES := $(shell if [ -d "$(BEARSSL_DIR)/src" ]; then find "$(BEARSSL_DIR)/src" -name '*.c' -type f | sort; fi)
  BEARSSL_OBJECTS := $(patsubst $(BEARSSL_DIR)/src/%.c,$(BUILD)/bearssl/%.o,$(BEARSSL_C_SOURCES))
  BEARSSL_CFLAGS += -I$(BEARSSL_DIR)/inc -I$(BEARSSL_DIR)/src \
                    -DBR_USE_URANDOM=0 -DBR_USE_WIN32_RAND=0 -DBR_RDRAND=0
  TLS_ROOTS_SOURCE := $(BUILD)/tls_roots.c
  TLS_ARCHIVE := $(BUILD)/libbearssl-kernel.a
  OBJECTS_TLS := $(TLS_ARCHIVE)
else
  BEARSSL_OBJECTS :=
  OBJECTS_TLS :=
endif

C_SOURCES := $(shell find kernel -name '*.c' -type f | sort)
ASM_SOURCES := $(shell find kernel -name '*.asm' -type f | sort)
APP_SOURCES := $(shell find userland/apps -name '*.c' -type f | sort)
APP_EMBEDDED_SOURCE := $(BUILD)/apps_embedded.c
APP_EMBEDDED_OBJECT := $(BUILD)/apps_embedded.o
C_OBJECTS := $(patsubst kernel/%.c,$(BUILD)/kernel/%.o,$(C_SOURCES))
ASM_OBJECTS := $(patsubst kernel/%.asm,$(BUILD)/kernel/%.o,$(ASM_SOURCES))
OBJECTS := $(C_OBJECTS) $(ASM_OBJECTS) $(APP_EMBEDDED_OBJECT) $(OBJECTS_TLS)
DEPFILES := $(C_OBJECTS:.o=.d) $(APP_EMBEDDED_OBJECT:.o=.d)
KERNEL_ELF := $(BUILD)/denos.elf
KERNEL_ISO := $(BUILD)/denos.iso

.PHONY: build run clean

# Check tools before the recursive file build so missing dependencies produce
# a useful message instead of a cryptic "command not found" halfway through.
build:
	@set -eu; \
	if [ "$(TLS)" = 1 ] && [ ! -f "$(BEARSSL_DIR)/inc/bearssl.h" ]; then \
	  echo "ERROR: BearSSL source is missing at $(BEARSSL_DIR)." >&2; \
	  echo "Fetch it with: ./tools/fetch_bearssl.sh $(BEARSSL_DIR)" >&2; \
	  exit 1; \
	fi; \
	check_tool() { \
	  if ! command -v "$$1" >/dev/null 2>&1; then \
	    echo "ERROR: required build tool '$$1' is not installed." >&2; \
	    echo "See docs/BUILDING.md for the Debian/Ubuntu install command." >&2; \
	    exit 127; \
	  fi; \
	}; \
	check_tool "$(firstword $(CC))"; \
	check_tool "$(firstword $(LD))"; \
	check_tool "$(firstword $(NASM))"; \
	check_tool "$(firstword $(GRUB))"; \
	check_tool xorriso; \
	check_tool "$(firstword $(PYTHON))"; \
	if [ "$(TLS)" = 1 ]; then check_tool "$(firstword $(AR))"; fi
	@$(MAKE) --no-print-directory $(KERNEL_ISO)
	@if [ ! -e "$(DATA_IMG)" ]; then \
	  echo "Creating a new $(DATA_CREATE_BYTES)-byte DenOS data disk..."; \
	  temp="$(DATA_IMG).tmp.$$$$"; \
	  trap 'rm -f "$$temp"' EXIT HUP INT TERM; \
	  dd if=/dev/zero of="$$temp" bs=1M count=32 status=none; \
	  mv "$$temp" "$(DATA_IMG)"; \
	  trap - EXIT HUP INT TERM; \
	fi
	@echo "DenOS $(VERSION) build complete: $(KERNEL_ISO)"
	@echo "Persistent data disk: $(DATA_IMG)"

run:
	@set -eu; \
	if ! command -v "$(firstword $(QEMU))" >/dev/null 2>&1; then \
	  echo "ERROR: QEMU ('$(firstword $(QEMU))') is not installed." >&2; \
	  echo "Install qemu-system-x86, then run 'make run' again." >&2; \
	  exit 127; \
	fi
	@$(MAKE) --no-print-directory build
	@test -s "$(DATA_IMG)" || { echo "ERROR: $(DATA_IMG) is missing or empty." >&2; exit 1; }
	@disk_bytes=$$(wc -c < "$(DATA_IMG)"); \
	if [ "$$disk_bytes" -lt "$(DATA_MIN_BYTES)" ]; then \
	  echo "ERROR: $(DATA_IMG) is too small ($$disk_bytes bytes)." >&2; \
	  echo "Back it up, then enlarge it to 32 MiB with:" >&2; \
	  echo "  cp $(DATA_IMG) $(DATA_IMG).backup && truncate -s 32M $(DATA_IMG)" >&2; \
	  exit 1; \
	fi
	@echo "Starting DenOS in QEMU (display: $(QEMU_DISPLAY))."
	@echo "COM1 diagnostics: $(BUILD)/serial.log"
	$(QEMU) -name DenOS -m 512M -cdrom "$(KERNEL_ISO)" -boot order=d \
		-machine pc,accel=tcg -cpu qemu64,+rdrand -rtc base=utc -vga std \
		-display "$(QEMU_DISPLAY)" -monitor none \
		-drive file="$(abspath $(DATA_IMG))",format=raw,if=ide,index=0,media=disk,cache=writeback \
		-netdev user,id=net0 -device e1000,netdev=net0 \
		-serial file:$(BUILD)/serial.log -no-reboot -no-shutdown

clean:
	@echo "Removing generated DenOS build files (the persistent data disk is kept)."
	rm -rf build build-tls

# Rebuild objects when compiler/linker flags or source lists in this file change.
$(C_OBJECTS) $(ASM_OBJECTS) $(APP_EMBEDDED_OBJECT): Makefile
$(APP_EMBEDDED_SOURCE): Makefile

$(BUILD)/kernel/%.o: kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel/%.o: kernel/%.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

$(APP_EMBEDDED_SOURCE): $(APP_SOURCES) tools/embed_apps.py
	@mkdir -p $(BUILD)
	$(PYTHON) tools/embed_apps.py $@ $(APP_SOURCES)

$(APP_EMBEDDED_OBJECT): $(APP_EMBEDDED_SOURCE)
	$(CC) $(CFLAGS) -MMD -MP -MF $(APP_EMBEDDED_OBJECT:.o=.d) -c $< -o $@

ifeq ($(TLS),1)
$(BEARSSL_DIR)/build/brssl:
	$(MAKE) -C $(BEARSSL_DIR) build/brssl

$(TLS_ROOTS_SOURCE): $(BEARSSL_DIR)/build/brssl tools/generate_tls_roots.sh FORCE_TLS_ROOTS
	@mkdir -p $(BUILD)
	./tools/generate_tls_roots.sh "$(BEARSSL_DIR)" "$@" "$(TLS_CA_BUNDLE)"

$(BUILD)/kernel/net/tls.o: $(TLS_ROOTS_SOURCE)

.PHONY: FORCE_TLS_ROOTS
FORCE_TLS_ROOTS:

$(BUILD)/bearssl/%.o: $(BEARSSL_DIR)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BEARSSL_CFLAGS) -c $< -o $@

$(BEARSSL_OBJECTS): Makefile

$(TLS_ARCHIVE): $(BEARSSL_OBJECTS)
	$(AR) rcs $@ $^
endif

$(KERNEL_ELF): $(OBJECTS) kernel/linker.ld
	$(LD) $(LDFLAGS) -Map=$(BUILD)/denos.map -o $@ $(OBJECTS)

$(KERNEL_ISO): $(KERNEL_ELF) boot/grub.cfg
	@mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/denos.elf
	cp boot/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUB) -o $@ $(ISO_DIR)

-include $(DEPFILES)
