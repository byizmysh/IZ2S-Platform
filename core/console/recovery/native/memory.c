#include "../memory.h"
#include <stdint.h>
void *memcpy(void*out,const void*in,size_t n){uint8_t*a=out;const uint8_t*b=in;for(size_t i=0;i<n;i++)a[i]=b[i];return out;}
void *memset(void*out,int value,size_t n){uint8_t*a=out;for(size_t i=0;i<n;i++)a[i]=(uint8_t)value;return out;}
int memcmp(const void*a,const void*b,size_t n){const uint8_t*x=a,*y=b;for(size_t i=0;i<n;i++)if(x[i]!=y[i])return x[i]<y[i]?-1:1;return 0;}
