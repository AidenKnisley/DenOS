#include "fs.h"
#include "serial.h"
#include "ata.h"
#include "kstring.h"
#include "path.h"

/* DenFS v4 keeps the fixed 64 KiB data slots and 256-entry table, while
 * expanding every stored path to 255 bytes. Recognized v1/v2/v3 volumes migrate. */
#define FILES 256
#define NAME 256
#define DATA 65536
#define SLOT_DATA_CAPACITY (DATA - 1u)
#define SECTOR_SIZE 512u
#define DIR_ENTRY_SIZE 264u
#define DIR_SECTORS 132u
#define DATA_START_LBA (1u + DIR_SECTORS)
#define SLOT_SECTORS (DATA / SECTOR_SIZE)
#define FS_VERSION 4u
#define DIR_BYTES (DIR_SECTORS * SECTOR_SIZE)
#define FS_REQUIRED_SECTORS (DATA_START_LBA + (FILES * SLOT_SECTORS))

#define V1_FILES 48u
#define V1_DATA 16384u
#define V1_DIR_SECTORS 7u
#define V1_DATA_START_LBA (1u + V1_DIR_SECTORS)
#define V1_SLOT_SECTORS (V1_DATA / SECTOR_SIZE)
#define V2_FILES 128u
#define V2_DATA 32768u
#define V2_DIR_SECTORS 18u
#define V2_DATA_START_LBA (1u + V2_DIR_SECTORS)
#define V2_SLOT_SECTORS (V2_DATA / SECTOR_SIZE)
#define V3_FILES 256u
#define V3_DATA 65536u
#define V3_DIR_SECTORS 36u
#define V3_DATA_START_LBA (1u + V3_DIR_SECTORS)
#define V3_SLOT_SECTORS (V3_DATA / SECTOR_SIZE)
#define LEGACY_DIR_ENTRY_SIZE 72u
#define LEGACY_NAME_CAPACITY 64u
#define LEGACY_DIR_BYTES (V3_DIR_SECTORS * SECTOR_SIZE)

static struct {
    char name[NAME];
    char data[DATA];
    int used;
    int is_directory;
} files[FILES];

static u8 directory[DIR_BYTES];
static u8 legacy_directory[LEGACY_DIR_BYTES];
static u8 sector_buffer[SECTOR_SIZE];
static int disk_ready;
static int storage_faulted;
static const char *storage_status = "RAM fallback: no ATA disk";
static enum fs_error last_fs_error;

/* One bounded transaction snapshot protects the live cache from reporting an
 * uncommitted edit as saved after a disk I/O failure. DenOS is currently a
 * single-core, non-preemptive kernel, so filesystem mutations are serialized. */
static struct {
    char name[NAME];
    char data[DATA];
    usize data_length;
    int used;
    int is_directory;
    u8 directory_entry[DIR_ENTRY_SIZE];
} slot_snapshot;

static void set_fs_error(enum fs_error error) { last_fs_error = error; }

const char *fs_last_error_message(void) {
    switch (last_fs_error) {
        case FS_ERROR_NONE: return "no error";
        case FS_ERROR_INVALID_ARGUMENT: return "a required path or data pointer was missing";
        case FS_ERROR_INVALID_NAME: return "invalid path; use normal folder names without empty, dot, parent, control, or device-separator components";
        case FS_ERROR_NAME_TOO_LONG: return "path is longer than 255 characters";
        case FS_ERROR_DATA_TOO_LARGE: return "file contents exceed the 65535-byte limit";
        case FS_ERROR_NOT_FOUND: return "file or directory does not exist";
        case FS_ERROR_NO_SPACE: return "the filesystem has no free directory slots";
        case FS_ERROR_IO: return "ATA storage I/O failed; persistent writes are disabled for this boot";
        case FS_ERROR_BUFFER_TOO_SMALL: return "the destination buffer is too small for the entry name";
        case FS_ERROR_ALREADY_EXISTS: return "an entry with that path already exists";
        case FS_ERROR_NOT_DIRECTORY: return "a parent path component is not a directory";
        case FS_ERROR_DIRECTORY_NOT_EMPTY: return "directory is not empty; remove its contents first";
        case FS_ERROR_IS_DIRECTORY: return "path refers to a directory, not a file";
        default: return "unknown filesystem error";
    }
}

