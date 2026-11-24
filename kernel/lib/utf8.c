#include "utf8.h"
int utf8_decode(const u8 *s, usize n, u32 *cp, usize *used) {
    u32 c; usize need; if (!s||!n||!cp||!used) return -1;
    if (s[0]<0x80) { c=s[0]; need=1; }
    else if (s[0]>=0xC2&&s[0]<=0xDF) { c=s[0]&0x1Fu; need=2; }
    else if (s[0]>=0xE0&&s[0]<=0xEF) { c=s[0]&0x0Fu; need=3; }
    else if (s[0]>=0xF0&&s[0]<=0xF4) { c=s[0]&0x07u; need=4; }
    else return -1;
    if (n<need) return -1;
    for (usize i=1;i<need;i++) { if ((s[i]&0xC0u)!=0x80u) return -1; c=(c<<6)|(s[i]&0x3Fu); }
    if ((need==2&&c<0x80u)||(need==3&&c<0x800u)||(need==4&&c<0x10000u)||c>0x10FFFFu||(c>=0xD800u&&c<=0xDFFFu)) return -1;
    *cp=c; *used=need; return 0;
}
int utf8_validate(const u8 *text, usize length) { usize offset=0,used; u32 cp; if (!text&&length) return 0; while (offset<length) { if (utf8_decode(text+offset,length-offset,&cp,&used)<0) return 0; offset+=used; } return 1; }
