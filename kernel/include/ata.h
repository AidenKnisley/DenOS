#ifndef AIDEN_ATA_H
#define AIDEN_ATA_H
#include "types.h"

/* Legacy IDE PIO using IDENTIFY DEVICE, both channels, and 28-bit LBA. */
int ata_init(void);
int ata_read_sector(u32 lba, void *buffer);
int ata_write_sector(u32 lba, const void *buffer);
int ata_flush(void);
int ata_is_available(void);
u32 ata_sector_count(void);
const char *ata_model(void);
const char *ata_diagnostic(void);

#endif
