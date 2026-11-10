/* Calls the already opened RO endpoint directly from the console CPU.
 * Only the caller-owned cache is mapped or patched. Every patch checks old bytes.
 */
#include "platform.h"
#include <string.h>
static unsigned request(Ps1Context*c,uint32_t command,const uint64_t*input,unsigned count,uint8_t*buffer){
 memset(buffer,0,4096);uint32_t*p=(void*)buffer;p[0]=4;p[1]=0x80000000u+(32+count*8)/4;p[2]=3;p[5]=0xffff8001u;p[8]=0x49434653;p[10]=command;
 uint64_t*words=(void*)(buffer+48);for(unsigned i=0;i<count;i++)words[i]=input[i];
 c->transport=((uint32_t(*)(void*,uint64_t,uint32_t))(uintptr_t)c->send)(buffer,4096,c->service);
 if(c->transport)return 1;if(!(p[1]&0x80000000u)||p[2]!=1||p[8]!=0x4f434653)return 2;if(((uint64_t)p[3]|((uint64_t)p[4]<<32))!=c->ro_pid)return 3;if(p[10])return 4;return 0;
}
void ps1_ro_map(void){
 Ps1Context*c=ps1_context;if(!c->service||!c->send||!c->pid||!c->ro_pid||!c->jit_source||(c->jit_source|c->jit_rx|c->jit_bytes)&4095||!c->jit_bytes||c->jit_bytes>32*1024*1024||c->pages_mapped)ps1_fatal(101);
 _Alignas(4096) uint8_t message[4096];uint64_t in[4];
 for(uint64_t offset=0;offset<c->jit_bytes;offset+=4096){in[0]=c->pid;in[1]=c->jit_source+offset;in[2]=c->jit_rx+offset;in[3]=4096;c->mapping_unknown=1;unsigned error=request(c,0x524f0012,in,4,message);if(error){if(error==4){uint64_t*out=(void*)(message+48);if(out[3]||(out[4]&&!out[5]))c->mapping_unknown=0;}ps1_fatal(110+error);}c->pages_mapped++;c->mapping_unknown=0;}
 c->publish=(uintptr_t)ps1_ro_publish;
}
/* Explicit cache maintenance is necessary: RO map/unmap handlers do not call
 * publish_code(). The 4-byte stride matches that handler's existing primitive,
 * covers every possible line and avoids importing small-model libgcc CTR/BSS
 * state. EL0 accessibility must be qualified by page-jit-probe on hardware. */
static void clean_source(uint64_t address){
#if defined(__aarch64__)
 for(uint64_t i=0;i<4096;i+=4)__asm__ volatile("dc cvau, %0"::"r"(address+i):"memory");
 __asm__ volatile("dsb ish":::"memory");
#else
 __builtin___clear_cache((char*)(uintptr_t)address,(char*)(uintptr_t)(address+4096));
#endif
}
static void invalidate_rx(uint64_t address){
#if defined(__aarch64__)
 for(uint64_t i=0;i<4096;i+=4)__asm__ volatile("ic ivau, %0"::"r"(address+i):"memory");
 __asm__ volatile("dsb ish; isb":::"memory");
#else
 __builtin___clear_cache((char*)(uintptr_t)address,(char*)(uintptr_t)(address+4096));
#endif
}
static int publish_pages(Ps1Context*c,uint64_t a,uint64_t b){
 if(!c->jit_source||!c->jit_rw||((c->jit_source|c->jit_rx)&4095)||(c->jit_rw&7))return 2;
 _Alignas(4096) uint8_t message[4096];uint8_t expected[4096];
 for(uint64_t page=a&~4095ull;page<b;page+=4096){
  uint64_t lo=a>page?a:page,hi=b<page+4096?b:page+4096,changed[4]={0};unsigned count=0;
  const volatile uint64_t*rx=(void*)(uintptr_t)(c->jit_rx+page);const uint64_t*rw=(void*)(uintptr_t)(c->jit_rw+page);
  memcpy(expected,(const void*)(uintptr_t)(c->jit_rx+page),4096);
  for(uint64_t at=lo;at<hi;at+=16){unsigned index=(unsigned)((at-page)/8);if(rx[index]!=rw[index]||rx[index+1]!=rw[index+1]){unsigned block=index/2;changed[block/64]|=1ull<<(block%64);count++;}}
  if(!count)continue;
  uint64_t in[4]={c->pid,c->jit_source+page,c->jit_rx+page,4096};c->mapping_unknown=1;
  unsigned error=request(c,0x524f0013,in,4,message);if(error)return 30+error;
  c->pages_mapped--; /* Known hole: retain unknown until the page is restored. */
  uint64_t*source=(void*)(uintptr_t)(c->jit_source+page);
  for(unsigned block=0;block<256;block++)if(changed[block/64]&(1ull<<(block%64))){source[block*2]=rw[block*2];source[block*2+1]=rw[block*2+1];memcpy(expected+block*16,rw+block*2,16);}
  clean_source(c->jit_source+page);
  error=request(c,0x524f0012,in,4,message);if(error)return 40+error;
  c->pages_mapped++;
  invalidate_rx(c->jit_rx+page);
  if(memcmp((const void*)(uintptr_t)(c->jit_rx+page),expected,4096))return 50;
  c->mapping_unknown=0;c->jit_patches+=count;c->jit_bytes_published+=(uint64_t)count*16;
 }
 return 0;
}
int ps1_ro_publish(Ps1Context*c,uint64_t a,uint64_t b){
 if(!c||b<a||b>c->jit_bytes||a>c->jit_bytes||c->mapping_unknown)return 1;
 a&=~15ull;b=(b+15)&~15ull;if(b>c->jit_bytes||c->pages_mapped*4096ull!=c->jit_bytes)return 1;
 if(c->base.flags&PS1_FLAG_PAGE_PUBLICATION)return publish_pages(c,a,b);
 _Alignas(4096) uint8_t message[4096];const volatile uint64_t*rx=(void*)(uintptr_t)c->jit_rx;const uint64_t*rw=(void*)(uintptr_t)c->jit_rw;
 for(uint64_t at=a;at<b;at+=16){unsigned index=at/8;uint64_t old0=rx[index],old1=rx[index+1],new0=rw[index],new1=rw[index+1];if(old0==new0&&old1==new1)continue;
  uint64_t in[8]={c->pid,c->jit_rx+at,16,0,old0,old1,new0,new1};unsigned error=request(c,0x524f0014,in,8,message);if(error)return 10+error;uint64_t*out=(void*)(message+48);if(out[6]!=1||out[7]!=1||out[8])return 20;c->jit_patches++;c->jit_bytes_published+=16;
 }return 0;
}
void ps1_ro_unmap(void){Ps1Context*c=ps1_context;_Alignas(4096) uint8_t message[4096];if(c->mapping_unknown)ps1_fatal(121);while(c->pages_mapped){uint64_t offset=(c->pages_mapped-1ull)*4096;uint64_t in[4]={c->pid,c->jit_source+offset,c->jit_rx+offset,4096};c->mapping_unknown=1;unsigned error=request(c,0x524f0013,in,4,message);if(error)ps1_fatal(130+error);c->pages_mapped--;c->mapping_unknown=0;}}