static usize string_length(const char *text, usize limit) { return kstrnlen(text, limit); }
static int string_equal(const char *left, const char *right) { return kstrcmp(left, right) == 0; }
static void string_copy(char *destination, const char *source, usize capacity) {
    (void)kstrlcpy(destination, source, capacity);
}
static u32 read_u32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static void write_u32(u8 *p, u32 value) {
    p[0] = (u8)value; p[1] = (u8)(value >> 8); p[2] = (u8)(value >> 16); p[3] = (u8)(value >> 24);
}
static u32 entry_offset(int slot) { return (u32)slot * DIR_ENTRY_SIZE; }
static u32 data_lba(int slot) { return DATA_START_LBA + (u32)slot * SLOT_SECTORS; }

static void snapshot_slot(int slot) {
    usize length = string_length(files[slot].data, DATA);
    slot_snapshot.data_length = length < DATA ? length : DATA - 1u;
    slot_snapshot.used = files[slot].used;
    slot_snapshot.is_directory = files[slot].is_directory;
    for (usize i = 0; i < NAME; ++i) slot_snapshot.name[i] = files[slot].name[i];
    for (usize i = 0; i <= slot_snapshot.data_length; ++i)
        slot_snapshot.data[i] = files[slot].data[i];
    u32 offset = entry_offset(slot);
    for (usize i = 0; i < DIR_ENTRY_SIZE; ++i)
        slot_snapshot.directory_entry[i] = directory[offset + i];
}

static void restore_slot(int slot) {
    u32 offset = entry_offset(slot);
    files[slot].used = slot_snapshot.used;
    files[slot].is_directory = slot_snapshot.is_directory;
    for (usize i = 0; i < NAME; ++i) files[slot].name[i] = slot_snapshot.name[i];
    for (usize i = 0; i <= slot_snapshot.data_length; ++i)
        files[slot].data[i] = slot_snapshot.data[i];
    for (usize i = 0; i < DIR_ENTRY_SIZE; ++i)
        directory[offset + i] = slot_snapshot.directory_entry[i];
}

static int storage_io_failed(const char *reason) {
    disk_ready = 0;
    storage_faulted = 1;
    storage_status = reason;
    return -1;
}

static int write_directory(void) {
    if (!disk_ready) return storage_faulted ? -1 : 0;
    for (u32 s = 0; s < DIR_SECTORS; s++)
        if (ata_write_sector(1u + s, directory + s * SECTOR_SIZE) < 0) return -1;
    return ata_flush();
}
static int read_directory(void) {
    for (u32 s = 0; s < DIR_SECTORS; s++)
        if (ata_read_sector(1u + s, directory + s * SECTOR_SIZE) < 0) return -1;
    return 0;
}
static int format_disk(void) {
    for (usize i = 0; i < SECTOR_SIZE; i++) sector_buffer[i] = 0;
    const char magic[8] = {'A','I','D','E','N','F','S','1'};
    for (int i = 0; i < 8; i++) sector_buffer[i] = (u8)magic[i];
    write_u32(sector_buffer + 8, FS_VERSION);
    write_u32(sector_buffer + 12, FILES);
    write_u32(sector_buffer + 16, DATA);
    if (ata_write_sector(0, sector_buffer) < 0 || ata_flush() < 0) return -1;
    for (usize i = 0; i < DIR_BYTES; i++) directory[i] = 0;
    return write_directory();
}
static int superblock_valid(const u8 *sector) {
    const char magic[8] = {'A','I','D','E','N','F','S','1'};
    for (int i = 0; i < 8; i++) if (sector[i] != (u8)magic[i]) return 0;
    return read_u32(sector + 8) == FS_VERSION &&
           read_u32(sector + 12) == FILES && read_u32(sector + 16) == DATA;
}
static int old_superblock_version(const u8 *sector) {
    const char magic[8] = {'A','I','D','E','N','F','S','1'};
    for (int i = 0; i < 8; i++) if (sector[i] != (u8)magic[i]) return 0;
    if (read_u32(sector + 8) == 1u &&
        read_u32(sector + 12) == V1_FILES && read_u32(sector + 16) == V1_DATA) return 1;
    if (read_u32(sector + 8) == 2u &&
        read_u32(sector + 12) == V2_FILES && read_u32(sector + 16) == V2_DATA) return 2;
    if (read_u32(sector + 8) == 3u &&
        read_u32(sector + 12) == V3_FILES && read_u32(sector + 16) == V3_DATA) return 3;
    return 0;
}

