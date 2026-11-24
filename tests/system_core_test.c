#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "kstring.h"
#include "kformat.h"
#include "checksum.h"
#include "path.h"
#include "device.h"
#include "boot_status.h"
#include "memory.h"
#include "heap_diag.h"
#include "log.h"
#include "cpu.h"

static u64 fake_ticks;
static char serial_capture[2048];
static usize serial_capture_length;

u64 timer_ticks(void) { return fake_ticks; }
void serial_write(const char *text) {
    if (!text) return;
    while (*text && serial_capture_length + 1 < sizeof(serial_capture))
        serial_capture[serial_capture_length++] = *text++;
    serial_capture[serial_capture_length] = '\0';
}
void serial_putc(char character) {
    if (serial_capture_length + 1 < sizeof(serial_capture)) {
        serial_capture[serial_capture_length++] = character;
        serial_capture[serial_capture_length] = '\0';
    }
}

static int failures;
#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); \
        failures++; \
    } \
} while (0)

static void test_strings(void) {
    char text[16];
    char overlap[16] = "DenOS";
    CHECK(kstrlen("DenOS") == 5, "kstrlen counts characters");
    CHECK(kstrnlen("DenOS", 3) == 3, "kstrnlen respects its bound");
    CHECK(kstrcmp("alpha", "alpha") == 0, "kstrcmp equality");
    CHECK(kstrcmp("alpha", "beta") < 0, "kstrcmp ordering");
    CHECK(kstrncmp("kernel", "kenya", 2) == 0, "kstrncmp stops at limit");
    CHECK(kstrlcpy(text, "hello", sizeof(text)) == 5, "kstrlcpy returns source length");
    CHECK(strcmp(text, "hello") == 0, "kstrlcpy copies and terminates");
    CHECK(kstrlcpy(text, "abcdefghijklmnop", 5) == 16, "kstrlcpy reports truncation length");
    CHECK(strcmp(text, "abcd") == 0, "kstrlcpy truncates safely");
    kmemset(text, 'x', 3);
    text[3] = 0;
    CHECK(strcmp(text, "xxx") == 0, "kmemset fills bytes");
    kmemmove(overlap + 1, overlap, 5);
    CHECK(strcmp(overlap, "DDenOS") == 0, "kmemmove handles overlap");
    CHECK(kmemcmp("same", "same", 4) == 0, "kmemcmp equality");
    CHECK(kmemcmp("a", "b", 1) < 0, "kmemcmp ordering");
    CHECK(kstrchr("kernel", 'n') != 0, "kstrchr finds a character");
    CHECK(kstrrchr("a.b.c", '.') != 0 && kstrrchr("a.b.c", '.')[1] == 'c',
          "kstrrchr finds the final occurrence");
    CHECK(kstrstr("DenOS kernel", "kernel") != 0, "kstrstr finds a substring");
}

static void test_format(void) {
    char output[128];
    char tiny[5];
    int length = ksnprintf(output, sizeof(output), "%s %04d %08x %llu %c %%",
                           "boot", -23, 0x2Au, 1234567890123ull, 'Z');
    CHECK(strcmp(output, "boot -023 0000002a 1234567890123 Z %") == 0,
          "formatter handles strings, width, signed/unsigned integers and percent");
    CHECK(length == (int)strlen(output), "formatter returns characters that would be written");
    length = ksnprintf(tiny, sizeof(tiny), "abcdef");
    CHECK(length == 6, "formatter reports required size after truncation");
    CHECK(strcmp(tiny, "abcd") == 0, "formatter always terminates a bounded buffer");
    ksnprintf(output, sizeof(output), "%+d %-4s", 7, "ok");
    CHECK(strcmp(output, "+7 ok  ") == 0, "formatter handles plus sign and left alignment");
    ksnprintf(output, sizeof(output), "%p", (void *)(usize)0x12u);
    CHECK(strcmp(output, "0x12") == 0, "formatter prints pointers as hexadecimal");
    ksnprintf(output, sizeof(output), "%018p", (void *)(usize)0x12u);
    CHECK(strcmp(output, "0x0000000000000012") == 0,
          "formatter applies pointer zero padding after the hexadecimal prefix");
    CHECK(ksnprintf(0, 0, "length:%d", 42) == 9,
          "formatter supports a null, zero-capacity sizing pass");
    ksnprintf(output, sizeof(output), "%zu", (usize)23u);
    CHECK(strcmp(output, "23") == 0, "formatter supports size_t-style values");
}

