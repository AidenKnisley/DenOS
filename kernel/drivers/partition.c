#include "partition.h"
#include "kstring.h"
static u32 read_le32(const u8 *p) { return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
int partition_scan_mbr(partition_read_fn reader,void *context,u32 disk_sectors,partition_entry_t *out,usize capacity) {
    u8 sector[512];usize found=0;if(!reader||(!out&&capacity))return -1;if(reader(context,0,sector)<0)return -1;if(sector[510]!=0x55||sector[511]!=0xAA)return -1;
    for(usize i=0;i<PARTITION_MAX_MBR;i++){const u8 *entry=sector+446u+i*16u;u8 status=entry[0],type=entry[4];u32 start=read_le32(entry+8),count=read_le32(entry+12);if(!type||!count)continue;if(status!=0&&status!=0x80)continue;if(start==0||count>0xFFFFFFFFu-start)continue;if(disk_sectors&&(start>=disk_sectors||count>disk_sectors-start))continue;if(found<capacity){out[found].bootable=(u8)(status==0x80);out[found].type=type;out[found].first_lba=start;out[found].sectors=count;}++found;}
    return (int)(found<capacity?found:capacity);
}
const char *partition_type_name(u8 t) { switch(t){case 0x01:return "FAT12";case 0x04:case 0x06:case 0x0E:return "FAT16";case 0x07:return "NTFS/exFAT/HPFS";case 0x0B:case 0x0C:return "FAT32";case 0x82:return "Linux swap";case 0x83:return "Linux filesystem";case 0x85:return "Linux extended";case 0xEE:return "GPT protective";case 0xEF:return "EFI system";default:return "Unknown";} }