static int sector_is_blank(const u8 *sector) {
    int all_zero = 1, all_ones = 1;
    for (usize i = 0; i < SECTOR_SIZE; i++) {
        if (sector[i] != 0) all_zero = 0;
        if (sector[i] != 0xFF) all_ones = 0;
    }
    return all_zero || all_ones;
}
static int load_file(int slot, u32 size) {
    u32 base = data_lba(slot);
    u32 sectors = (size + SECTOR_SIZE - 1u) / SECTOR_SIZE;
    for (u32 s = 0; s < sectors; s++) {
        if (ata_read_sector(base + s, sector_buffer) < 0) return -1;
        u32 offset = s * SECTOR_SIZE;
        for (u32 i = 0; i < SECTOR_SIZE && offset + i < size; i++)
            files[slot].data[offset + i] = (char)sector_buffer[i];
    }
    files[slot].data[size] = 0;
    return 0;
}

static int persist_entry(int slot);

static int read_old_directory(u32 directory_sectors) {
    if (directory_sectors > V3_DIR_SECTORS) return -1;
    for (u32 sector = 0; sector < directory_sectors; sector++)
        if (ata_read_sector(1u + sector, legacy_directory + sector * SECTOR_SIZE) < 0) return -1;
    return 0;
}

static int migrate_old_disk(u32 old_files, u32 old_data, u32 old_dir_sectors,
                            u32 old_data_start_lba, u32 old_slot_sectors) {
    if (read_old_directory(old_dir_sectors) < 0) return -1;
    for (u32 slot = 0; slot < old_files; slot++) {
        u32 off = slot * LEGACY_DIR_ENTRY_SIZE;
        u8 type = legacy_directory[off];
        u32 size = read_u32(legacy_directory + off + 1);
        int valid_name = 0;
        if (type != 1u && type != 2u) continue;
        if ((type == 1u && size >= old_data) || (type == 2u && size != 0)) continue;
        files[slot].name[0] = 0;
        for (u32 i = 0; i < LEGACY_NAME_CAPACITY; i++) {
            files[slot].name[i] = (char)legacy_directory[off + 5u + i];
            if (!files[slot].name[i]) { valid_name = 1; break; }
        }
        if (!valid_name || !fs_path_valid(files[slot].name)) continue;
        files[slot].used = 1;
        files[slot].is_directory = type == 2u;
        files[slot].data[0] = 0;
        if (type == 1u) {
            u32 base = old_data_start_lba + slot * old_slot_sectors;
            u32 sectors = (size + SECTOR_SIZE - 1u) / SECTOR_SIZE;
            int read_ok = 1;
            for (u32 sector = 0; sector < sectors; sector++) {
                if (ata_read_sector(base + sector, sector_buffer) < 0) { read_ok = 0; break; }
                u32 offset = sector * SECTOR_SIZE;
                for (u32 i = 0; i < SECTOR_SIZE && offset + i < size; i++)
                    files[slot].data[offset + i] = (char)sector_buffer[i];
            }
            if (!read_ok) {
                files[slot].used = 0;
                files[slot].name[0] = 0;
                files[slot].data[0] = 0;
                return -1;
            }
            files[slot].data[size] = 0;
        }
    }

    /* Every recognized old entry is now resident in RAM. Only supported
       versions are migrated; unknown disks are never formatted. */
    if (format_disk() < 0) return -1;
    for (int slot = 0; slot < FILES; slot++) {
        if (!files[slot].used) continue;
        if (persist_entry(slot) < 0) return -1;
    }
    return 0;
}

