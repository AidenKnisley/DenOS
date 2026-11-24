#include <stdio.h>
#include "utf8.h"
int main(void) { const u8 good[]={0x41,0xC2,0xA2,0xE2,0x82,0xAC,0xF0,0x9F,0x8C,0x8D}; const u8 bad1[]={0xC0,0xAF}; const u8 bad2[]={0xED,0xA0,0x80}; u32 cp;usize used; if(!utf8_validate(good,sizeof(good)))return 1; if(utf8_decode(good+3,7,&cp,&used)<0||cp!=0x20AC||used!=3)return 2; if(utf8_validate(bad1,sizeof(bad1))||utf8_validate(bad2,sizeof(bad2)))return 3; puts("UTF-8 tests passed.");return 0; }
