#include <stdio.h>
#include <string.h>
#include "sha256.h"
#include "crc32.h"
#include "uuid.h"
#include "entropy.h"
u64 timer_ticks(void) { return 1234u; }
int main(void) {
    const char *abc="abc"; u8 digest[32]; char hex[65];
    sha256_digest(abc,3,digest); sha256_hex(digest,hex);
    if(strcmp(hex,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")!=0) return 1;
    if(crc32_compute("123456789",9)!=0xCBF43926u) return 2;
    entropy_init(); if(!entropy_is_seeded()||entropy_next()==0) return 3;
    char id[37]; if(uuid_generate_v4(id)<0||strlen(id)!=36||id[14]!='4'||(id[19]!='8'&&id[19]!='9'&&id[19]!='a'&&id[19]!='b')) return 4;
    puts("Crypto utility tests passed."); return 0;
}