static int mount_disk(void) {
    int old_version = 0;
    disk_ready = 0;
    if (ata_init() < 0) { storage_status = ata_diagnostic(); return -1; }
    if (ata_sector_count() < FS_REQUIRED_SECTORS) {
        storage_status = "RAM fallback: ATA disk is too small for DenFS v4"; return -1;
    }
    if (ata_read_sector(0, sector_buffer) < 0) {
        storage_status = "RAM fallback: ATA read error"; return -1;
    }
    if (sector_is_blank(sector_buffer)) {
        disk_ready = 1;
        if (format_disk() < 0) { disk_ready = 0; storage_status = "RAM fallback: could not format ATA disk"; return -1; }
    } else if (superblock_valid(sector_buffer)) {
        disk_ready = 1;
        if (read_directory() < 0) { disk_ready = 0; storage_status = "RAM fallback: could not read ATA directory"; return -1; }
    } else if ((old_version = old_superblock_version(sector_buffer)) != 0) {
        u32 old_files = old_version == 1 ? V1_FILES : (old_version == 2 ? V2_FILES : V3_FILES);
        u32 old_data = old_version == 1 ? V1_DATA : (old_version == 2 ? V2_DATA : V3_DATA);
        u32 old_dir_sectors = old_version == 1 ? V1_DIR_SECTORS : (old_version == 2 ? V2_DIR_SECTORS : V3_DIR_SECTORS);
        u32 old_data_start = old_version == 1 ? V1_DATA_START_LBA : (old_version == 2 ? V2_DATA_START_LBA : V3_DATA_START_LBA);
        u32 old_slot_sectors = old_version == 1 ? V1_SLOT_SECTORS : (old_version == 2 ? V2_SLOT_SECTORS : V3_SLOT_SECTORS);
        disk_ready = 1;
        if (migrate_old_disk(old_files, old_data, old_dir_sectors,
                             old_data_start, old_slot_sectors) < 0) {
            disk_ready = 0;
            storage_status = "RAM fallback: filesystem migration failed; keep a backup of this disk image";
            return -1;
        }
        if (read_directory() < 0) {
            disk_ready = 0; storage_status = "RAM fallback: could not verify migrated directory"; return -1;
        }
    } else {
        storage_status = "RAM fallback: disk format is unknown; disk left untouched";
        return -1;
    }

    for (int slot = 0; slot < FILES; slot++) {
        u32 off = entry_offset(slot);
        u8 type = directory[off];
        u32 size = read_u32(directory + off + 1);
        if (type != FS_ENTRY_FILE && type != FS_ENTRY_DIRECTORY) continue;
        if ((type == FS_ENTRY_FILE && size >= DATA) ||
            (type == FS_ENTRY_DIRECTORY && size != 0)) {
            disk_ready = 0;
            storage_status = "RAM fallback: invalid DenFS directory entry; disk left untouched";
            return -1;
        }
        int name_ok = 0;
        for (u32 i = 0; i < NAME; i++) {
            files[slot].name[i] = (char)directory[off + 5 + i];
            if (files[slot].name[i] == 0) name_ok = 1;
        }
        if (!name_ok || !fs_path_valid(files[slot].name)) {
            disk_ready = 0;
            storage_status = "RAM fallback: invalid DenFS path in directory; disk left untouched";
            return -1;
        }
        files[slot].used = 1;
        files[slot].is_directory = type == FS_ENTRY_DIRECTORY;
        files[slot].data[0] = 0;
        if (!files[slot].is_directory && load_file(slot, size) < 0) {
            disk_ready = 0;
            storage_status = "RAM fallback: ATA file-data read failed; disk left untouched";
            return -1;
        }
    }
    storage_status = ata_model();
    return 0;
}

