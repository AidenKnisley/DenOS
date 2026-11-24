#include <denos/crc32.h>
unsigned int den_crc32(const void*data,unsigned long n){const unsigned char*p=data;unsigned int c=~0u;while(n--){c^=*p++;for(int k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}return~c;}
