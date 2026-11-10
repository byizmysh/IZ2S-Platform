#include "abi.h"
void *memset(void*p,int v,size_t n){u8*b=p;for(size_t i=0;i<n;i++)b[i]=(u8)v;return p;}
void *memcpy(void*p,const void*q,size_t n){u8*a=p;const u8*b=q;for(size_t i=0;i<n;i++)a[i]=b[i];return p;}
int memcmp(const void*p,const void*q,size_t n){const u8*a=p,*b=q;for(size_t i=0;i<n;i++)if(a[i]!=b[i])return a[i]<b[i]?-1:1;return 0;}
size_t strlen(const char*s){size_t n=0;while(s[n])n++;return n;}
u32 iz_crc_update(u32 previous,const void*p,size_t n){const u8*b=p;u32 c=~previous;for(size_t i=0;i<n;i++){c^=b[i];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1));}return ~c;}
u32 iz_crc(const void*p,size_t n){return iz_crc_update(0,p,n);}
static int id(const char*s){unsigned n=0;for(;n<48&&s[n];n++)if(!((s[n]>='a'&&s[n]<='z')||(s[n]>='0'&&s[n]<='9')||(n&&s[n]=='-')))return 0;return n&&n<48;}
u32 iz_validate_header(const IzAppHeader*h,u64 size){
 if(h->version==3){
  if(memcmp(h->magic,"IZSDAPP1",8)||h->header_bytes!=512||size!=512+IZ_TILE_BYTES||h->total_bytes!=size||h->flags!=(IZ_APP_MANAGED|IZ_APP_ASSETS)||!id(h->id)||!h->name[0]||h->name[63]||h->private_bytes||h->repeat||h->reserved[0]||h->reserved[1]!=1||!h->reserved[2]||h->reserved[2]>256u*1024*1024||h->reserved[3]||h->reserved[4])return 0xe10b;
  for(unsigned k=0;k<3;k++)if(h->groups[k]!=1)return 0xe10b;
  u8 binding=0,source=0;for(unsigned k=0;k<32;k++){binding|=h->padding[k];source|=h->padding[k+32];}if(!binding||!source)return 0xe10b;
  for(unsigned k=64;k<sizeof h->padding;k++)if(h->padding[k])return 0xe10b;
  for(unsigned k=0;k<SEC_COUNT;k++){const IzSection*s=&h->section[k];if(k==SEC_TILE){if(s->offset!=512||s->bytes!=IZ_TILE_BYTES||s->reserved)return 0xe10b;}else if(s->offset||s->bytes||s->crc32||s->reserved)return 0xe10b;}return 0;
 }
 int v2=h->version==2,native=(h->flags&IZ_APP_NATIVE)!=0;
 if(memcmp(h->magic,"IZSDAPP1",8)||(h->version!=1&&!v2)||h->header_bytes!=512||h->total_bytes!=size||size>(v2?80u:48u)*1024*1024||(h->flags&~(v2?447u:1u))||!id(h->id)||!h->name[0]||h->name[63])return 0xe101;
 if((h->flags&(IZ_APP_AUDIO_OUT|IZ_APP_ASSETS|IZ_APP_ENGINE_IMAGE|IZ_APP_COOPERATIVE|IZ_APP_WORKER|IZ_APP_PRESENTATION))&&(!native||!v2))return 0xe108;
 if((h->flags&IZ_APP_PRESENTATION)&&!(h->flags&IZ_APP_WORKER))return 0xe108;
 if((h->flags&IZ_APP_WORKER)&&!(h->flags&IZ_APP_COOPERATIVE))return 0xe108;
 if((h->flags&IZ_APP_COOPERATIVE)&&!(h->flags&IZ_APP_ENGINE_IMAGE))return 0xe108;
 if(h->private_bytes>((h->flags&IZ_APP_PRIVATE_128)?128u:32u)*1024*1024||(h->private_bytes&&h->private_bytes%4096)||h->repeat>512)return 0xe102;
 u64 product=1;for(unsigned k=0;k<3;k++){if(!h->groups[k]||h->groups[k]>65535)return 0xe103;product*=h->groups[k];}if(product>4096)return 0xe103;
 unsigned assets=(h->flags&IZ_APP_ASSETS)!=0;
 if(assets){if(!h->reserved[1]||h->reserved[1]>16||!h->reserved[2]||h->reserved[2]>512u*1024*1024)return 0xe10a;u8 hash=0;for(unsigned k=0;k<32;k++)hash|=h->padding[k];if(!hash)return 0xe10a;}
 for(unsigned k=assets?3:native?1:0;k<5;k++)if(h->reserved[k])return 0xe104;
 if(native&&(!h->reserved[0]||h->reserved[0]>32u*1024*1024||h->reserved[0]%4096||(!v2&&h->repeat)))return 0xe108;
 for(unsigned k=assets?32:0;k<sizeof h->padding;k++)if(h->padding[k])return 0xe104;
 if(!v2)for(unsigned k=SEC_NATIVE_CODE;k<SEC_COUNT;k++)if(h->section[k].offset||h->section[k].bytes||h->section[k].crc32||h->section[k].reserved)return 0xe104;
 u64 end=512;for(unsigned k=0;k<SEC_COUNT;k++){const IzSection*s=&h->section[k];if(s->reserved||(!s->bytes&&(s->offset||s->crc32)))return 0xe105;
  if(s->bytes){if(s->offset<end||s->offset%4||s->offset>size||s->bytes>size-s->offset)return 0xe105;end=(u64)s->offset+s->bytes;}}
 if(h->section[SEC_STATE].bytes!=1024||h->section[SEC_DATA].bytes>7u*1024*1024||h->section[SEC_TILE].bytes!=IZ_TILE_BYTES||h->section[SEC_PRIVATE].bytes>h->private_bytes)return 0xe106;
 for(unsigned k=SEC_VERTEX;k<=SEC_AFTER;k++){u32 n=h->section[k].bytes;if(native&&!v2&&k==SEC_BEGIN){if(n<4||n>16u*1024*1024||n%4)return 0xe109;continue;}if(native&&!v2&&k==SEC_AFTER){if(n>h->reserved[0])return 0xe109;continue;}if((k<=SEC_COMPUTE||h->repeat)?(n<20||n>1048576||n%4):n)return 0xe107;}
 if(v2){u32 n=h->section[SEC_NATIVE_CODE].bytes,d=h->section[SEC_NATIVE_DATA].bytes;if(native?(n<4||n>16u*1024*1024||n%4||d>h->reserved[0]):(n||d))return 0xe109;}
 if(h->section[SEC_PAD].bytes<20||h->section[SEC_PAD].bytes>1048576||h->section[SEC_PAD].bytes%4)return 0xe107;
 return 0;
}
