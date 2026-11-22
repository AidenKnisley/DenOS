#ifndef AIDEN_ATA_H
#define AIDEN_ATA_H
#include "types.h"

/* Primary-master ATA PIO, 28-bit LBA, one sector per operation. */
int ata_init(void);
int ata_read_sector(u32 lba, void *buffer);
int ata_write_sector(u32 lba, const void *buffer);
int ata_flush(void);
int ata_is_available(void);

#endif
