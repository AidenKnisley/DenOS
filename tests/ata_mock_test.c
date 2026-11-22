#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ata.h"
#include "fs.h"
#include "apps.h"
#include "serial.h"

#define PRIMARY_BASE   0x1F0
#define PRIMARY_CTRL   0x3F6
#define SECONDARY_BASE 0x170
#define SECONDARY_CTRL 0x376

#define MOCK_SECTORS 16384u
#define MOCK_STATUS_READY 0x50
#define MOCK_STATUS_DRQ   0x58

enum mock_mode {
    MOCK_IDLE,
    MOCK_IDENTIFY,
    MOCK_READ,
    MOCK_WRITE
};

static unsigned char mock_disk[MOCK_SECTORS][512];
static unsigned short identify_words[256];
static unsigned char status_register = MOCK_STATUS_READY;
static unsigned char drive_head = 0xA0;
static unsigned char sector_count;
static unsigned char lba0;
static unsigned char lba1;
static unsigned char lba2;
static unsigned int active_lba;
static unsigned int word_position;
static enum mock_mode mode;
static int disk_attached = 1;
static unsigned int disk_channel = 0;
static unsigned int disk_slave = 0;
static unsigned int selected_channel = 0;
static unsigned int flush_commands;
static unsigned int busy_reads_left;

/* The same mock device can be placed on either legacy IDE channel. */
static int decode_io(unsigned short port, unsigned int *channel,
                     unsigned int *reg) {
    if (port >= PRIMARY_BASE && port <= PRIMARY_BASE + 7) {
        *channel = 0;
        *reg = port - PRIMARY_BASE;
        return 1;
    }
    if (port >= SECONDARY_BASE && port <= SECONDARY_BASE + 7) {
        *channel = 1;
        *reg = port - SECONDARY_BASE;
        return 1;
    }
    return 0;
}

static int decode_control(unsigned short port, unsigned int *channel) {
    if (port == PRIMARY_CTRL) { *channel = 0; return 1; }
    if (port == SECONDARY_CTRL) { *channel = 1; return 1; }
    return 0;
}

/* Filesystem dependencies are stubbed; the real ATA and filesystem code run. */
void serial_write(const char *text) { (void)text; }
void apps_install(void) { }

static void prepare_identify(void) {
    const char model[40] = "DENOS TEST VIRTUAL DISK";
    memset(identify_words, 0, sizeof(identify_words));
    identify_words[49] = (1u << 9);       /* LBA supported. */
    identify_words[60] = (unsigned short)(MOCK_SECTORS & 0xFFFFu);
    identify_words[61] = (unsigned short)(MOCK_SECTORS >> 16);
    identify_words[83] = 0x4000u | (1u << 12); /* Valid; FLUSH CACHE. */

    for (unsigned int i = 0; i < 40; i += 2) {
        identify_words[27 + (i / 2)] =
            (unsigned short)(((unsigned short)(unsigned char)model[i] << 8) |
                             (unsigned char)model[i + 1]);
    }
}

void denos_test_outb(unsigned short port, unsigned char value) {
    unsigned int channel, reg;
    if (decode_io(port, &channel, &reg)) {
        if (reg == 6) {
            selected_channel = channel;
            drive_head = value;
            busy_reads_left = 0;
            if (!disk_attached || channel != disk_channel ||
                (((value & 0x10u) != 0) != (disk_slave != 0)))
                status_register = 0x00;
            else
                status_register = MOCK_STATUS_READY;
            return;
        }
        if (channel != selected_channel) return;
        if (reg == 2) { sector_count = value; return; }
        if (reg == 3) { lba0 = value; return; }
        if (reg == 4) { lba1 = value; return; }
        if (reg == 5) { lba2 = value; return; }
        if (reg != 7) return;

        if (value == 0xEC) {
            if (!disk_attached || channel != disk_channel ||
                (((drive_head & 0x10u) != 0) != (disk_slave != 0))) {
                status_register = 0x00;
                mode = MOCK_IDLE;
                return;
            }
            word_position = 0;
            mode = MOCK_IDENTIFY;
            status_register = MOCK_STATUS_DRQ;
            busy_reads_left = 2; /* Model a device that takes time to clear BSY. */
        } else if (value == 0x20) {
            active_lba = ((unsigned int)(drive_head & 0x0F) << 24) |
                         ((unsigned int)lba2 << 16) |
                         ((unsigned int)lba1 << 8) | lba0;
            word_position = 0;
            mode = MOCK_READ;
            status_register = active_lba < MOCK_SECTORS
                            ? MOCK_STATUS_DRQ : 0x01;
            busy_reads_left = active_lba < MOCK_SECTORS ? 2 : 0;
        } else if (value == 0x30) {
            active_lba = ((unsigned int)(drive_head & 0x0F) << 24) |
                         ((unsigned int)lba2 << 16) |
                         ((unsigned int)lba1 << 8) | lba0;
            word_position = 0;
            mode = MOCK_WRITE;
            status_register = active_lba < MOCK_SECTORS
                            ? MOCK_STATUS_DRQ : 0x01;
            busy_reads_left = active_lba < MOCK_SECTORS ? 2 : 0;
        } else if (value == 0xE7) {
            flush_commands++;
            status_register = MOCK_STATUS_READY;
            mode = MOCK_IDLE;
        }
        return;
    }
}