static void test_checksums(void) {
    CHECK(kcrc32("123456789", 9) == 0xCBF43926u, "CRC32 standard check vector");
    CHECK(kfnv1a32("hello", 5) == 0x4F9F2CABu, "FNV-1a standard check vector");
    CHECK(kcrc32(0, 0) == 0, "CRC32 empty input");
}

static void test_paths(void) {
    CHECK(fs_name_valid("hello.c"), "ordinary source filename accepted");
    CHECK(fs_name_valid(".profile"), "hidden filename accepted");
    CHECK(!fs_name_valid("../hello.c"), "parent traversal rejected");
    CHECK(!fs_name_valid("folder/file.c"), "directory separator rejected by flat filesystem");
    CHECK(!fs_name_valid("two words.c"), "space rejected by command-oriented filename model");
    CHECK(!fs_name_valid(""), "empty filename rejected");
    CHECK(!fs_name_valid("bad:name"), "colon rejected");
    CHECK(strcmp(fs_name_extension("main.c"), "c") == 0, "extension returned");
    CHECK(strcmp(fs_name_extension("README"), "") == 0, "extension missing safely represented");
    CHECK(fs_name_is_hidden(".config"), "hidden filename identified");
}

static void test_devices(void) {
    device_registry_init();
    CHECK(device_count() == 0, "device registry initializes empty");
    CHECK(device_register("VGA", "vga-text", DEVICE_CLASS_DISPLAY,
                          DEVICE_STATE_READY) == 0, "first device gets index zero");
    CHECK(device_register("ATA", "ide-pio", DEVICE_CLASS_STORAGE,
                          DEVICE_STATE_DEGRADED) == 1, "second device registered");
    CHECK(device_register("VGA", "vga-console", DEVICE_CLASS_DISPLAY,
                          DEVICE_STATE_READY) == 0, "duplicate registration updates in place");
    CHECK(device_count() == 2, "duplicate does not increase count");
    CHECK(strcmp(device_find("VGA")->driver, "vga-console") == 0,
          "updated driver stored");
    CHECK(device_set_state("ATA", DEVICE_STATE_READY) == 0, "device state update succeeds");
    CHECK(device_get(1)->state == DEVICE_STATE_READY, "state update is visible");
    CHECK(device_get(2) == 0, "out-of-range lookup rejected");
    CHECK(device_register("", "bad", DEVICE_CLASS_SYSTEM, DEVICE_STATE_READY) < 0,
          "empty device name rejected");

    device_registry_init();
    for (int i = 0; i < DEVICE_REGISTRY_CAP; i++) {
        char name[16];
        ksnprintf(name, sizeof(name), "device%d", i);
        CHECK(device_register(name, "test-driver", DEVICE_CLASS_SYSTEM,
                              DEVICE_STATE_READY) == i,
              "device registry accepts entries up to its documented capacity");
    }
    CHECK(device_count() == DEVICE_REGISTRY_CAP, "device registry capacity is fixed");
    CHECK(device_register("overflow", "test-driver", DEVICE_CLASS_SYSTEM,
                          DEVICE_STATE_READY) < 0,
          "device registry rejects an entry beyond capacity");
}

static void test_boot_status(void) {
    boot_status_init();
    fake_ticks = 12;
    CHECK(boot_status_add("heap ready", BOOT_RESULT_OK) == 0, "boot status first index");
    fake_ticks = 37;
    CHECK(boot_status_add("ATA fallback", BOOT_RESULT_WARN) == 1, "boot warning added");
    CHECK(boot_status_count() == 2, "boot record count");
    CHECK(boot_status_get(1)->tick == 37, "boot record captures timer tick");
    CHECK(strcmp(boot_result_name(BOOT_RESULT_WARN), "WARN") == 0,
          "boot result has readable label");
    CHECK(boot_status_get(-1) == 0, "negative boot record index rejected");
    boot_status_init();
    for (int i = 0; i < BOOT_STATUS_CAP; i++) {
        char label[BOOT_LABEL_CAP];
        ksnprintf(label, sizeof(label), "stage%d", i);
        CHECK(boot_status_add(label, BOOT_RESULT_OK) == i,
              "boot log accepts entries up to capacity");
    }
    CHECK(boot_status_add("overflow", BOOT_RESULT_OK) < 0,
          "boot log rejects overflow rather than corrupting memory");
}

