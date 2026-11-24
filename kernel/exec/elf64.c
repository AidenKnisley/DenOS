#include "elf64.h"
static u16 le16(const u8 *p){return (u16)(p[0]|((u16)p[1]<<8));}
static u32 le32(const u8 *p){return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);}
static u64 le64(const u8 *p){return (u64)le32(p)|((u64)le32(p+4)<<32);}
static int range_ok(u64 offset,u64 size,usize length){return offset<=(u64)length&&size<=(u64)length-offset;}
int elf64_validate(const void *image,usize length,elf64_info_t *info){
    const u8 *b=(const u8 *)image;u16 type,machine,ehsize,phentsize,phnum,shentsize,shnum;u64 phoff,shoff,entry;
    if(!b||length<64u||b[0]!=0x7F||b[1]!='E'||b[2]!='L'||b[3]!='F'||b[4]!=2||b[5]!=1||b[6]!=1)return -1;
    type=le16(b+16);machine=le16(b+18);entry=le64(b+24);phoff=le64(b+32);shoff=le64(b+40);ehsize=le16(b+52);phentsize=le16(b+54);phnum=le16(b+56);shentsize=le16(b+58);shnum=le16(b+60);
    if((type!=2&&type!=3)||machine!=62||ehsize<64u||ehsize>length)return -1;
    if(phnum&&(phentsize<56u||!range_ok(phoff,(u64)phentsize*phnum,length)))return -1;
    if(shnum&&(shentsize<64u||!range_ok(shoff,(u64)shentsize*shnum,length)))return -1;
    for(u32 i=0;i<phnum;i++){const u8 *p=b+phoff+(u64)i*phentsize;u32 ptype=le32(p);u64 offset=le64(p+8),filesz=le64(p+32),memsz=le64(p+40),align=le64(p+48);if(ptype==1u){if(filesz>memsz||!range_ok(offset,filesz,length))return -1;if(align>1u&&(align&(align-1u)))return -1;}}
    if(info){info->type=type;info->machine=machine;info->program_headers=phnum;info->section_headers=shnum;info->entry=entry;info->program_header_offset=phoff;info->section_header_offset=shoff;}
    return 0;
}
