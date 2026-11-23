/* Freestanding native frontend memory primitives. No import from the PC or
 * loaded engine, no TLS/global allocator, no mutable executable-page state.
 * In particular the framebuffer copy uses unaligned NEON-width transfers. */
#include <stddef.h>
#include <stdint.h>
typedef unsigned char Block16 __attribute__((vector_size(16),aligned(1),may_alias));
void *memcpy(void *restrict destination,const void *restrict source,size_t bytes){
 unsigned char*d=destination;const unsigned char*s=source;void*result=destination;
 while(bytes>=64){
  Block16 a=*(const Block16*)(s),b=*(const Block16*)(s+16),c=*(const Block16*)(s+32),e=*(const Block16*)(s+48);
  *(Block16*)d=a;*(Block16*)(d+16)=b;*(Block16*)(d+32)=c;*(Block16*)(d+48)=e;d+=64;s+=64;bytes-=64;
 }
 while(bytes>=16){*(Block16*)d=*(const Block16*)s;d+=16;s+=16;bytes-=16;}
 while(bytes--)*d++=*s++;
 return result;
}
void *memset(void *destination,int value,size_t bytes){
 unsigned char*d=destination;const unsigned char v=(unsigned char)value;void*result=destination;
 const Block16 block={v,v,v,v,v,v,v,v,v,v,v,v,v,v,v,v};
 while(bytes>=64){*(Block16*)d=block;*(Block16*)(d+16)=block;*(Block16*)(d+32)=block;*(Block16*)(d+48)=block;d+=64;bytes-=64;}
 while(bytes>=16){*(Block16*)d=block;d+=16;bytes-=16;}
 while(bytes--)*d++=v;
 return result;
}
int memcmp(const void *left,const void *right,size_t bytes){
 const unsigned char*a=left,*b=right;
 while(bytes--){const unsigned x=*a++,y=*b++;if(x!=y)return x<y?-1:1;}
 return 0;
}
size_t strlen(const char *text){
 const char *end=text;
 while(*end)++end;
 return (size_t)(end-text);
}
char *strrchr(const char *text,int value){
 const unsigned char byte=(unsigned char)value;
 const char *last=NULL;
 do {
  if((unsigned char)*text==byte)last=text;
 }while(*text++);
 return (char *)last;
}