static void test_logging(void) {
    serial_capture_length = 0;
    serial_capture[0] = '\0';
    fake_ticks = 42;
    klog_init();
    klog_set_level(KLOG_DEBUG);
    klogf(KLOG_WARN, "ATA probe returned %d", 3);
    CHECK(strstr(serial_capture, "[00000042] [WARN ] ATA probe returned 3") != 0,
          "kernel logger adds tick, severity, and formatted message");
    usize before = serial_capture_length;
    klog_set_level(KLOG_INFO);
    klog_write(KLOG_DEBUG, "this debug message must be filtered");
    CHECK(serial_capture_length == before, "kernel logger filters messages below its threshold");
    klog_write(KLOG_ERROR, "device failed");
    CHECK(strstr(serial_capture, "[ERROR] device failed") != 0,
          "kernel logger preserves error severity");
}

static void test_cpu_helpers(void) {
    char vendor[16];
    cpu_get_vendor(vendor, sizeof(vendor));
    CHECK(kstrlen(vendor) == 12, "CPUID vendor helper writes a terminated 12-byte vendor string");
    CHECK((cpu_read_flags() & 2u) != 0, "RFLAGS helper observes reserved bit one");
    cpu_pause();
}

static void test_heap(void) {
    heap_diagnostics_t stats;
    memory_init();
    CHECK(memory_is_healthy(), "fresh heap is healthy");
    void *first = kmalloc(17);
    CHECK(first != 0 && ((usize)first & 15u) == 0, "heap returns 16-byte aligned memory");
    CHECK(memory_used() == 32, "heap rounds allocations to alignment");

    void *zeroed = kcalloc(4, 8);
    CHECK(zeroed != 0, "kcalloc allocates a valid range");
    for (int i = 0; i < 32; i++) CHECK(((u8 *)zeroed)[i] == 0, "kcalloc zeroes memory");
    CHECK(kcalloc((usize)-1, 2) == 0, "kcalloc rejects multiplication overflow");
    CHECK(memory_used() == 64, "heap tracks two live aligned allocations");

    kfree(first);
    CHECK(memory_used() == 32 && memory_allocation_count() == 1,
          "kfree releases an allocation and updates live accounting");
    CHECK(memory_validate(), "heap remains structurally valid after freeing a block");

    void *grown = krealloc(zeroed, 160);
    CHECK(grown == zeroed, "krealloc grows into an adjacent free block in place");
    CHECK(((u8 *)grown)[0] == 0 && ((u8 *)grown)[31] == 0,
          "krealloc preserves the original payload");
    CHECK(memory_used() == 160, "krealloc updates allocated payload accounting");

    void *shrunk = krealloc(grown, 9);
    CHECK(shrunk == grown, "krealloc shrinks an allocation in place");
    CHECK(memory_used() == 16, "shrinking returns excess payload to the heap");
    CHECK(memory_validate(), "heap remains valid after realloc split");

    kfree(shrunk);
    CHECK(memory_used() == 0 && memory_allocation_count() == 0,
          "freeing the final object returns all payload capacity");
    CHECK(memory_largest_free_block() > 900u * 1024u,
          "coalescing restores one large free block");
    CHECK(memory_validate(), "adjacent free blocks coalesce into a valid heap");
    kfree(shrunk);
    CHECK(memory_invalid_frees() == 1, "double free is rejected and counted");
    kfree((void *)(usize)0x1234u);
    CHECK(memory_invalid_frees() == 2, "foreign pointers are rejected and counted");
    CHECK(!krealloc((void *)(usize)0x1234u, 64),
          "krealloc rejects pointers that do not belong to the kernel heap");
    CHECK(memory_validate(), "invalid free attempts do not corrupt heap metadata");

    void *from_null = krealloc(0, 24);
    CHECK(from_null != 0 && memory_used() == 32,
          "krealloc with a null pointer behaves like kmalloc");
    CHECK(krealloc(from_null, 0) == 0 && memory_used() == 0,
          "krealloc with a zero size frees the original block");

    CHECK(memory_diagnostics(&stats) == 0, "heap diagnostic query succeeds");
    CHECK(stats.healthy && stats.used_bytes == memory_used(), "heap stats agree with allocator");
    CHECK(stats.free_bytes == memory_available(), "heap reports actual reusable payload bytes");
    CHECK(stats.metadata_bytes + stats.free_bytes + stats.used_bytes == stats.total_bytes,
          "heap metadata, free payload, and used payload account for the full arena");
    CHECK(stats.live_allocations == 0 && stats.total_allocations >= 3,
          "heap diagnostics distinguish live from cumulative allocations");
    CHECK(memory_failed_allocations() == 2,
          "failed allocation counter tracks overflow and invalid realloc attempts");

    memory_init();
    {
        u8 *released = (u8 *)kmalloc(32);
        CHECK(released != 0, "heap allocates a use-after-free diagnostic block");
        if (released) {
            kfree(released);
            CHECK(memory_validate(), "freed payload is filled with the free poison pattern");
            released[0] = 0;
            CHECK(!memory_validate(), "heap validation detects a write into a freed block");
            memory_init();
            CHECK(memory_validate(), "heap can be reinitialized after poison detection");
        }
    }
}

