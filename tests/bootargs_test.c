#include <stdio.h>
#include <string.h>
#include "bootargs.h"
int main(void) {
    union { u64 align; u8 bytes[64]; } block;
    u32 *p=(u32 *)block.bytes; char value[16];
    memset(block.bytes,0,sizeof(block.bytes));
    p[0]=56; p[1]=0; p[2]=1; p[3]=33;
    memcpy(block.bytes+16,"safe-mode loglevel=debug",24);
    block.bytes[40]=0;
    p[12]=0; p[13]=8;
    bootargs_init(block.bytes,56);
    if(!bootargs_has("safe-mode")||!bootargs_has("loglevel=debug")) return 1;
    if(bootargs_value("loglevel",value,sizeof(value))<0||strcmp(value,"debug")!=0) return 2;
    if(bootargs_has("safe")) return 3;
    puts("Boot argument tests passed."); return 0;
}