unsigned char denos_test_inb(unsigned short port) {
    unsigned int channel, reg;
    if (decode_io(port, &channel, &reg)) {
        if (channel != selected_channel || !disk_attached || channel != disk_channel ||
            (((drive_head & 0x10u) != 0) != (disk_slave != 0)))
            return 0x00;
        if (reg == 7) {
            if (busy_reads_left) {
                busy_reads_left--;
                return 0x80;
            }
            return status_register;
        }
        if (reg == 1) return 0;
        if (reg == 4 || reg == 5) return 0;
        return 0;
    }
    if (decode_control(port, &channel)) {
        return (disk_attached && channel == selected_channel && channel == disk_channel &&
                (((drive_head & 0x10u) != 0) == (disk_slave != 0)))
             ? status_register : 0x00;
    }
    return 0;
}

void denos_test_outw(unsigned short port, unsigned short value) {
    unsigned int channel, reg;
    if (!decode_io(port, &channel, &reg) || reg != 0 ||
        channel != selected_channel || !disk_attached || channel != disk_channel ||
        (((drive_head & 0x10u) != 0) != (disk_slave != 0)) || mode != MOCK_WRITE || active_lba >= MOCK_SECTORS)
        return;

    unsigned int byte = word_position * 2;
    mock_disk[active_lba][byte] = (unsigned char)(value & 0xFF);
    mock_disk[active_lba][byte + 1] = (unsigned char)(value >> 8);
    word_position++;

    if (word_position == 256) status_register = MOCK_STATUS_READY;
}

unsigned short denos_test_inw(unsigned short port) {
    unsigned int channel, reg;
    if (!decode_io(port, &channel, &reg) || reg != 0 ||
        channel != selected_channel || !disk_attached || channel != disk_channel ||
        (((drive_head & 0x10u) != 0) != (disk_slave != 0)) || word_position >= 256)
        return 0;

    unsigned short value = 0;
    if (mode == MOCK_IDENTIFY) {
        value = identify_words[word_position];
    } else if (mode == MOCK_READ && active_lba < MOCK_SECTORS) {
        unsigned int byte = word_position * 2;
        value = (unsigned short)mock_disk[active_lba][byte] |
                ((unsigned short)mock_disk[active_lba][byte + 1] << 8);
    }

    word_position++;
    if (word_position == 256) status_register = MOCK_STATUS_READY;
    return value;
}

static void fail(const char *message) {
    fprintf(stderr, "ATA mock test FAILED: %s\n", message);
    exit(1);
}