static int find_slot(const char *path) {
    for (int i = 0; i < FILES; i++)
        if (files[i].used && string_equal(files[i].name, path)) return i;
    return -1;
}
static int used_slots(void) {
    int count = 0;
    for (int i = 0; i < FILES; i++) if (files[i].used) count++;
    return count;
}
static int parent_is_valid(const char *path) {
    char parent[NAME];
    if (fs_path_parent(path, parent, sizeof(parent)) < 0) return 0;
    return parent[0] == 0 || fs_is_directory(parent);
}
static int has_children(const char *path) {
    usize length = string_length(path, NAME);
    for (int i = 0; i < FILES; i++) {
        if (!files[i].used || i == find_slot(path)) continue;
        if (files[i].name[length] == '/' && kstrncmp(files[i].name, path, length) == 0) return 1;
    }
    return 0;
}
static int persist_entry(int slot) {
    u32 off = entry_offset(slot);
    u32 size = files[slot].is_directory ? 0u : (u32)string_length(files[slot].data, DATA);
    if (!disk_ready) return storage_faulted ? -1 : 0;
    if (!files[slot].is_directory) {
        u32 base = data_lba(slot);
        u32 sectors = (size + SECTOR_SIZE - 1u) / SECTOR_SIZE;
        for (u32 s = 0; s < sectors; s++) {
            u32 offset = s * SECTOR_SIZE;
            for (u32 i = 0; i < SECTOR_SIZE; i++) {
                u32 at = offset + i;
                sector_buffer[i] = at < size ? (u8)files[slot].data[at] : 0;
            }
            if (ata_write_sector(base + s, sector_buffer) < 0)
                return storage_io_failed("Storage fault: ATA file-data write failed; disk disabled");
        }
        if (ata_flush() < 0)
            return storage_io_failed("Storage fault: ATA file-data flush failed; disk disabled");
    }
    directory[off] = files[slot].is_directory ? FS_ENTRY_DIRECTORY : FS_ENTRY_FILE;
    write_u32(directory + off + 1, size);
    for (u32 i = 0; i < NAME; i++) directory[off + 5 + i] = (u8)files[slot].name[i];
    for (u32 i = 5u + NAME; i < DIR_ENTRY_SIZE; i++) directory[off + i] = 0;
    if (write_directory() < 0)
        return storage_io_failed("Storage fault: ATA directory write failed; disk disabled");
    return 0;
}
static int persist_remove(int slot) {
    if (!disk_ready) return storage_faulted ? -1 : 0;
    u32 off = entry_offset(slot);
    for (u32 i = 0; i < DIR_ENTRY_SIZE; i++) directory[off + i] = 0;
    if (write_directory() < 0)
        return storage_io_failed("Storage fault: ATA directory update failed; disk disabled");
    return 0;
}

void fs_init(void) {
    set_fs_error(FS_ERROR_NONE);
    for (int i = 0; i < FILES; i++) {
        files[i].used = 0; files[i].is_directory = 0;
        files[i].name[0] = 0; files[i].data[0] = 0;
    }
    disk_ready = 0;
    storage_faulted = 0;
    if (mount_disk() < 0) {
        for (int i = 0; i < FILES; ++i) {
            files[i].used = 0;
            files[i].is_directory = 0;
            files[i].name[0] = 0;
            files[i].data[0] = 0;
        }
        for (usize i = 0; i < DIR_BYTES; ++i) directory[i] = 0;
    }
    if (!fs_read("welcome.txt"))
        (void)fs_write("welcome.txt", disk_ready
            ? "Welcome to DenOS.\nYour files are stored on the ATA hard disk.\n"
            : "Welcome to DenOS.\nPersistent storage is unavailable; this session is temporary.\n");
}

int fs_write(const char *path, const char *data) {
    if (!path || !data) { set_fs_error(FS_ERROR_INVALID_ARGUMENT); return -1; }
    if (kstrnlen(path, NAME) >= NAME) { set_fs_error(FS_ERROR_NAME_TOO_LONG); return -1; }
    if (!fs_path_valid(path)) { set_fs_error(FS_ERROR_INVALID_NAME); return -1; }
    if (!parent_is_valid(path)) { set_fs_error(FS_ERROR_NOT_DIRECTORY); return -1; }
    usize data_len = string_length(data, DATA);
    if (data_len >= DATA) { set_fs_error(FS_ERROR_DATA_TOO_LARGE); return -1; }
    int slot = find_slot(path);
    if (slot >= 0 && files[slot].is_directory) { set_fs_error(FS_ERROR_IS_DIRECTORY); return -1; }
    if (slot < 0) {
        for (int i = 0; i < FILES; i++) if (!files[i].used) { slot = i; break; }
    }
    if (slot < 0) { set_fs_error(FS_ERROR_NO_SPACE); return -1; }
    snapshot_slot(slot);
    string_copy(files[slot].name, path, NAME);
    string_copy(files[slot].data, data, DATA);
    files[slot].used = 1; files[slot].is_directory = 0;
    if (persist_entry(slot) < 0) {
        restore_slot(slot);
        set_fs_error(FS_ERROR_IO);
        return -1;
    }
    set_fs_error(FS_ERROR_NONE); return 0;
}

const char *fs_read(const char *path) {
    if (!path) { set_fs_error(FS_ERROR_INVALID_ARGUMENT); return NULL; }
    if (kstrnlen(path, NAME) >= NAME) { set_fs_error(FS_ERROR_NAME_TOO_LONG); return NULL; }
    if (!fs_path_valid(path)) { set_fs_error(FS_ERROR_INVALID_NAME); return NULL; }
    int slot = find_slot(path);
    if (slot < 0) { set_fs_error(FS_ERROR_NOT_FOUND); return NULL; }
    if (files[slot].is_directory) { set_fs_error(FS_ERROR_IS_DIRECTORY); return NULL; }
    set_fs_error(FS_ERROR_NONE); return files[slot].data;
}

