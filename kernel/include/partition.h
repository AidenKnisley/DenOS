#ifndef DENOS_PARTITION_H
#define DENOS_PARTITION_H
#include "types.h"
#define PARTITION_MAX_MBR 4u
typedef int (*partition_read_fn)(void *context,u32 lba,void *buffer);
typedef struct partition_entry { u8 bootable; u8 type; u32 first_lba; u32 sectors; } partition_entry_t;
/* Returns the number of valid primary MBR entries, -1 for read/signature errors. */
int partition_scan_mbr(partition_read_fn reader,void *context,u32 disk_sectors,partition_entry_t *out,usize capacity);
const char *partition_type_name(u8 type);
#endif