static void test_heap_stress(void) {
    enum { SLOT_COUNT = 48, ITERATIONS = 1200 };
    void *slots[SLOT_COUNT] = {0};
    usize sizes[SLOT_COUNT] = {0};
    u8 patterns[SLOT_COUNT];
    u32 random_state = 0xC001D00Du;

    memory_init();
    for (int step = 0; step < ITERATIONS; step++) {
        usize slot;
        u32 action;
        usize requested;
        random_state = random_state * 1664525u + 1013904223u;
        slot = (usize)(random_state % SLOT_COUNT);
        action = (random_state >> 16) % 3u;
        requested = (usize)((random_state >> 5) % 3072u) + 1u;

        if (!slots[slot]) {
            void *allocation = kmalloc(requested);
            if (allocation) {
                slots[slot] = allocation;
                sizes[slot] = requested;
                patterns[slot] = (u8)(slot * 17u + (usize)step);
                for (usize i = 0; i < sizes[slot]; i++)
                    ((u8 *)slots[slot])[i] = patterns[slot];
            }
        } else if (action == 0u) {
            for (usize i = 0; i < sizes[slot]; i++)
                CHECK(((u8 *)slots[slot])[i] == patterns[slot],
                      "stress heap preserves data before freeing a block");
            kfree(slots[slot]);
            slots[slot] = 0;
            sizes[slot] = 0;
        } else {
            usize old_size = sizes[slot];
            usize preserve = old_size < requested ? old_size : requested;
            void *resized;
            for (usize i = 0; i < old_size; i++)
                CHECK(((u8 *)slots[slot])[i] == patterns[slot],
                      "stress heap preserves data before reallocation");
            resized = krealloc(slots[slot], requested);
            if (resized) {
                for (usize i = 0; i < preserve; i++)
                    CHECK(((u8 *)resized)[i] == patterns[slot],
                          "krealloc preserves the old bytes during stress");
                slots[slot] = resized;
                sizes[slot] = requested;
                for (usize i = preserve; i < requested; i++)
                    ((u8 *)slots[slot])[i] = patterns[slot];
            }
        }
        CHECK(memory_validate(), "heap block chain stays valid during randomized lifecycle stress");
    }

    for (usize slot = 0; slot < SLOT_COUNT; slot++) {
        if (slots[slot]) kfree(slots[slot]);
    }
    CHECK(memory_validate(), "heap remains valid after randomized cleanup");
    CHECK(memory_used() == 0 && memory_allocation_count() == 0,
          "stress cleanup releases every live allocation");
    CHECK(memory_largest_free_block() > 900u * 1024u,
          "stress cleanup coalesces the arena into one large free block");
}

int main(void) {
    test_strings();
    test_format();
    test_checksums();
    test_paths();
    test_devices();
    test_boot_status();
    test_logging();
    test_cpu_helpers();
    test_heap();
    test_heap_stress();
    if (failures) {
        fprintf(stderr, "%d system-core test(s) failed.\n", failures);
        return 1;
    }
    puts("System core tests passed: strings/memory, bounded formatting, checksums, safe paths, device registry, boot diagnostics, logging, CPU helpers, heap lifecycle, and randomized allocator stress.");
    return 0;
}