int fs_mkdir(const char *path) {
    if (!path) { set_fs_error(FS_ERROR_INVALID_ARGUMENT); return -1; }
    if (kstrnlen(path, NAME) >= NAME) { set_fs_error(FS_ERROR_NAME_TOO_LONG); return -1; }
    if (!fs_path_valid(path)) { set_fs_error(FS_ERROR_INVALID_NAME); return -1; }
    if (find_slot(path) >= 0) { set_fs_error(FS_ERROR_ALREADY_EXISTS); return -1; }
    if (!parent_is_valid(path)) { set_fs_error(FS_ERROR_NOT_DIRECTORY); return -1; }
    int slot = -1;
    for (int i = 0; i < FILES; i++) if (!files[i].used) { slot = i; break; }
    if (slot < 0) { set_fs_error(FS_ERROR_NO_SPACE); return -1; }
    snapshot_slot(slot);
    string_copy(files[slot].name, path, NAME);
    files[slot].data[0] = 0; files[slot].used = 1; files[slot].is_directory = 1;
    if (persist_entry(slot) < 0) {
        restore_slot(slot);
        set_fs_error(FS_ERROR_IO);
        return -1;
    }
    set_fs_error(FS_ERROR_NONE); return 0;
}

int fs_is_directory(const char *path) {
    if (!path || !path[0]) return 1; /* filesystem root */
    if (!fs_path_valid(path)) return 0;
    int slot = find_slot(path);
    return slot >= 0 && files[slot].is_directory;
}

int fs_remove(const char *path) {
    if (!path) { set_fs_error(FS_ERROR_INVALID_ARGUMENT); return -1; }
    if (kstrnlen(path, NAME) >= NAME) { set_fs_error(FS_ERROR_NAME_TOO_LONG); return -1; }
    if (!fs_path_valid(path)) { set_fs_error(FS_ERROR_INVALID_NAME); return -1; }
    int slot = find_slot(path);
    if (slot < 0) { set_fs_error(FS_ERROR_NOT_FOUND); return -1; }
    if (files[slot].is_directory && has_children(path)) {
        set_fs_error(FS_ERROR_DIRECTORY_NOT_EMPTY); return -1;
    }
    u32 off = entry_offset(slot);
    u8 old_entry[DIR_ENTRY_SIZE];
    for (usize i = 0; i < DIR_ENTRY_SIZE; ++i) old_entry[i] = directory[off + i];
    if (persist_remove(slot) < 0) {
        for (usize i = 0; i < DIR_ENTRY_SIZE; ++i) directory[off + i] = old_entry[i];
        set_fs_error(FS_ERROR_IO);
        return -1;
    }
    files[slot].used = 0; files[slot].is_directory = 0;
    files[slot].name[0] = 0; files[slot].data[0] = 0;
    set_fs_error(FS_ERROR_NONE); return 0;
}

int fs_copy(const char *source, const char *destination) {
    const char *contents;
    if (!source || !destination) { set_fs_error(FS_ERROR_INVALID_ARGUMENT); return -1; }
    if (string_equal(source, destination)) { set_fs_error(FS_ERROR_ALREADY_EXISTS); return -1; }
    contents = fs_read(source);
    if (!contents) return -1;
    if (find_slot(destination) >= 0) { set_fs_error(FS_ERROR_ALREADY_EXISTS); return -1; }
    return fs_write(destination, contents);
}

int fs_move(const char *source, const char *destination) {
    if (!source || !destination) { set_fs_error(FS_ERROR_INVALID_ARGUMENT); return -1; }
    if (string_equal(source, destination)) { set_fs_error(FS_ERROR_ALREADY_EXISTS); return -1; }
    if (find_slot(destination) >= 0) { set_fs_error(FS_ERROR_ALREADY_EXISTS); return -1; }
    if (fs_copy(source, destination) < 0) return -1;
    if (fs_remove(source) < 0) {
        /* Do not leave a half-completed move when source deletion fails. */
        (void)fs_remove(destination);
        return -1;
    }
    set_fs_error(FS_ERROR_NONE);
    return 0;
}

int fs_rename(const char *source, const char *destination) {
    return fs_move(source, destination);
}

