#include "av.h"
#include "gpu-profile.h"
#define LIMIT (UINT64_C(1)<<39)
static int range(uint64_t p,uint64_t n,uint64_t alignment){return p&&p<LIMIT&&!(p&(alignment-1))&&n&&n<=LIMIT-p;}
static int overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m){return a<b+m&&b<a+n;}
static void zero(void*p,size_t n){uint8_t*b=p;for(size_t i=0;i<n;i++)b[i]=0;}
static void copy(void*p,const void*q,size_t n){uint8_t*b=p;const uint8_t*a=q;for(size_t i=0;i<n;i++)b[i]=a[i];}
static void u32(uint8_t*b,unsigned at,uint32_t v){for(unsigned i=0;i<4;i++)b[at+i]=(uint8_t)(v>>(8*i));}
static void u64(uint8_t*b,unsigned at,uint64_t v){for(unsigned i=0;i<8;i++)b[at+i]=(uint8_t)(v>>(8*i));}
static void lease(const IzPs2AvInput*in,unsigned slot,uint8_t*b){
 const IzPs2VideoSource*s=&in->sources[slot];u32(b,0,0x32564c50);u32(b,4,1);u32(b,8,224);u32(b,12,7);copy(b+16,in->token,16);
 u64(b,32,in->device);u64(b,40,in->queue);u64(b,48,in->swapchain);u64(b,56,s->semaphores[0]);
 for(unsigned i=0;i<8;i++){u64(b,64+i*8,s->commands[i]);u64(b,128+i*8,s->semaphores[i+1]);}
 u64(b,192,s->mapped);u64(b,200,IZ_PS2_VIDEO_BYTES);u32(b,208,s->memory_flags);u32(b,212,in->image_count);u32(b,216,1024);u32(b,220,1024);
}
__attribute__((section(".text.iz_ps2_pack_av")))
uint32_t iz_ps2_pack_av(const IzPs2AvInput*in,uint8_t out[4096]){
 if(!in||!out||in->version!=1||in->bytes!=sizeof *in||in->reserved||in->reserved2||!in->image_count||in->image_count>8||in->protected_count>24)return 0xed01;
 uint64_t a=(uint64_t)(uintptr_t)in,b=(uint64_t)(uintptr_t)out;
 if(a>UINT64_MAX-sizeof *in||b>UINT64_MAX-4096||overlap(a,sizeof *in,b,4096))return 0xed01;
 if(!range(in->device,8,8)||!range(in->queue,8,8)||!in->swapchain||in->scratch_bytes<IZ_PS2_SCANOUT_BYTES||in->scratch_bytes%32)return 0xed02;
 unsigned nonzero=0;for(unsigned i=0;i<16;i++)nonzero|=in->token[i];if(!nonzero)return 0xed03;
 IzPs2AvSpan spans[30];spans[0]=(IzPs2AvSpan){in->av_page,4096};spans[1]=(IzPs2AvSpan){in->vulkan,IZ_PS2_VULKAN_BYTES};
 spans[2]=(IzPs2AvSpan){in->audio,IZ_PS2_AUDIO_BYTES};spans[3]=(IzPs2AvSpan){in->scratch,in->scratch_bytes};
 spans[4]=(IzPs2AvSpan){in->sources[0].mapped,IZ_PS2_VIDEO_BYTES};spans[5]=(IzPs2AvSpan){in->sources[1].mapped,IZ_PS2_VIDEO_BYTES};
 for(unsigned i=0;i<in->protected_count;i++)spans[6+i]=in->protected[i];
 unsigned count=6+in->protected_count;
 for(unsigned i=0;i<count;i++){
  uint64_t alignment=i==3?32:4096;
  if(!range(spans[i].base,spans[i].bytes,alignment)||(i>=6&&spans[i].bytes%4096))return 0xed04;
  for(unsigned j=0;j<i;j++)if(overlap(spans[i].base,spans[i].bytes,spans[j].base,spans[j].bytes))return 0xed04;
 }
 for(unsigned i=0;i<2;i++){
  const IzPs2VideoSource*s=&in->sources[i];
  if(s->reserved||s->mapped_bytes!=IZ_PS2_VIDEO_BYTES||(s->memory_flags&6)!=6||!s->buffer||!s->memory||!s->set_layout||!s->layout||!s->pool||!s->descriptor)return 0xed05;
  for(unsigned j=0;j<8;j++)if(j<in->image_count?!s->commands[j]:s->commands[j]!=0)return 0xed05;
  for(unsigned j=0;j<9;j++)if(j<=in->image_count?!s->semaphores[j]:s->semaphores[j]!=0)return 0xed05;
 }
 /* Opaque handles need not be addresses. Uniqueness is required within each
  * object class, including across the two separately allocated sources. */
 const IzPs2VideoSource*x=&in->sources[0],*y=&in->sources[1];
 if(x->buffer==y->buffer||x->memory==y->memory||x->set_layout==y->set_layout||x->layout==y->layout||x->pool==y->pool||x->descriptor==y->descriptor)return 0xed06;
 for(unsigned i=0;i<2;i++)for(unsigned j=0;j<in->image_count;j++)for(unsigned k=0;k<=i;k++)for(unsigned n=0;n<in->image_count;n++)
  if((k<i||n<j)&&in->sources[i].commands[j]==in->sources[k].commands[n])return 0xed06;
 for(unsigned i=0;i<2;i++)for(unsigned j=0;j<=in->image_count;j++)for(unsigned k=0;k<=i;k++)for(unsigned n=0;n<=in->image_count;n++)
  if((k<i||n<j)&&in->sources[i].semaphores[j]==in->sources[k].semaphores[n])return 0xed06;
 zero(out,4096);u32(out,0,0x32415650);u32(out,4,1);u32(out,8,288);u32(out,12,1);
 u64(out,16,in->vulkan);u64(out,24,IZ_PS2_VULKAN_BYTES);u64(out,32,in->audio);u64(out,40,IZ_PS2_AUDIO_BYTES);u64(out,48,in->scratch);u64(out,56,in->scratch_bytes);lease(in,0,out+64);
 uint8_t*t=out+512;u32(t,0,0x31505650);u32(t,4,1);u32(t,8,528);u32(t,12,1);copy(t+16,in->token,16);
 u64(t,32,in->av_page);u64(t,40,4096);u64(t,48,in->av_page+512);u64(t,56,in->av_page+64);
 u32(t,64,2);u32(t,68,960);u32(t,72,540);lease(in,0,t+80);lease(in,1,t+304);return 0;
}