int main(void) {
    unsigned char written[512];
    unsigned char read_back[512];

    prepare_identify();
    memset(mock_disk, 0, sizeof(mock_disk));

    /* First test a disk on the primary IDE master. */
    disk_channel = 0;
    if (ata_init() != 0) fail("IDENTIFY DEVICE did not detect the primary mock disk");
    if (!ata_is_available()) fail("availability flag was not set");
    if (ata_sector_count() != MOCK_SECTORS) fail("wrong LBA sector capacity");
    if (strcmp(ata_model(), "DENOS TEST VIRTUAL DISK") != 0)
        fail("ATA model string was not decoded correctly");

    for (unsigned int i = 0; i < sizeof(written); i++)
        written[i] = (unsigned char)((i * 37u + 11u) & 0xFFu);

    if (ata_write_sector(1234, written) != 0)
        fail("sector write failed");
    if (ata_flush() != 0) fail("FLUSH CACHE failed");
    if (flush_commands != 1) fail("supported FLUSH CACHE command was not issued");

    memset(read_back, 0, sizeof(read_back));
    if (ata_read_sector(1234, read_back) != 0)
        fail("sector read failed");
    if (memcmp(written, read_back, sizeof(written)) != 0)
        fail("read data did not match written data");

    if (ata_read_sector(MOCK_SECTORS, read_back) == 0)
        fail("out-of-range read was accepted");
    if (ata_write_sector(MOCK_SECTORS, written) == 0)
        fail("out-of-range write was accepted");

    /* Also verify the primary IDE slave selection path. */
    disk_slave = 1;
    if (ata_init() != 0) fail("primary IDE slave disk was not detected");
    if (strcmp(ata_diagnostic(), "ATA disk detected on primary IDE slave") != 0)
        fail("primary IDE slave diagnostic was incorrect");

    disk_attached = 0;
    if (ata_init() == 0 || ata_is_available())
        fail("missing disk was incorrectly reported as present");

    /* Probe the secondary slave, then use the secondary master for filesystem I/O. */
    disk_attached = 1;
    disk_channel = 1;
    memset(mock_disk, 0, sizeof(mock_disk));
    if (ata_init() != 0) fail("secondary IDE slave disk was not detected");
    if (strcmp(ata_diagnostic(), "ATA disk detected on secondary IDE slave") != 0)
        fail("secondary IDE slave diagnostic was incorrect");

    disk_slave = 0;
    if (ata_init() != 0) fail("secondary IDE master disk was not detected");
    if (strcmp(ata_diagnostic(), "ATA disk detected on secondary IDE master") != 0)
        fail("secondary IDE master diagnostic was incorrect");

    /* Mount, save a file, reboot the filesystem, and prove it remains present. */
    fs_init();
    if (!fs_is_persistent()) fail("blank secondary-channel disk did not format and mount");
    if (fs_write("persist.txt", "hello from the ATA filesystem") != 0)
        fail("filesystem could not save a file");
    if (!fs_read("persist.txt") ||
        strcmp(fs_read("persist.txt"), "hello from the ATA filesystem") != 0)
        fail("saved file was not readable from the filesystem cache");

    fs_init();
    if (!fs_is_persistent()) fail("formatted disk failed to remount");
    if (!fs_read("persist.txt") ||
        strcmp(fs_read("persist.txt"), "hello from the ATA filesystem") != 0)
        fail("saved file did not survive filesystem remount");
    if (fs_storage_capacity() != (48u * 16383u))
        fail("filesystem usable capacity changed unexpectedly");
    if (fs_file_count() != 2u)
        fail("filesystem did not retain both welcome.txt and persist.txt");
    if (fs_storage_free() != (46u * 16383u))
        fail("free storage did not account for both allocated fixed file slots");
    if (fs_storage_reserved() != (2u * 16383u) -
        (strlen("Welcome to DenOS.\nYour files are stored on the ATA hard disk.\n") +
         strlen("hello from the ATA filesystem")))
        fail("reserved storage inside allocated file slots was reported incorrectly");
    if (fs_disk_size_bytes() != (usize)MOCK_SECTORS * 512u)
        fail("physical disk capacity reporting is incorrect");

    /* An unknown filesystem must not be reformatted or have data destroyed. */
    mock_disk[0][0] = 0x55;
    fs_init();
    if (fs_is_persistent()) fail("unknown filesystem was incorrectly mounted");
    if (mock_disk[0][0] != 0x55)
        fail("unknown disk contents were overwritten during probing");

    puts("DenOS storage tests passed: all four primary/secondary IDE master/slave positions, IDENTIFY, model/capacity, PIO read/write, flush, bounds, absent disk, filesystem format/remount, and non-destructive unknown-disk handling.");
    return 0;
}