int fs_exists(const char *path) {
    if (!path || !fs_path_valid(path)) return 0;
    return find_slot(path) >= 0;
}

int fs_is_file(const char *path) {
    if (!path || !fs_path_valid(path)) return 0;
    int slot = find_slot(path);
    return slot >= 0 && !files[slot].is_directory;
}

int fs_mkdir_p(const char *path) {
    char current[NAME];
    usize length;
    if (!path) { set_fs_error(FS_ERROR_INVALID_ARGUMENT); return -1; }
    length = kstrnlen(path, NAME);
    if (length >= NAME) { set_fs_error(FS_ERROR_NAME_TOO_LONG); return -1; }
    if (!length) { set_fs_error(FS_ERROR_NONE); return 0; }
    if (!fs_path_valid(path)) { set_fs_error(FS_ERROR_INVALID_NAME); return -1; }
    string_copy(current, path, sizeof(current));
    for (usize i = 0; i <= length; ++i) {
        char separator;
        int slot;
        if (current[i] != '/' && current[i] != '\0') continue;
        separator = current[i];
        current[i] = '\0';
        slot = find_slot(current);
        if (slot >= 0) {
            if (!files[slot].is_directory) {
                set_fs_error(FS_ERROR_NOT_DIRECTORY);
                return -1;
            }
        } else if (fs_mkdir(current) < 0) {
            return -1;
        }
        current[i] = separator;
    }
    set_fs_error(FS_ERROR_NONE);
    return 0;
}

int fs_touch(const char *path) {
    int slot;
    if (!path) { set_fs_error(FS_ERROR_INVALID_ARGUMENT); return -1; }
    if (kstrnlen(path, NAME) >= NAME) { set_fs_error(FS_ERROR_NAME_TOO_LONG); return -1; }
    if (!fs_path_valid(path)) { set_fs_error(FS_ERROR_INVALID_NAME); return -1; }
    slot = find_slot(path);
    if (slot >= 0) {
        if (files[slot].is_directory) { set_fs_error(FS_ERROR_IS_DIRECTORY); return -1; }
        set_fs_error(FS_ERROR_NONE);
        return 0;
    }
    return fs_write(path, "");
}

void fs_seed_user_tree(void) {
    static const char *const directories[] = {
        "home", "home/user", "home/user/documents", "home/user/downloads",
        "home/user/projects", "home/user/examples", "home/user/utilities",
        "home/user/pictures", "home/user/music", "home/user/videos",
        "home/user/screenshots", "home/user/games", "home/user/bin",
        "home/user/tmp", "bin", "sbin", "var", "var/log", "etc", "etc/denos"
    };
    for (usize i = 0; i < sizeof(directories) / sizeof(directories[0]); i++)
        if (!fs_is_directory(directories[i])) (void)fs_mkdir(directories[i]);
    if (!fs_read("home/user/documents/welcome.md"))
        (void)fs_write("home/user/documents/welcome.md",
            "# Welcome to DenOS\n\n"
            "DenOS is an experimental 64-bit terminal-first operating system.\n\n"
            "Use `help` for commands, `apps` for native terminal apps, `monitor` for\n"
            "live system statistics, and `arcade` for the built-in text games.\n\n"
            "Your home folder contains documents, downloads, projects, examples,\n"
            "utilities, games, pictures, music, videos, screenshots, bin, and tmp.\n\n"
            "Bundled Aiden C examples live in examples/, games/, and utilities/.\n"
            "Compile one with `cc examples/hello.c -o hello`, then `run hello`.\n\n"
            "Aiden C compiles its supported C subset to AEX bytecode. Use `cc` to\n"
            "compile a source file and `run` to execute a trusted AEX program.\n");
    if (!fs_read("home/user/projects/hello.c"))
        (void)fs_write("home/user/projects/hello.c",
            "#include <stdio.h>\n\n"
            "int main(void) {\n"
            "    printf(\"Hello from Aiden C!\\n\");\n"
            "    return 0;\n"
            "}\n");
    if (!fs_read("home/user/documents/quick-notes.md"))
        (void)fs_write("home/user/documents/quick-notes.md", "# Quick notes\n\nStart writing here.\n");
}

void fs_list(void) {
    int any = 0;
    for (int i = 0; i < FILES; i++) if (files[i].used) {
        serial_write(files[i].name);
        if (files[i].is_directory) serial_write("/");
        serial_write("\n"); any = 1;
    }
    if (!any) serial_write("(empty)\n");
}

