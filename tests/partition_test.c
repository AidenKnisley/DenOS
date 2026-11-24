#include <stdio.h>
#include <string.h>
#include "partition.h"
static u8 disk[512];static int reader(void *ctx,u32 lba,void *out){(void)ctx;if(lba)return -1;memcpy(out,disk,sizeof(disk));return 0;}
static void le32(u8 *p,u32 v){p[0]=(u8)v;p[1]=(u8)(v>>8);p[2]=(u8)(v>>16);p[3]=(u8)(v>>24);}
int main(void){partition_entry_t entries[4];memset(disk,0,sizeof(disk));disk[446]=0x80;disk[450]=0x83;le32(disk+454,2048);le32(disk+458,4096);disk[510]=0x55;disk[511]=0xAA;int n=partition_scan_mbr(reader,0,10000,entries,4);if(n!=1||entries[0].bootable!=1||entries[0].type!=0x83||entries[0].first_lba!=2048||entries[0].sectors!=4096)return 1;if(partition_scan_mbr(reader,0,3000,entries,4)!=0)return 2;disk[510]=0;if(partition_scan_mbr(reader,0,10000,entries,4)>=0)return 3;puts("Partition parser tests passed.");return 0;}
