#include <denos/bitmap.h>
#include <stddef.h>
int den_bit_get(const unsigned char*b,size_t n,size_t i){return b&&i<n?((b[i>>3]>>(i&7))&1):-1;}
int den_bit_set(unsigned char*b,size_t n,size_t i,int v){if(!b||i>=n)return-1;if(v)b[i>>3]|=(unsigned char)(1u<<(i&7));else b[i>>3]&=(unsigned char)~(1u<<(i&7));return 0;}
