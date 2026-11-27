#include "builtin_image.h"
#include <string.h>
static uint32_t read32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int hbloaderCopyBuiltin(void *staging,size_t capacity,const void *image,size_t bytes){
 if(!staging||!image||bytes<128)return -1;
 const unsigned char*h=image;uint32_t size=read32(h+24),bss=read32(h+56),mod=read32(h+4);
 if(memcmp(h+16,"NRO0",4)||size<4096||size!=bytes||size>capacity||bss>=capacity-size||((size|bss)&4095)||mod<128||mod>size-28)return -1;
 uint64_t end=0;
 for(unsigned i=0;i<3;i++){uint32_t at=read32(h+32+i*8),n=read32(h+36+i*8);if(at!=end||((at|n)&4095)||(!i&&n<4096))return -1;end=(uint64_t)at+n;if(end>size)return -1;}
 if(end!=size)return -1;
 memcpy(staging,image,size);return 0;
}
