#include <stdio.h>
#include <string.h>
#include "elf64.h"
static void le16(unsigned char *p,unsigned v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}static void le64(unsigned char *p,unsigned long long v){for(int i=0;i<8;i++)p[i]=(unsigned char)(v>>(i*8));}
int main(void){unsigned char image[128];elf64_info_t info;memset(image,0,sizeof(image));image[0]=0x7F;image[1]='E';image[2]='L';image[3]='F';image[4]=2;image[5]=1;image[6]=1;le16(image+16,2);le16(image+18,62);le64(image+24,0x100000);le16(image+52,64);le16(image+54,56);le16(image+56,1);le16(image+58,64);le16(image+60,0);le64(image+32,64);image[64]=1;le64(image+72,120);le64(image+96,8);le64(image+104,16);le64(image+112,1);if(elf64_validate(image,sizeof(image),&info)<0||info.machine!=62||info.program_headers!=1)return 1;if(elf64_validate(image,100,&info)==0)return 2;image[4]=1;if(elf64_validate(image,sizeof(image),&info)==0)return 3;puts("ELF64 validation tests passed.");return 0;}
