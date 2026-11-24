#include <denos/utf8.h>
#include <stddef.h>
int den_utf8_next(const unsigned char*s,size_t n,size_t*i,unsigned int*out){if(!s||!i||!out||*i>=n)return-1;unsigned c=s[(*i)++],v,min,need;if(c<128){*out=c;return 0;}if(c>=0xc2&&c<=0xdf){v=c&31;need=1;min=0x80;}else if(c>=0xe0&&c<=0xef){v=c&15;need=2;min=0x800;}else if(c>=0xf0&&c<=0xf4){v=c&7;need=3;min=0x10000;}else return-1;if(n-*i<need)return-1;for(unsigned k=0;k<need;k++){unsigned t=s[(*i)++];if((t&0xc0)!=0x80)return-1;v=(v<<6)|(t&63);}if(v<min||v>0x10ffff||(v>=0xd800&&v<=0xdfff))return-1;*out=v;return 0;}
