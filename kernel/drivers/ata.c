#include "ata.h"
#include "io.h"

/* Legacy PC IDE ports. QEMU's pc machine exposes both channels. */
#define ATA_PRIMARY_BASE   0x1F0
#define ATA_PRIMARY_CTRL   0x3F6
#define ATA_SECONDARY_BASE 0x170
#define ATA_SECONDARY_CTRL 0x376

#define ATA_REG_DATA       0
#define ATA_REG_ERROR      1
#define ATA_REG_FEATURES   1
#define ATA_REG_SECCOUNT   2
#define ATA_REG_LBA0       3
#define ATA_REG_LBA1       4
#define ATA_REG_LBA2       5
#define ATA_REG_DRIVE      6
#define ATA_REG_STATUS     7
#define ATA_REG_COMMAND    7

#define ATA_SR_BSY 0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF  0x20
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

#define ATA_CMD_READ       0x20
#define ATA_CMD_WRITE      0x30
#define ATA_CMD_IDENTIFY   0xEC
#define ATA_CMD_FLUSH      0xE7
#define ATA_TIMEOUT        1000000u
#define ATA_LBA28_LIMIT    0x10000000u

enum probe_result {
    PROBE_OK = 0,
    PROBE_ABSENT,
    PROBE_TIMEOUT,
    PROBE_NOT_ATA,
    PROBE_NO_LBA,
    PROBE_BAD_CAPACITY
};

typedef struct {
    u16 io_base;
    u16 control;
    const char *name;
} ata_channel_t;

static const ata_channel_t ata_channels[] = {
    { ATA_PRIMARY_BASE, ATA_PRIMARY_CTRL, "primary" },
    { ATA_SECONDARY_BASE, ATA_SECONDARY_CTRL, "secondary" }
};

static int ata_available;
static u16 ata_io_base;
static u16 ata_control;
static u8 ata_drive_head;
static u32 ata_sectors;
static int ata_flush_supported;
static char ata_model_name[41];
static const char *ata_diagnostic_text = "ATA probe has not run";

/* Four alternate-status reads satisfy the ATA 400 ns bus delay. */
static void ata_delay_400ns(u16 control_port) {
    (void)inb(control_port);
    (void)inb(control_port);
    (void)inb(control_port);
    (void)inb(control_port);
}

/* Poll BSY without treating the DRQ bit as an error. */
static int wait_not_busy(u16 base) {
    for (u32 i = 0; i < ATA_TIMEOUT; i++) {
        u8 status = inb((u16)(base + ATA_REG_STATUS));
        if (status == 0x00 || status == 0xFF) return -1;
        if (!(status & ATA_SR_BSY)) {
            if (status & (ATA_SR_ERR | ATA_SR_DF)) return -1;
            return 0;
        }
        __asm__ volatile("pause");
    }
    return -1;
}

static int wait_data_request(u16 base) {
    for (u32 i = 0; i < ATA_TIMEOUT; i++) {
        u8 status = inb((u16)(base + ATA_REG_STATUS));
        if (status == 0x00 || status == 0xFF) return -1;
        if (status & (ATA_SR_ERR | ATA_SR_DF)) return -1;
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) return 0;
        __asm__ volatile("pause");
    }
    return -1;
}


/*
 * Identify one device on one IDE channel. The IDENTIFY data phase is only
 * read after BSY clears and DRQ is observed; this avoids racing a slower
 * controller or virtual device immediately after issuing command 0xEC.
 */
