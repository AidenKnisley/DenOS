#include <stdio.h>
#include "parse.h"
int main(void) { u64 n;u32 small;u8 ip[4]; if(parse_u64("18446744073709551615",&n)<0||n!=~(u64)0)return 1; if(parse_u64("18446744073709551616",&n)==0)return 2; if(parse_u32("0xdeadbeef",&small)<0||small!=0xdeadbeefu)return 3; if(parse_ipv4("10.0.2.15",ip)<0||ip[0]!=10||ip[3]!=15)return 4; if(parse_ipv4("10.0.2.256",ip)==0||parse_ipv4("1.2.3",ip)==0)return 5; puts("Parser tests passed.");return 0; }
