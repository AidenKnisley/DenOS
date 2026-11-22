#include "fs.h"
#include "serial.h"
#include "apps.h"
#include "ata.h"

#define FILES 48
#define NAME 64
#define DATA 16384
#define SLOT_DATA_CAPACITY (DATA - 1u)
#define SECTOR_SIZE 512u
#define DIR_ENTRY_SIZE 72u
#define DIR_SECTORS 7u
#define DATA_START_LBA (1u + DIR_SECTORS)
#define SLOT_SECTORS (DATA / SECTOR_SIZE)
#define FS_VERSION 1u
#define DIR_BYTES (DIR_SECTORS * SECTOR_SIZE)
#define FS_REQUIRED_SECTORS (DATA_START_LBA + (FILES * SLOT_SECTORS))

static struct {
    char name[NAME];
    char data[DATA];
    int used;
} files[FILES];

static u8 directory[DIR_BYTES];
static u8 sector_buffer[SECTOR_SIZE];
static int disk_ready;
static const char *storage_status = "RAM fallback: no ATA disk";

static usize string_length(const char *s, usize limit) {
    usize n = 0;
    if (!s) return 0;
    while (n < limit && s[n]) n++;
    return n;
}

static int string_equal(const char *a, const char *b) {
    usize i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static void string_copy(char *dst, const char *src, usize cap) {
    usize i = 0;
    if (!cap) return;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static u32 read_u32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) |
           ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void write_u32(u8 *p, u32 value) {
    p[0] = (u8)value;
    p[1] = (u8)(value >> 8);
    p[2] = (u8)(value >> 16);
    p[3] = (u8)(value >> 24);
}

static u32 entry_offset(int slot) {
    return (u32)slot * DIR_ENTRY_SIZE;
}

static u32 data_lba(int slot) {
    return DATA_START_LBA + (u32)slot * SLOT_SECTORS;
}

static int write_directory(void) {
    if (!disk_ready) return 0;
    for (u32 s = 0; s < DIR_SECTORS; s++) {
        if (ata_write_sector(1u + s, directory + s * SECTOR_SIZE) < 0)
            return -1;
    }
    return ata_flush();
}

static int read_directory(void) {
    for (u32 s = 0; s < DIR_SECTORS; s++) {
        if (ata_read_sector(1u + s, directory + s * SECTOR_SIZE) < 0)
            return -1;
    }
    return 0;
}

static int format_disk(void) {
    for (usize i = 0; i < SECTOR_SIZE; i++) sector_buffer[i] = 0;
    const char magic[8] = {'A','I','D','E','N','F','S','1'};
    for (int i = 0; i < 8; i++) sector_buffer[i] = (u8)magic[i];
    write_u32(sector_buffer + 8, FS_VERSION);
    write_u32(sector_buffer + 12, FILES);
    write_u32(sector_buffer + 16, DATA);
    if (ata_write_sector(0, sector_buffer) < 0) return -1;
    if (ata_flush() < 0) return -1;

    for (usize i = 0; i < DIR_BYTES; i++) directory[i] = 0;
    return write_directory();
}

static int superblock_valid(const u8 *sector) {
    const char magic[8] = {'A','I','D','E','N','F','S','1'};
    for (int i = 0; i < 8; i++)
        if (sector[i] != (u8)magic[i]) return 0;

    return read_u32(sector + 8) == FS_VERSION &&
           read_u32(sector + 12) == FILES &&
           read_u32(sector + 16) == DATA;
}

/* Only a genuinely blank disk is formatted automatically. Never erase an
 * unknown or damaged disk just because its superblock is not ours. */
static int sector_is_blank(const u8 *sector) {
    int all_zero = 1;
    int all_ones = 1;

    for (usize i = 0; i < SECTOR_SIZE; i++) {
        if (sector[i] != 0x00) all_zero = 0;
        if (sector[i] != 0xFF) all_ones = 0;
    }
    return all_zero || all_ones;
}

static int load_file(int slot, u32 size) {
    u32 base = data_lba(slot);
    for (u32 s = 0; s < SLOT_SECTORS; s++) {
        if (ata_read_sector(base + s, sector_buffer) < 0) return -1;
        u32 offset = s * SECTOR_SIZE;
        for (u32 i = 0; i < SECTOR_SIZE; i++) {
            if (offset + i < size) files[slot].data[offset + i] = (char)sector_buffer[i];
        }
    }
    files[slot].data[size] = 0;
    return 0;
}

static int mount_disk(void) {
    disk_ready = 0;

    if (ata_init() < 0) {
        storage_status = ata_diagnostic();
        return -1;
    }

    if (ata_sector_count() < FS_REQUIRED_SECTORS) {
        storage_status = "RAM fallback: ATA disk is too small";
        return -1;
    }

    if (ata_read_sector(0, sector_buffer) < 0) {
        storage_status = "RAM fallback: ATA read error";
        return -1;
    }

    if (sector_is_blank(sector_buffer)) {
        disk_ready = 1;
        if (format_disk() < 0) {
            disk_ready = 0;
            storage_status = "RAM fallback: could not format ATA disk";
            return -1;
        }
    } else if (superblock_valid(sector_buffer)) {
        disk_ready = 1;
    } else {
        storage_status = "RAM fallback: disk format is unknown; disk left untouched";
        return -1;
    }

    if (read_directory() < 0) {
        disk_ready = 0;
        storage_status = "RAM fallback: could not read ATA directory";
        return -1;
    }

    for (int slot = 0; slot < FILES; slot++) {
        u32 off = entry_offset(slot);
        if (directory[off] != 1) continue;
        u32 size = read_u32(directory + off + 1);
        if (size >= DATA) continue;

        int name_ok = 0;
        for (u32 i = 0; i < NAME; i++) {
            files[slot].name[i] = (char)directory[off + 5 + i];
            if (files[slot].name[i] == 0) name_ok = 1;
        }
        if (!name_ok || !files[slot].name[0]) continue;
        files[slot].used = 1;
        if (load_file(slot, size) < 0) {
            files[slot].used = 0;
            files[slot].name[0] = 0;
            files[slot].data[0] = 0;
        }
    }

    storage_status = ata_model();
    return 0;
}

static int persist_slot(int slot) {
    if (!disk_ready) return 0;
    u32 size = (u32)string_length(files[slot].data, DATA);
    u32 base = data_lba(slot);

    /* Write contents first; commit the directory entry only after data is safe. */
    for (u32 s = 0; s < SLOT_SECTORS; s++) {
        u32 offset = s * SECTOR_SIZE;
        for (u32 i = 0; i < SECTOR_SIZE; i++) {
            u32 at = offset + i;
            sector_buffer[i] = at < size ? (u8)files[slot].data[at] : 0;
        }
        if (ata_write_sector(base + s, sector_buffer) < 0) return -1;
    }
    if (ata_flush() < 0) return -1;

    u32 off = entry_offset(slot);
    directory[off] = 1;
    write_u32(directory + off + 1, size);
    for (u32 i = 0; i < NAME; i++)
        directory[off + 5 + i] = (u8)files[slot].name[i];
    for (u32 i = 69; i < DIR_ENTRY_SIZE; i++) directory[off + i] = 0;
    return write_directory();
}

static int persist_remove(int slot) {
    if (!disk_ready) return 0;
    u32 off = entry_offset(slot);
    for (u32 i = 0; i < DIR_ENTRY_SIZE; i++) directory[off + i] = 0;
    return write_directory();
}

void fs_init(void) {
    for (int i = 0; i < FILES; i++) {
        files[i].used = 0;
        files[i].name[0] = 0;
        files[i].data[0] = 0;
    }
    disk_ready = 0;
    if (mount_disk() < 0) {
        for (usize i = 0; i < DIR_BYTES; i++) directory[i] = 0;
    }

    if (!fs_read("welcome.txt")) {
        fs_write("welcome.txt", disk_ready
            ? "Welcome to DenOS.\nYour files are stored on the ATA hard disk.\n"
            : "Welcome to DenOS.\nPersistent storage is unavailable; this session is temporary.\n");
    }

    /* Bundled examples are defaults only; never overwrite the user's saved copy. */
    apps_install();
}

int fs_write(const char *name, const char *data) {
    if (!name || !data || !name[0]) return -1;
    usize name_len = string_length(name, NAME);
    usize data_len = string_length(data, DATA);
    if (!name_len || name_len >= NAME || data_len >= DATA) return -1;

    int slot = -1;
    for (int i = 0; i < FILES; i++) {
        if (files[i].used && string_equal(files[i].name, name)) {
            slot = i;
            break;
        }
        if (!files[i].used && slot < 0) slot = i;
    }
    if (slot < 0) return -1;

    string_copy(files[slot].name, name, NAME);
    string_copy(files[slot].data, data, DATA);
    files[slot].used = 1;
    if (persist_slot(slot) < 0) return -1;
    return 0;
}

const char *fs_read(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < FILES; i++)
        if (files[i].used && string_equal(files[i].name, name)) return files[i].data;
    return NULL;
}

