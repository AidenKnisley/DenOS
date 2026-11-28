# DenOS host regression and static-check targets.
# Run from the project root with: make -f tests/Makefile test

CC := gcc
BUILD := build
CFLAGS := -MMD -MP -std=gnu11 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
          -fno-builtin -fno-asynchronous-unwind-tables -fno-unwind-tables \
          -m64 -mno-red-zone -mcmodel=small -fno-omit-frame-pointer -Wall -Wextra -Werror -O2 \
          -Ikernel/include
BEARSSL_CFLAGS := -std=c99 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
                  -fno-builtin -m64 -mno-red-zone -mcmodel=small -O2 -w \
                  -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
                  -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_TIME=0
APP_SOURCES := $(shell find userland/apps -name '*.c' -type f | sort)
APP_EMBEDDED_SOURCE := $(BUILD)/apps_embedded.c
C_SOURCES := $(shell find kernel -name '*.c' -type f | sort)

.PHONY: test check check-kernel-c check-stack-usage exception-test paging-test \
        vmalloc-test qoi-fixtures-test tls-adapter-check bearssl-freestanding-check \
        boot-asm-check libc-test

test: $(BUILD)/event-test $(BUILD)/ipc-test $(BUILD)/vfs-test $(BUILD)/scheduler-test \
      $(BUILD)/compiler-vm-test $(BUILD)/compiler-app-test \
      $(BUILD)/ata-mock-test $(BUILD)/calculator-test $(BUILD)/system-core-test \
      $(BUILD)/hardware-mock-test $(BUILD)/pmm-test $(BUILD)/keyboard-poll-test \
      $(BUILD)/net-protocol-test $(BUILD)/net-transport-test $(BUILD)/e1000-ring-test $(BUILD)/crypto-utils-test $(BUILD)/cpu-random-test $(BUILD)/tls-adapter-test $(BUILD)/parse-test \
      $(BUILD)/utf8-test $(BUILD)/bitmap-ringbuf-test $(BUILD)/env-history-test \
      $(BUILD)/bootargs-test $(BUILD)/partition-test $(BUILD)/elf64-test $(BUILD)/service-test \
      $(BUILD)/graphics-test $(BUILD)/graphics-mouse-test $(BUILD)/framebuffer-test $(BUILD)/qoi-test $(BUILD)/png-test \
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
	./$(BUILD)/net-transport-test
	./$(BUILD)/e1000-ring-test
	./$(BUILD)/crypto-utils-test
	./$(BUILD)/cpu-random-test
	./$(BUILD)/tls-adapter-test
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
	./$(BUILD)/png-test
	./$(BUILD)/completion-test
	./$(BUILD)/qoi-fixtures-test
	./$(BUILD)/exception-test
	./$(BUILD)/paging-test
	./$(BUILD)/vmalloc-test
	@python3 tests/embed_apps_test.py
	@./tests/tls_roots_test.sh

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
$(BUILD)/net-protocol-test: kernel/net.c kernel/net/udp.c kernel/net/dns.c kernel/net/dhcp.c kernel/net/tcp.c kernel/net/http.c kernel/net/tls.c kernel/arch/x86_64/cpu.c kernel/lib/string.c kernel/lib/format.c kernel/lib/parse.c kernel/core/entropy.c tests/net_protocol_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -DDENOS_HTTP_TEST_MODE -Ikernel/include $^ -o $@
$(BUILD)/net-transport-test: kernel/net.c kernel/net/udp.c kernel/net/dns.c kernel/net/dhcp.c kernel/net/tcp.c kernel/net/http.c kernel/net/tls.c kernel/arch/x86_64/cpu.c kernel/lib/string.c kernel/lib/format.c kernel/lib/parse.c kernel/core/entropy.c tests/net_transport_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -DDENOS_HTTP_TEST_MODE -Ikernel/include $^ -o $@
$(BUILD)/e1000-ring-test: tests/e1000_ring_test.c kernel/include/e1000_ring.h
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $< -o $@
$(BUILD)/crypto-utils-test: kernel/lib/sha256.c kernel/lib/crc32.c kernel/lib/uuid.c kernel/core/entropy.c kernel/lib/string.c tests/crypto_utils_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@
$(BUILD)/cpu-random-test: kernel/arch/x86_64/cpu.c kernel/lib/string.c tests/cpu_random_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@
$(BUILD)/tls-adapter-test: kernel/net/tls.c kernel/lib/string.c kernel/lib/format.c tests/tls_adapter_test.c tests/stubs/bearssl.h tests/stubs/tls_roots.h
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include -Itests/stubs \
	  -DDENOS_ENABLE_TLS -DDENOS_TLS_ROOTS_HEADER='"$(abspath tests/stubs/tls_roots.h)"' \
	  kernel/net/tls.c kernel/lib/string.c kernel/lib/format.c tests/tls_adapter_test.c -o $@
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


$(BUILD)/service-test: kernel/sys/service.c kernel/lib/string.c tests/service_test.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include $^ -o $@

# Aiden C compiler and AEX virtual machine tests.
$(BUILD)/compiler-vm-test: kernel/compiler.c kernel/vm.c tests/compiler_vm_test.c tests/fixtures/aex3-functions.h kernel/include/cvm.h
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -DVM_INSTRUCTION_BUDGET=4096 -Ikernel/include $^ -o $@

