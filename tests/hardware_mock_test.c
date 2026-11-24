#include <stdio.h>
#include <string.h>
#include "io.h"
#include "pic.h"
#include "rtc.h"
#include "speaker.h"

static u8 port_values[65536];
static u8 cmos_registers[128];
static u8 cmos_index;
static u16 write_ports[256];
static u8 write_values[256];
static int write_count;

void denos_test_outb(u16 port, u8 value) {
    if (write_count < (int)(sizeof(write_ports) / sizeof(write_ports[0]))) {
        write_ports[write_count] = port;
        write_values[write_count] = value;
        write_count++;
    }
    if (port == 0x70) cmos_index = (u8)(value & 0x7Fu);
    else port_values[port] = value;
}

u8 denos_test_inb(u16 port) {
    if (port == 0x71) return cmos_registers[cmos_index];
    return port_values[port];
}

static int failures;
#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); \
        failures++; \
    } \
} while (0)

static void reset_mock(void) {
    memset(port_values, 0, sizeof(port_values));
    memset(cmos_registers, 0, sizeof(cmos_registers));
    memset(write_ports, 0, sizeof(write_ports));
    memset(write_values, 0, sizeof(write_values));
    write_count = 0;
    cmos_index = 0;
}

static void test_pic(void) {
    reset_mock();
    pic_init();
    CHECK(port_values[0x21] == 0xFE, "PIC leaves only PIT IRQ0 unmasked on master");
    CHECK(port_values[0xA1] == 0xFF, "PIC masks all slave IRQ lines");
    CHECK(pic_get_mask() == 0xFFFEu, "PIC mask value can be read as a 16-bit value");

    pic_clear_mask(1);
    CHECK((port_values[0x21] & 0x02u) == 0, "clear_mask unmasks a master IRQ");
    pic_set_mask(1);
    CHECK((port_values[0x21] & 0x02u) != 0, "set_mask masks a master IRQ");

    pic_clear_mask(10);
    CHECK((port_values[0xA1] & 0x04u) == 0, "clear_mask unmasks a slave IRQ");
    CHECK((port_values[0x21] & 0x04u) == 0, "unmasking a slave IRQ enables cascade IRQ2");
    pic_set_mask(10);
    CHECK((port_values[0xA1] & 0x04u) != 0, "set_mask masks a slave IRQ");

    write_count = 0;
    pic_send_eoi(10);
    CHECK(write_count == 2, "slave IRQ sends two EOI writes");
    CHECK(write_ports[0] == 0xA0 && write_values[0] == 0x20,
          "slave PIC receives EOI before master");
    CHECK(write_ports[1] == 0x20 && write_values[1] == 0x20,
          "master PIC receives EOI after slave");
    write_count = 0;
    pic_send_eoi(0);
    CHECK(write_count == 1 && write_ports[0] == 0x20,
          "master IRQ sends one EOI write");
}

static void test_rtc(void) {
    rtc_datetime_t now;
    reset_mock();
    cmos_registers[0x0A] = 0x00; /* UIP clear */
    cmos_registers[0x0B] = 0x06; /* binary values, 24-hour clock */
    cmos_registers[0x00] = 56;
    cmos_registers[0x02] = 34;
    cmos_registers[0x04] = 21;
    cmos_registers[0x07] = 22;
    cmos_registers[0x08] = 11;
    cmos_registers[0x09] = 26;
    cmos_registers[0x32] = 20;
    CHECK(rtc_read(&now) == 0, "RTC accepts a stable binary 24-hour sample");
    CHECK(now.year == 2026 && now.month == 11 && now.day == 22,
          "RTC decodes date and century");
    CHECK(now.hour == 21 && now.minute == 34 && now.second == 56,
          "RTC decodes time fields");

    reset_mock();
    cmos_registers[0x0A] = 0x00;
    cmos_registers[0x0B] = 0x00; /* BCD values, 12-hour clock */
    cmos_registers[0x00] = 0x05;
    cmos_registers[0x02] = 0x30;
    cmos_registers[0x04] = 0x89; /* 9 PM */
    cmos_registers[0x07] = 0x01;
    cmos_registers[0x08] = 0x12;
    cmos_registers[0x09] = 0x24;
    cmos_registers[0x32] = 0x20;
    CHECK(rtc_read(&now) == 0, "RTC accepts stable BCD 12-hour sample");
    CHECK(now.year == 2024 && now.hour == 21 && now.minute == 30,
          "RTC converts BCD and PM hour to 24-hour time");

    cmos_registers[0x04] = 0x09;
    cmos_registers[0x07] = 0x31;
    cmos_registers[0x08] = 0x02;
    cmos_registers[0x09] = 0x23;
    cmos_registers[0x32] = 0x20;
    CHECK(rtc_read(&now) < 0, "RTC rejects impossible calendar dates");

    cmos_registers[0x0A] = 0x80;
    CHECK(rtc_read(&now) < 0, "RTC update-in-progress timeout is bounded and reported");
}

static void test_speaker(void) {
    reset_mock();
    CHECK(speaker_play_tone(880) == 0, "speaker accepts a normal audible frequency");
    CHECK(port_values[0x43] == 0xB6, "speaker selects PIT channel 2 square-wave mode");
    CHECK((port_values[0x61] & 0x03u) == 0x03u, "speaker gate is enabled");
    speaker_stop();
    CHECK((port_values[0x61] & 0x03u) == 0, "speaker_stop disables the speaker gate");
    CHECK(speaker_play_tone(1) < 0, "speaker rejects out-of-range frequencies");
}

int main(void) {
    test_pic();
    test_rtc();
    test_speaker();
    if (failures) {
        fprintf(stderr, "%d hardware mock test(s) failed.\n", failures);
        return 1;
    }
    puts("Hardware mock tests passed: PIC remapping/masking/EOI, stable RTC sampling and time conversion, and PC speaker control.");
    return 0;
}
