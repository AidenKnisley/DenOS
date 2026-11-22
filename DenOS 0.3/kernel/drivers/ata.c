#include "ata.h"
#include "io.h"

#define ATA_DATA       0x1F0
#define ATA_ERROR      0x1F1
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA0       0x1F3
#define ATA_LBA1       0x1F4
#define ATA_LBA2       0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7
#define ATA_ALTSTATUS  0x3F6

#define ATA_SR_BSY 0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF  0x20
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

#define ATA_CMD_READ  0x20
#define ATA_CMD_WRITE 0x30
#define ATA_CMD_FLUSH 0xE7
#define ATA_TIMEOUT   1000000u

static int ata_available;

static void ata_delay_400ns(void) {
    (void)inb(ATA_ALTSTATUS);
    (void)inb(ATA_ALTSTATUS);
    (void)inb(ATA_ALTSTATUS);
    (void)inb(ATA_ALTSTATUS);
}

static int wait_not_busy(void) {
    for (u32 i = 0; i < ATA_TIMEOUT; i++) {
        u8 status = inb(ATA_STATUS);
        if (status == 0x00 || status == 0xFF) return -1;
        if (!(status & ATA_SR_BSY)) {
            if (status & (ATA_SR_ERR | ATA_SR_DF)) return -1;
            return 0;
        }
        __asm__ volatile("pause");
    }
    return -1;
}

static int wait_data_request(void) {
    for (u32 i = 0; i < ATA_TIMEOUT; i++) {
        u8 status = inb(ATA_STATUS);
        if (status == 0x00 || status == 0xFF) return -1;
        if (status & (ATA_SR_ERR | ATA_SR_DF)) return -1;
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) return 0;
        __asm__ volatile("pause");
    }
    return -1;
}

static void select_lba(u32 lba) {
    outb(ATA_DRIVE, (u8)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_delay_400ns();
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA0, (u8)(lba & 0xFF));
    outb(ATA_LBA1, (u8)((lba >> 8) & 0xFF));
    outb(ATA_LBA2, (u8)((lba >> 16) & 0xFF));
}

int ata_init(void) {
    ata_available = 0;
    outb(ATA_DRIVE, 0xE0);
    ata_delay_400ns();
    u8 status = inb(ATA_STATUS);
    if (status == 0x00 || status == 0xFF) return -1;
    if (wait_not_busy() < 0) return -1;
    status = inb(ATA_STATUS);
    if (status == 0x00 || status == 0xFF) return -1;
    ata_available = 1;
    return 0;
}

int ata_is_available(void) {
    return ata_available;
}

int ata_read_sector(u32 lba, void *buffer) {
    if (!ata_available || !buffer || lba >= 0x10000000u) return -1;
    if (wait_not_busy() < 0) return -1;
    select_lba(lba);
    outb(ATA_COMMAND, ATA_CMD_READ);
    if (wait_data_request() < 0) return -1;

    u8 *bytes = (u8 *)buffer;
    for (u32 i = 0; i < 256; i++) {
        u16 value = inw(ATA_DATA);
        bytes[i * 2] = (u8)(value & 0xFF);
        bytes[i * 2 + 1] = (u8)(value >> 8);
    }
    ata_delay_400ns();
    return wait_not_busy();
}

int ata_write_sector(u32 lba, const void *buffer) {
    if (!ata_available || !buffer || lba >= 0x10000000u) return -1;
    if (wait_not_busy() < 0) return -1;
    select_lba(lba);
    outb(ATA_COMMAND, ATA_CMD_WRITE);
    if (wait_data_request() < 0) return -1;

    const u8 *bytes = (const u8 *)buffer;
    for (u32 i = 0; i < 256; i++) {
        u16 value = (u16)bytes[i * 2] | ((u16)bytes[i * 2 + 1] << 8);
        outw(ATA_DATA, value);
    }
    ata_delay_400ns();
    return wait_not_busy();
}

int ata_flush(void) {
    if (!ata_available || wait_not_busy() < 0) return -1;
    outb(ATA_COMMAND, ATA_CMD_FLUSH);
    return wait_not_busy();
}