# Compile every bundled C example with the exact freestanding Aiden C subset.
$(BUILD)/compiler-app-test: kernel/compiler.c tests/compiler_apps_test.c kernel/include/cvm.h $(shell find userland/apps -name '*.c' -type f)
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


# Bounded PNG decoder tests, including all filters and all DEFLATE block types.
$(BUILD)/png-test: kernel/lib/png.c kernel/lib/crc32.c tests/png_test.c kernel/include/png.h tests/fixtures/png/*.png
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Ikernel/include kernel/lib/png.c kernel/lib/crc32.c tests/png_test.c -o $@

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
check-kernel-c: $(APP_EMBEDDED_SOURCE)
	@set -eu; count=0; for source in $(C_SOURCES) $(APP_EMBEDDED_SOURCE); do \
	  $(CC) $(filter-out -MMD -MP,$(CFLAGS)) -fsyntax-only "$$source"; \
	  count=$$((count + 1)); \
	done; echo "Freestanding kernel C check passed: $$count translation units"

boot-asm-check:
	python3 tests/check_boot_asm.py

# Compile every kernel C unit with GCC stack-usage reporting. Reject unknown
# dynamic frames and any single frame above half of the 16 KiB bootstrap stack.
check-stack-usage: $(APP_EMBEDDED_SOURCE)
	CC="$(CC)" CFLAGS="$(filter-out -MMD -MP,$(CFLAGS))" tools/check_stack_usage.sh $(C_SOURCES) $(APP_EMBEDDED_SOURCE)

tls-adapter-check:
	$(CC) $(filter-out -MMD -MP -DDENOS_TLS_ROOTS_HEADER=%,$(CFLAGS)) -DDENOS_ENABLE_TLS -Itests/stubs \
	  -DDENOS_TLS_ROOTS_HEADER='"$(abspath tests/stubs/tls_roots.h)"' \
	  -fsyntax-only kernel/net/tls.c

# Reproduce the exact class of failure seen when a fortified host string.h
# emits __memcpy_chk for a fixed-size local destination in BearSSL code.
# This uses BEARSSL_CFLAGS itself, so the test cannot silently drift away
# from the flags used for third_party/BearSSL objects.
$(BUILD)/bearssl-fortify-check.c: tests/Makefile
	@mkdir -p $(BUILD)
	@printf '%s\n' '#include <stddef.h>' '#include <string.h>' \
	  'unsigned char denos_fortify_probe(const unsigned char *src, size_t len) {' \
	  '  unsigned char dst[16];' \
	  '  memcpy(dst, src, len);' \
	  '  return dst[0];' '}' > $@

$(BUILD)/bearssl-fortify-check.o: $(BUILD)/bearssl-fortify-check.c tests/Makefile
	$(CC) -D_FORTIFY_SOURCE=2 $(BEARSSL_CFLAGS) -c $< -o $@
	@if nm -u $@ | grep -q '__memcpy_chk'; then \
	  echo 'ERROR: BearSSL freestanding flags still emit __memcpy_chk.' >&2; exit 1; \
	fi
	@echo 'BearSSL freestanding fortify check passed (no __memcpy_chk reference).'

# BearSSL can autodetect a Unix host and compile x509_minimal.c to call
# time(). DenOS passes the hardware RTC value explicitly with
# br_x509_minimal_set_time(), so host OS clock backends must be disabled.
$(BUILD)/bearssl-time-check.c: tests/Makefile
	@mkdir -p $(BUILD)
	@printf '%s\n' \
	  '#if !defined(BR_USE_UNIX_TIME) || BR_USE_UNIX_TIME != 0' \
	  '#error "BearSSL Unix time backend must be disabled for DenOS"' \
	  '#endif' \
	  '#if !defined(BR_USE_WIN32_TIME) || BR_USE_WIN32_TIME != 0' \
	  '#error "BearSSL Win32 time backend must be disabled for DenOS"' \
	  '#endif' \
	  '#if BR_USE_UNIX_TIME' \
	  'extern long time(long *);' \
	  'long denos_time_probe(void) { return time(0); }' \
	  '#else' \
	  'long denos_time_probe(void) { return 0; }' \
	  '#endif' > $@

$(BUILD)/bearssl-time-check.o: $(BUILD)/bearssl-time-check.c tests/Makefile
	$(CC) $(BEARSSL_CFLAGS) -c $< -o $@
	@if nm -u $@ | grep -Eq '[[:space:]]time$$'; then \
		echo 'ERROR: BearSSL time probe still references the host time() function.' >&2; exit 1; \
	fi
	@echo 'BearSSL RTC-only time configuration check passed (no host time() reference).'

bearssl-freestanding-check: $(BUILD)/bearssl-fortify-check.o $(BUILD)/bearssl-time-check.o
check: test check-kernel-c check-stack-usage tls-adapter-check bearssl-freestanding-check libc-test boot-asm-check

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
$(APP_EMBEDDED_SOURCE): $(APP_SOURCES) tools/embed_apps.py
	@mkdir -p $(BUILD)
	python3 tools/embed_apps.py $@ $(APP_SOURCES)

