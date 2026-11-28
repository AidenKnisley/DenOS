#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ata.h"
#include "fs.h"
#include "serial.h"
#include "path.h"

#define PRIMARY_BASE   0x1F0
#define PRIMARY_CTRL   0x3F6
#define SECONDARY_BASE 0x170
#define SECONDARY_CTRL 0x376

#define MOCK_SECTORS 65536u
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
static int fail_writes;
static unsigned int fail_read_lba = 0xFFFFFFFFu;

static void put_u32_le(unsigned char *destination, unsigned int value) {
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static void make_old_superblock(unsigned int version, unsigned int files,
                                unsigned int data_size) {
    memcpy(mock_disk[0], "AIDENFS1", 8);
    put_u32_le(mock_disk[0] + 8, version);
    put_u32_le(mock_disk[0] + 12, files);
    put_u32_le(mock_disk[0] + 16, data_size);
}

static void make_old_entry(unsigned int slot, unsigned char type,
                           const char *name, const char *contents,
                           unsigned int data_start_lba, unsigned int slot_sectors) {
    unsigned int entry_offset = slot * 72u;
    unsigned int size = contents ? (unsigned int)strlen(contents) : 0u;
    unsigned char *entry = mock_disk[1] + entry_offset;
    entry[0] = type;
    put_u32_le(entry + 1, size);
    strncpy((char *)(entry + 5), name, 63);
    if (contents) {
        unsigned int lba = data_start_lba + slot * slot_sectors;
        for (unsigned int offset = 0; offset < size; offset++)
            mock_disk[lba + offset / 512u][offset % 512u] = (unsigned char)contents[offset];
    }
}

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
            if (active_lba == fail_read_lba) {
                mode = MOCK_IDLE;
                status_register = 0x01;
                busy_reads_left = 0;
            } else {
                mode = MOCK_READ;
                status_register = active_lba < MOCK_SECTORS
                                ? MOCK_STATUS_DRQ : 0x01;
                busy_reads_left = active_lba < MOCK_SECTORS ? 2 : 0;
            }
        } else if (value == 0x30) {
            active_lba = ((unsigned int)(drive_head & 0x0F) << 24) |
                         ((unsigned int)lba2 << 16) |
                         ((unsigned int)lba1 << 8) | lba0;
            word_position = 0;
            if (fail_writes) {
                mode = MOCK_IDLE;
                status_register = 0x01; /* Device ERR before accepting data. */
                busy_reads_left = 0;
            } else {
                mode = MOCK_WRITE;
                status_register = active_lba < MOCK_SECTORS
                                ? MOCK_STATUS_DRQ : 0x01;
                busy_reads_left = active_lba < MOCK_SECTORS ? 2 : 0;
            }
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
    if (fs_write("../escape.txt", "unsafe") == 0)
        fail("filesystem accepted a parent-traversal filename");
    if (strcmp(fs_last_error_message(), "invalid path; use normal folder names without empty, dot, parent, control, or device-separator components") != 0)
        fail("filesystem did not explain why a path traversal name was rejected");
    if (fs_write("bad\\name.txt", "unsafe") == 0)
        fail("filesystem accepted a path containing a backslash");
    if (fs_read("/etc/passwd") != 0)
        fail("filesystem returned data for a path containing a separator");
    if (strcmp(fs_last_error_message(), "invalid path; use normal folder names without empty, dot, parent, control, or device-separator components") != 0)
        fail("filesystem did not report an invalid read filename");
    if (fs_read("missing.txt") != 0 ||
        strcmp(fs_last_error_message(), "file or directory does not exist") != 0)
        fail("missing file did not produce a not-found diagnostic");
    char oversized_name[257];
    memset(oversized_name, 'n', sizeof(oversized_name));
    oversized_name[256] = 0;
    if (fs_touch(oversized_name) == 0 ||
        strcmp(fs_last_error_message(), "path is longer than 255 characters") != 0)
        fail("filesystem did not explain an overlong filename");
    char oversized_data[65537];
    memset(oversized_data, 'd', sizeof(oversized_data));
    oversized_data[65536] = 0;
    if (fs_write("large.txt", oversized_data) == 0 ||
        strcmp(fs_last_error_message(), "file contents exceed the 65535-byte limit") != 0)
        fail("filesystem did not explain an oversized file");
    if (fs_write("persist.txt", "hello from the ATA filesystem") != 0)
        fail("filesystem could not save a file");
    if (!fs_read("persist.txt") ||
        strcmp(fs_read("persist.txt"), "hello from the ATA filesystem") != 0)
        fail("saved file was not readable from the filesystem cache");
    if (fs_touch("persist.txt") != 0 ||
        !fs_read("persist.txt") ||
        strcmp(fs_read("persist.txt"), "hello from the ATA filesystem") != 0)
        fail("touching an existing file did not preserve its contents");
    {
        char entry_name[256];
        usize entry_size = 0;
        if (fs_get_entry(0, entry_name, sizeof(entry_name), &entry_size) != 0 ||
            strcmp(entry_name, "welcome.txt") != 0 ||
            entry_size != strlen("Welcome to DenOS.\nYour files are stored on the ATA hard disk.\n"))
            fail("filesystem directory-entry query returned the wrong first entry");
        if (fs_get_entry(1, entry_name, sizeof(entry_name), &entry_size) != 0 ||
            strcmp(entry_name, "persist.txt") != 0 ||
            entry_size != strlen("hello from the ATA filesystem"))
            fail("filesystem directory-entry query returned the wrong second entry");
        if (fs_get_entry(2, entry_name, sizeof(entry_name), &entry_size) == 0)
            fail("filesystem directory-entry query accepted an out-of-range index");
        if (fs_get_entry(0, entry_name, 1, &entry_size) == 0)
            fail("filesystem directory-entry query accepted a truncated name buffer");
    }

    fs_init();
    if (!fs_is_persistent()) fail("formatted disk failed to remount");
    if (!fs_read("persist.txt") ||
        strcmp(fs_read("persist.txt"), "hello from the ATA filesystem") != 0)
        fail("saved file did not survive filesystem remount");
    if (fs_storage_capacity() != (256u * 65535u))
        fail("filesystem usable capacity changed unexpectedly");
    if (fs_file_count() != 2u)
        fail("filesystem did not retain both welcome.txt and persist.txt");
    if (fs_storage_free() != (254u * 65535u))
        fail("free storage did not account for both allocated fixed file slots");
    if (fs_storage_reserved() != (2u * 65535u) -
        (strlen("Welcome to DenOS.\nYour files are stored on the ATA hard disk.\n") +
         strlen("hello from the ATA filesystem")))
        fail("reserved storage inside allocated file slots was reported incorrectly");
    if (fs_disk_size_bytes() != (usize)MOCK_SECTORS * 512u)
        fail("physical disk capacity reporting is incorrect");

    /* Hierarchical directories use the reserved type byte in the existing
       directory record. The v1 superblock and old flat files remain intact. */
    if (fs_mkdir("home") != 0 || fs_mkdir("home/user") != 0 ||
        fs_mkdir("home/user/Documents") != 0)
        fail("could not create nested directories");
    if (!fs_is_directory("home/user/Documents"))
        fail("new directory was not recognized");
    if (fs_touch("home/user") == 0 ||
        strcmp(fs_last_error_message(), "path refers to a directory, not a file") != 0)
        fail("touch accepted an existing directory as a file");
    if (fs_write("home/user", "not a directory") == 0 ||
        strcmp(fs_last_error_message(), "path refers to a directory, not a file") != 0)
        fail("file write accepted an existing directory as a file");
    if (fs_write("home/user/Documents/Meeting Notes.txt", "Bring the project notes.") != 0)
        fail("could not write a file with spaces in a nested directory");
    if (!fs_read("home/user/Documents/Meeting Notes.txt") ||
        strcmp(fs_read("home/user/Documents/Meeting Notes.txt"), "Bring the project notes.") != 0)
        fail("nested file could not be read");
    if (fs_remove("home/user") == 0 ||
        strcmp(fs_last_error_message(), "directory is not empty; remove its contents first") != 0)
        fail("non-empty directory removal was not rejected safely");
    {
        char entry[256];
        usize size = 0;
        int type = 0;
        if (fs_entry_count_in_dir("home/user/Documents") != 1u ||
            fs_get_entry_in_dir("home/user/Documents", 0, entry, sizeof(entry), &size, &type) != 0 ||
            strcmp(entry, "Meeting Notes.txt") != 0 || size != strlen("Bring the project notes.") ||
            type != FS_ENTRY_FILE)
            fail("directory enumeration did not return the expected child file");
        if (fs_entry_count_in_dir("home/user") != 1u ||
            fs_get_entry_in_dir("home/user", 0, entry, sizeof(entry), &size, &type) != 0 ||
            strcmp(entry, "Documents") != 0 || type != FS_ENTRY_DIRECTORY)
            fail("directory enumeration did not return the expected child directory");
    }
    if (fs_copy("home/user/Documents/Meeting Notes.txt", "home/user/Documents/Meeting Notes-copy.txt") != 0 ||
        !fs_read("home/user/Documents/Meeting Notes-copy.txt"))
        fail("file copy did not preserve the source contents");
    if (fs_move("home/user/Documents/Meeting Notes-copy.txt", "home/user/Documents/Archive.txt") != 0 ||
        fs_read("home/user/Documents/Meeting Notes-copy.txt") ||
        !fs_read("home/user/Documents/Archive.txt"))
        fail("file move did not create the destination and remove the source");
    if (!fs_exists("home/user/Documents/Archive.txt") ||
        !fs_is_file("home/user/Documents/Archive.txt") ||
        !fs_is_directory("home/user/Documents"))
        fail("filesystem existence/type queries returned incorrect results");
    if (fs_rename("home/user/Documents/Archive.txt", "home/user/Documents/Renamed Archive.txt") != 0 ||
        fs_exists("home/user/Documents/Archive.txt") ||
        !fs_is_file("home/user/Documents/Renamed Archive.txt"))
        fail("filesystem rename API did not move the file to its new long name");
    if (fs_rename("home/user/Documents/Renamed Archive.txt", "home/user/Documents/Archive.txt") != 0)
        fail("filesystem rename API could not restore the expected test path");
    if (fs_mkdir_p("home/user/nested/more/deep") != 0 ||
        !fs_is_directory("home/user/nested") ||
        !fs_is_directory("home/user/nested/more") ||
        !fs_is_directory("home/user/nested/more/deep"))
        fail("recursive mkdir did not create all missing parent directories");
    if (fs_mkdir_p("home/user/nested/more/deep") != 0)
        fail("recursive mkdir was not idempotent for an existing directory tree");
    if (fs_write("home/user/not-a-directory", "file") != 0 ||
        fs_mkdir_p("home/user/not-a-directory/child") == 0 ||
        strcmp(fs_last_error_message(), "a parent path component is not a directory") != 0)
        fail("recursive mkdir did not reject an existing file in the parent chain");
    if (fs_remove("home/user/not-a-directory") != 0 ||
        fs_remove("home/user/nested/more/deep") != 0 ||
        fs_remove("home/user/nested/more") != 0 ||
        fs_remove("home/user/nested") != 0)
        fail("recursive mkdir test directories could not be cleaned up");
    if (fs_mkdir_p("home/user/bad//path") == 0)
        fail("recursive mkdir accepted an invalid empty path component");
    {
        char normalized[256];
        if (fs_path_normalize("home/user", "../Documents/Meeting Notes.txt", normalized, sizeof(normalized)) != 0 ||
            strcmp(normalized, "home/Documents/Meeting Notes.txt") != 0)
            fail("relative path normalization returned the wrong path");
        if (fs_path_normalize("home/user", "../../../../escape.txt", normalized, sizeof(normalized)) == 0)
            fail("path normalization allowed traversal above the root");
    }
    fs_init();
    if (!fs_is_directory("home/user/Documents") ||
        !fs_read("home/user/Documents/Meeting Notes.txt") ||
        strcmp(fs_read("home/user/Documents/Meeting Notes.txt"), "Bring the project notes.") != 0)
        fail("nested directory or file did not survive remount");
    if (fs_remove("home/user/Documents/Meeting Notes.txt") != 0 ||
        fs_remove("home/user/Documents/Archive.txt") != 0 ||
        fs_remove("home/user/Documents") != 0 || fs_remove("home/user") != 0 || fs_remove("home") != 0)
        fail("could not remove empty directories after deleting their contents");

    /* DenFS v4 accepts long, readable paths and preserves them after remount. */
    {
        char long_path[220];
        if (fs_mkdir("home") != 0 || fs_mkdir("home/user") != 0 ||
            fs_mkdir("home/user/documents") != 0)
            fail("could not create long-path test directories");
        strcpy(long_path, "home/user/documents/");
        memset(long_path + strlen(long_path), 'a', 160);
        long_path[strlen("home/user/documents/") + 160] = 0;
        strcat(long_path, ".md");
        if (fs_write(long_path, "long-name survived") != 0 ||
            !fs_read(long_path) || strcmp(fs_read(long_path), "long-name survived") != 0)
            fail("DenFS v4 rejected a valid path longer than the v3 limit");
        fs_init();
        if (!fs_read(long_path) || strcmp(fs_read(long_path), "long-name survived") != 0)
            fail("DenFS v4 did not preserve a long path across remount");
        if (fs_remove(long_path) != 0) fail("could not remove the long-path test file");
    }
    {
        char max_name[256];
        memset(max_name, 'n', sizeof(max_name) - 1u);
        max_name[sizeof(max_name) - 1u] = 0;
        if (strlen(max_name) != 255u || fs_write(max_name, "255-character name") != 0 ||
            !fs_read(max_name) || strcmp(fs_read(max_name), "255-character name") != 0)
            fail("DenFS v4 did not accept the full 255-character path limit");
        fs_init();
        if (!fs_read(max_name) || strcmp(fs_read(max_name), "255-character name") != 0)
            fail("DenFS v4 did not preserve a maximum-length path across remount");
        if (fs_remove(max_name) != 0) fail("could not remove the maximum-length path test file");
    }

    /* A v3 image must migrate without losing data before the path table grows. */
    memset(mock_disk, 0, sizeof(mock_disk));
    make_old_superblock(3u, 256u, 65536u);
    make_old_entry(7u, 2u, "home", 0, 37u, 128u);
    make_old_entry(8u, 2u, "home/user", 0, 37u, 128u);
    make_old_entry(9u, 1u, "home/user/old-note.txt", "A v3 note survived the path upgrade.\n", 37u, 128u);
    fs_init();
    if (!fs_is_persistent() || !fs_read("home/user/old-note.txt") ||
        strcmp(fs_read("home/user/old-note.txt"), "A v3 note survived the path upgrade.\n") != 0)
        fail("v3 migration did not preserve nested files");
    if (mock_disk[0][8] != 4u || mock_disk[0][12] != 0u || mock_disk[0][13] != 1u)
        fail("v3 migration did not write the v4 filesystem superblock");

    /* An unknown filesystem must not be reformatted or have data destroyed. */
    mock_disk[0][0] = 0x55;
    fs_init();
    if (fs_is_persistent()) fail("unknown filesystem was incorrectly mounted");
    if (mock_disk[0][0] != 0x55)
        fail("unknown disk contents were overwritten during probing");

    /* A real v1 image is migrated in place, keeping its file contents. */
    memset(mock_disk, 0, sizeof(mock_disk));
    make_old_superblock(1u, 48u, 16384u);
    make_old_entry(0u, 1u, "legacy-note.txt", "Legacy v1 note preserved.\n", 8u, 32u);
    fs_init();
    if (!fs_is_persistent() || !fs_read("legacy-note.txt") ||
        strcmp(fs_read("legacy-note.txt"), "Legacy v1 note preserved.\n") != 0)
        fail("v1 migration did not preserve the legacy note");
    if (mock_disk[0][8] != 4u || mock_disk[0][12] != 0u || mock_disk[0][13] != 1u)
        fail("v1 migration did not write the v4 filesystem superblock");

    /* A v2 image also carries folders, so verify nested paths survive upgrade. */
    memset(mock_disk, 0, sizeof(mock_disk));
    make_old_superblock(2u, 128u, 32768u);
    make_old_entry(0u, 2u, "home", 0, 19u, 64u);
    make_old_entry(1u, 2u, "home/user", 0, 19u, 64u);
    make_old_entry(2u, 2u, "home/user/Documents", 0, 19u, 64u);
    make_old_entry(3u, 1u, "home/user/Documents/keep.txt",
                   "A v2 document survived the migration.\n", 19u, 64u);
    fs_init();
    if (!fs_is_persistent() || !fs_is_directory("home/user/Documents") ||
        !fs_read("home/user/Documents/keep.txt") ||
        strcmp(fs_read("home/user/Documents/keep.txt"),
               "A v2 document survived the migration.\n") != 0)
        fail("v2 migration did not preserve folders and documents");
    if (mock_disk[0][8] != 4u || mock_disk[0][12] != 0u || mock_disk[0][13] != 1u)
        fail("v2 migration did not write the v4 filesystem superblock");

    /* A failed data-sector read must abort the mount rather than silently
       publishing a partial cache as a healthy persistent filesystem. */
    fail_read_lba = 133u + 3u * 128u; /* Slot 3 was migrated into the v4 data area. */
    fs_init();
    if (fs_is_persistent())
        fail("filesystem mounted after a required file-data sector read failed");
    if (fs_read("home/user/Documents/keep.txt") != 0)
        fail("failed mount left a partial file cache visible");
    if (strncmp(fs_storage_name(), "RAM fallback: ATA file-data read failed", 39) != 0)
        fail("failed file-data read did not produce a clear mount diagnostic");
    fail_read_lba = 0xFFFFFFFFu;
    fs_init();
    if (!fs_is_persistent() || !fs_read("home/user/Documents/keep.txt"))
        fail("filesystem did not remount after the transient read fault cleared");

    /* A failed persistent write must not leave an uncommitted RAM-only edit
       looking like saved data, and later writes must fail visibly. */
    fail_writes = 1;
    if (fs_write("home/user/Documents/keep.txt", "overwrite must fail") == 0)
        fail("failed ATA overwrite was reported as a successful save");
    if (!fs_read("home/user/Documents/keep.txt") ||
        strcmp(fs_read("home/user/Documents/keep.txt"),
               "A v2 document survived the migration.\n") != 0)
        fail("failed ATA overwrite replaced the committed RAM-cache contents");
    if (fs_write("home/user/recovery-test.txt", "must not appear saved") == 0)
        fail("failed ATA create was reported as a successful save");
    if (fs_read("home/user/recovery-test.txt") != 0)
        fail("failed ATA create left an uncommitted file in the RAM cache");
    if (fs_is_persistent())
        fail("filesystem remained marked persistent after a failed write");
    if (strncmp(fs_storage_name(), "Storage fault:", 14) != 0)
        fail("filesystem did not expose the persistent-storage fault");
    if (fs_write("another-file.txt", "must also fail") == 0)
        fail("filesystem silently accepted a new RAM-only write after disk failure");

    puts("DenOS storage tests passed: all four IDE positions, IDENTIFY, PIO I/O, filesystem v4 format/remount, v1/v2/v3 migration, nested folders, 64 KiB file support, copy/move, unknown-disk protection, and failed-write rollback.");
    return 0;
}