static enum probe_result identify_device(const ata_channel_t *channel,
                                         u8 slave,
                                         u32 *sectors_out,
                                         int *flush_out,
                                         char model_out[41]) {
    u16 base = channel->io_base;
    u8 select = (u8)(0xA0 | (slave ? 0x10 : 0x00));
    u16 identify[256];

    outb((u16)(base + ATA_REG_DRIVE), select);
    ata_delay_400ns(channel->control);

    u8 status = inb((u16)(base + ATA_REG_STATUS));
    if (status == 0x00 || status == 0xFF) return PROBE_ABSENT;
    if (wait_not_busy(base) < 0) return PROBE_TIMEOUT;

    /* Clear task-file registers before issuing IDENTIFY DEVICE. */
    outb((u16)(base + ATA_REG_SECCOUNT), 0);
    outb((u16)(base + ATA_REG_LBA0), 0);
    outb((u16)(base + ATA_REG_LBA1), 0);
    outb((u16)(base + ATA_REG_LBA2), 0);
    outb((u16)(base + ATA_REG_COMMAND), ATA_CMD_IDENTIFY);
    ata_delay_400ns(channel->control);

    /* A floating bus or a missing device commonly reads as 0x00/0xFF. */
    status = inb((u16)(base + ATA_REG_STATUS));
    if (status == 0x00 || status == 0xFF) return PROBE_ABSENT;

    /* Wait for command completion, then distinguish ATA from ATAPI. */
    int data_ready = 0;
    for (u32 i = 0; i < ATA_TIMEOUT; i++) {
        status = inb((u16)(base + ATA_REG_STATUS));
        if (status == 0x00 || status == 0xFF) return PROBE_ABSENT;
        if (status & ATA_SR_BSY) {
            __asm__ volatile("pause");
            continue;
        }
        if (status & (ATA_SR_ERR | ATA_SR_DF)) return PROBE_NOT_ATA;

        /* ATAPI optical drives report a non-zero IDENTIFY signature here. */
        if (inb((u16)(base + ATA_REG_LBA1)) != 0 ||
            inb((u16)(base + ATA_REG_LBA2)) != 0)
            return PROBE_NOT_ATA;

        if (status & ATA_SR_DRQ) {
            data_ready = 1;
            break;
        }
        __asm__ volatile("pause");
    }
    if (!data_ready) return PROBE_TIMEOUT;

    for (u32 i = 0; i < 256; i++)
        identify[i] = inw((u16)(base + ATA_REG_DATA));

    if (wait_not_busy(base) < 0) return PROBE_TIMEOUT;

    /* Word 49 bit 9 advertises LBA; words 60-61 contain the LBA28 size. */
    if (!(identify[49] & (1u << 9))) return PROBE_NO_LBA;

    u32 sectors = (u32)identify[60] | ((u32)identify[61] << 16);
    if (!sectors) return PROBE_BAD_CAPACITY;
    if (sectors > ATA_LBA28_LIMIT) sectors = ATA_LBA28_LIMIT;

    *sectors_out = sectors;
    *flush_out = ((identify[83] & 0xC000u) == 0x4000u) &&
                 ((identify[83] & (1u << 12)) != 0);

    /* Decode to a temporary buffer so a failed probe cannot publish it. */
    for (usize i = 0; i < 41; i++) model_out[i] = 0;
    usize out = 0;
    for (u32 word = 27; word <= 46 && out < 40; word++) {
        model_out[out++] = (char)(identify[word] >> 8);
        model_out[out++] = (char)(identify[word] & 0xFF);
    }
    model_out[40] = 0;
    while (out && model_out[out - 1] == ' ') model_out[--out] = 0;
    usize first = 0;
    while (first < out && model_out[first] == ' ') first++;
    if (first) {
        usize i = 0;
        while (first + i < out) {
            model_out[i] = model_out[first + i];
            i++;
        }
        model_out[i] = 0;
    }
    if (!model_out[0]) {
        const char fallback[] = "ATA-compatible disk";
        for (usize i = 0; i < sizeof(fallback); i++) model_out[i] = fallback[i];
    }

    return PROBE_OK;
}

static const char *probe_error_text(enum probe_result result) {
    switch (result) {
        case PROBE_TIMEOUT: return "IDE device did not complete ATA IDENTIFY";
        case PROBE_NOT_ATA: return "IDE device is present but is not an ATA hard disk";
        case PROBE_NO_LBA: return "ATA device found but it does not support LBA";
        case PROBE_BAD_CAPACITY: return "ATA device reported an invalid capacity";
        case PROBE_ABSENT:
        case PROBE_OK:
        default: return "no ATA disk found on either IDE channel";
    }
}