usize fs_file_count(void) {
    usize n = 0;
    for (int i = 0; i < FILES; i++) if (files[i].used && !files[i].is_directory) n++;
    return n;
}
usize fs_entry_count(void) { return (usize)used_slots(); }

int fs_get_entry(usize index, char *name, usize name_capacity, usize *size) {
    if (!name || !name_capacity || !size) { set_fs_error(FS_ERROR_INVALID_ARGUMENT); return -1; }
    usize current = 0;
    for (int slot = 0; slot < FILES; slot++) {
        if (!files[slot].used) continue;
        if (current++ != index) continue;
        usize length = string_length(files[slot].name, NAME);
        if (length + 1 > name_capacity) { name[0] = 0; set_fs_error(FS_ERROR_BUFFER_TOO_SMALL); return -1; }
        string_copy(name, files[slot].name, name_capacity);
        *size = files[slot].is_directory ? 0 : string_length(files[slot].data, DATA);
        set_fs_error(FS_ERROR_NONE); return 0;
    }
    name[0] = 0; *size = 0; set_fs_error(FS_ERROR_NOT_FOUND); return -1;
}

static int entry_parent_matches(const char *entry_path, const char *directory_path) {
    char parent[NAME];
    if (fs_path_parent(entry_path, parent, sizeof(parent)) < 0) return 0;
    return string_equal(parent, directory_path ? directory_path : "");
}

usize fs_entry_count_in_dir(const char *directory_path) {
    usize count = 0;
    if (!directory_path) return 0;
    if (directory_path[0] && !fs_is_directory(directory_path)) return 0;
    for (int slot = 0; slot < FILES; slot++)
        if (files[slot].used && entry_parent_matches(files[slot].name, directory_path)) count++;
    return count;
}

int fs_get_entry_in_dir(const char *directory_path, usize index,
                        char *name, usize name_capacity,
                        usize *size, int *entry_type) {
    usize current = 0;
    if (!directory_path || !name || !name_capacity || !size || !entry_type) {
        set_fs_error(FS_ERROR_INVALID_ARGUMENT); return -1;
    }
    if (directory_path[0] && !fs_is_directory(directory_path)) {
        set_fs_error(FS_ERROR_NOT_DIRECTORY); return -1;
    }
    for (int slot = 0; slot < FILES; slot++) {
        if (!files[slot].used || !entry_parent_matches(files[slot].name, directory_path)) continue;
        if (current++ != index) continue;
        const char *leaf = fs_path_leaf(files[slot].name);
        usize length = string_length(leaf, NAME);
        if (length + 1 > name_capacity) { name[0] = 0; set_fs_error(FS_ERROR_BUFFER_TOO_SMALL); return -1; }
        string_copy(name, leaf, name_capacity);
        *size = files[slot].is_directory ? 0 : string_length(files[slot].data, DATA);
        *entry_type = files[slot].is_directory ? FS_ENTRY_DIRECTORY : FS_ENTRY_FILE;
        set_fs_error(FS_ERROR_NONE); return 0;
    }
    name[0] = 0; *size = 0; *entry_type = 0; set_fs_error(FS_ERROR_NOT_FOUND); return -1;
}

usize fs_bytes_used(void) {
    usize n = 0;
    for (int i = 0; i < FILES; i++)
        if (files[i].used && !files[i].is_directory) n += string_length(files[i].data, DATA);
    return n;
}
usize fs_storage_capacity(void) { return (usize)FILES * (usize)SLOT_DATA_CAPACITY; }
usize fs_storage_free(void) {
    usize count = (usize)(FILES - used_slots());
    return count * SLOT_DATA_CAPACITY;
}
usize fs_storage_reserved(void) {
    usize allocated = (usize)used_slots() * SLOT_DATA_CAPACITY;
    usize used = fs_bytes_used();
    return allocated > used ? allocated - used : 0;
}
usize fs_disk_size_bytes(void) { return (usize)ata_sector_count() * (usize)SECTOR_SIZE; }
u32 fs_disk_sector_count(void) { return ata_sector_count(); }
const char *fs_disk_model(void) { return ata_model(); }
int fs_is_persistent(void) { return disk_ready; }
const char *fs_storage_name(void) { return storage_status; }