int fs_remove(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < FILES; i++) {
        if (files[i].used && string_equal(files[i].name, name)) {
            if (persist_remove(i) < 0) return -1;
            files[i].used = 0;
            files[i].name[0] = 0;
            files[i].data[0] = 0;
            return 0;
        }
    }
    return -1;
}

int fs_touch(const char *name) {
    if (fs_read(name)) return 0;
    return fs_write(name, "");
}

void fs_list(void) {
    int any = 0;
    for (int i = 0; i < FILES; i++) {
        if (files[i].used) {
            serial_write(files[i].name);
            serial_write("\n");
            any = 1;
        }
    }
    if (!any) serial_write("(empty)\n");
}

usize fs_file_count(void) {
    usize n = 0;
    for (int i = 0; i < FILES; i++) if (files[i].used) n++;
    return n;
}

usize fs_bytes_used(void) {
    usize n = 0;
    for (int i = 0; i < FILES; i++)
        if (files[i].used) n += string_length(files[i].data, DATA);
    return n;
}

/* This is the filesystem's usable file-data pool, not the raw disk image size. */
usize fs_storage_capacity(void) {
    return (usize)FILES * (usize)SLOT_DATA_CAPACITY;
}

/* Each file owns one fixed-size slot, so unused bytes inside an existing
 * file's slot cannot be handed to another file. Free space is therefore
 * counted by unused slots, not by subtracting text length from total bytes. */
usize fs_storage_free(void) {
    usize count = fs_file_count();
    return count < FILES ? ((usize)FILES - count) * SLOT_DATA_CAPACITY : 0;
}

usize fs_storage_reserved(void) {
    usize allocated = fs_file_count() * SLOT_DATA_CAPACITY;
    usize used = fs_bytes_used();
    return allocated > used ? allocated - used : 0;
}

usize fs_disk_size_bytes(void) {
    return (usize)ata_sector_count() * (usize)SECTOR_SIZE;
}

u32 fs_disk_sector_count(void) {
    return ata_sector_count();
}

const char *fs_disk_model(void) {
    return ata_model();
}

int fs_is_persistent(void) {
    return disk_ready;
}

const char *fs_storage_name(void) {
    return storage_status;
}