int ata_init(void) {
    ata_available = 0;
    ata_io_base = 0;
    ata_control = 0;
    ata_drive_head = 0;
    ata_sectors = 0;
    ata_flush_supported = 0;
    ata_model_name[0] = 0;
    ata_diagnostic_text = "no ATA disk found on either IDE channel";

    enum probe_result best_error = PROBE_ABSENT;
    char detected_model[41];
    u32 detected_sectors = 0;
    int detected_flush = 0;

    /* Probe primary and secondary IDE channels, master and slave on each. */
    for (usize channel_index = 0;
         channel_index < sizeof(ata_channels) / sizeof(ata_channels[0]);
         channel_index++) {
        const ata_channel_t *channel = &ata_channels[channel_index];
        for (u8 slave = 0; slave < 2; slave++) {
            enum probe_result result = identify_device(channel, slave,
                                                        &detected_sectors,
                                                        &detected_flush,
                                                        detected_model);
            if (result == PROBE_OK) {
                ata_io_base = channel->io_base;
                ata_control = channel->control;
                ata_drive_head = (u8)(0xE0 | (slave ? 0x10 : 0x00));
                ata_sectors = detected_sectors;
                ata_flush_supported = detected_flush;
                for (usize i = 0; i < sizeof(ata_model_name); i++)
                    ata_model_name[i] = detected_model[i];
                ata_available = 1;
                ata_diagnostic_text = channel_index == 0
                    ? (slave ? "ATA disk detected on primary IDE slave"
                             : "ATA disk detected on primary IDE master")
                    : (slave ? "ATA disk detected on secondary IDE slave"
                             : "ATA disk detected on secondary IDE master");
                return 0;
            }
            if (result != PROBE_ABSENT && best_error == PROBE_ABSENT)
                best_error = result;
        }
    }

    ata_diagnostic_text = probe_error_text(best_error);
    return -1;
}

int ata_is_available(void) {
    return ata_available;
}

u32 ata_sector_count(void) {
    return ata_available ? ata_sectors : 0;
}

const char *ata_model(void) {
    return ata_available ? ata_model_name : "No ATA disk detected";
}

const char *ata_diagnostic(void) {
    return ata_diagnostic_text;
}

static int select_lba(u32 lba) {
    if (!ata_available || lba >= ata_sectors || lba >= ATA_LBA28_LIMIT)
        return -1;

    outb((u16)(ata_io_base + ATA_REG_DRIVE),
         (u8)(ata_drive_head | ((lba >> 24) & 0x0F)));
    ata_delay_400ns(ata_control);
    if (wait_not_busy(ata_io_base) < 0) return -1;
    outb((u16)(ata_io_base + ATA_REG_SECCOUNT), 1);
    outb((u16)(ata_io_base + ATA_REG_LBA0), (u8)(lba & 0xFF));
    outb((u16)(ata_io_base + ATA_REG_LBA1), (u8)((lba >> 8) & 0xFF));
    outb((u16)(ata_io_base + ATA_REG_LBA2), (u8)((lba >> 16) & 0xFF));
    return wait_not_busy(ata_io_base);
}

int ata_read_sector(u32 lba, void *buffer) {
    if (!buffer || select_lba(lba) < 0) return -1;

    outb((u16)(ata_io_base + ATA_REG_COMMAND), ATA_CMD_READ);
    if (wait_data_request(ata_io_base) < 0) return -1;

    u8 *bytes = (u8 *)buffer;
    for (u32 i = 0; i < 256; i++) {
        u16 value = inw((u16)(ata_io_base + ATA_REG_DATA));
        bytes[i * 2] = (u8)(value & 0xFF);
        bytes[i * 2 + 1] = (u8)(value >> 8);
    }
    ata_delay_400ns(ata_control);
    return wait_not_busy(ata_io_base);
}

int ata_write_sector(u32 lba, const void *buffer) {
    if (!buffer || select_lba(lba) < 0) return -1;

    outb((u16)(ata_io_base + ATA_REG_COMMAND), ATA_CMD_WRITE);
    if (wait_data_request(ata_io_base) < 0) return -1;

    const u8 *bytes = (const u8 *)buffer;
    for (u32 i = 0; i < 256; i++) {
        u16 value = (u16)bytes[i * 2] | ((u16)bytes[i * 2 + 1] << 8);
        outw((u16)(ata_io_base + ATA_REG_DATA), value);
    }

    ata_delay_400ns(ata_control);
    return wait_not_busy(ata_io_base);
}

int ata_flush(void) {
    if (!ata_available || wait_not_busy(ata_io_base) < 0) return -1;

    /* Older ATA disks may not implement FLUSH CACHE. */
    if (!ata_flush_supported) return 0;

    outb((u16)(ata_io_base + ATA_REG_COMMAND), ATA_CMD_FLUSH);
    return wait_not_busy(ata_io_base);
}
