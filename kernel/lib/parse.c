#include "parse.h"
int parse_u64(const char *text, u64 *value) {
    u64 result=0; if (!text || !*text || !value) return -1;
    if (text[0]=='0' && (text[1]=='x'||text[1]=='X')) { text+=2; if (!*text) return -1; for (;*text;text++) { u32 d; if (*text>='0'&&*text<='9') d=(u32)(*text-'0'); else if (*text>='a'&&*text<='f') d=(u32)(*text-'a'+10); else if (*text>='A'&&*text<='F') d=(u32)(*text-'A'+10); else return -1; if (result>(~(u64)0-d)/16u) return -1; result=result*16u+d; } }
    else { for (;*text;text++) { if (*text<'0'||*text>'9') return -1; u32 d=(u32)(*text-'0'); if (result>(~(u64)0-d)/10u) return -1; result=result*10u+d; } }
    *value=result; return 0;
}
int parse_u32(const char *text, u32 *value) { u64 parsed; if (!value || parse_u64(text,&parsed)<0 || parsed>0xFFFFFFFFull) return -1; *value=(u32)parsed; return 0; }
int parse_ipv4(const char *text, u8 address[4]) {
    u8 temp[4]; if (!text||!address) return -1;
    for (u32 i=0;i<4;i++) { u32 value=0,digits=0; while (*text>='0'&&*text<='9') { if (digits==3) return -1; value=value*10u+(u32)(*text-'0'); if (value>255u) return -1; ++digits; ++text; } if (!digits) return -1; temp[i]=(u8)value; if (i<3) { if (*text!='.') return -1; ++text; } else if (*text) return -1; }
    for (u32 i=0;i<4;i++) address[i]=temp[i];
    return 0;
}
