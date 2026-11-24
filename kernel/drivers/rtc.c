#include "rtc.h"
#include "io.h"

static u8 cmos_read(u8 reg) {
    outb(0x70, (u8)(reg & 0x7F));
    return inb(0x71);
}

static int wait_for_update(void) {
    for (u32 wait = 0; wait < 100000u; wait++) {
        if (!(cmos_read(0x0A) & 0x80)) return 0;
    }
    return -1;
}

static u8 from_bcd(u8 value) {
    return (u8)((value & 0x0Fu) + ((value >> 4) * 10u));
}

static int sample_clock(u8 values[7], u8 *status_b) {
    if (wait_for_update() < 0) return -1;
    values[0] = cmos_read(0x00);
    values[1] = cmos_read(0x02);
    values[2] = cmos_read(0x04);
    values[3] = cmos_read(0x07);
    values[4] = cmos_read(0x08);
    values[5] = cmos_read(0x09);
    values[6] = cmos_read(0x32);
    *status_b = cmos_read(0x0B);
    return wait_for_update();
}

int rtc_read(rtc_datetime_t *result) {
    u8 first[7], second[7], status_b = 0, status_unused = 0;
    if (!result) return -1;
    if (sample_clock(first, &status_b) < 0 ||
        sample_clock(second, &status_unused) < 0) return -1;

    int stable = status_b == status_unused;
    for (int i = 0; i < 7; i++) if (first[i] != second[i]) stable = 0;
    if (!stable) return -1;

    u8 hour_pm = (u8)(second[2] & 0x80u);
    if (!(status_b & 0x04u)) {
        second[0] = from_bcd(second[0]);
        second[1] = from_bcd(second[1]);
        second[2] = (u8)(from_bcd((u8)(second[2] & 0x7Fu)) | hour_pm);
        second[3] = from_bcd(second[3]);
        second[4] = from_bcd(second[4]);
        second[5] = from_bcd(second[5]);
        second[6] = from_bcd(second[6]);
    }

    if (!(status_b & 0x02u)) {
        u8 hour = (u8)(second[2] & 0x7Fu);
        if (hour == 12) hour = 0;
        if (hour_pm) hour = (u8)(hour + 12);
        second[2] = hour;
    }

    u16 year;
    if (second[6] >= 19 && second[6] <= 99)
        year = (u16)(second[6] * 100u + second[5]);
    else
        year = (u16)(second[5] >= 70 ? 1900u + second[5] : 2000u + second[5]);

    if (second[0] > 59 || second[1] > 59 || second[2] > 23 ||
        second[3] < 1 || second[4] < 1 || second[4] > 12 ||
        year < 1970 || year > 9999)
        return -1;

    u8 days_in_month;
    switch (second[4]) {
        case 2: {
            int leap_year = (year % 4u == 0u) &&
                            ((year % 100u != 0u) || (year % 400u == 0u));
            days_in_month = (u8)(leap_year ? 29 : 28);
            break;
        }
        case 4: case 6: case 9: case 11: days_in_month = 30; break;
        default: days_in_month = 31; break;
    }
    if (second[3] > days_in_month) return -1;

    result->second = second[0];
    result->minute = second[1];
    result->hour = second[2];
    result->day = second[3];
    result->month = second[4];
    result->year = year;
    return 0;
}
