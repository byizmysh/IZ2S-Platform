#include "startup.h"
#define PAGE UINT64_C(4096)
#define LIMIT (UINT64_C(1)<<39)
typedef struct {uint64_t base,bytes;} Span;
static int address(uint64_t p,uint64_t alignment){return p&&p<LIMIT&&!(p&(alignment-1));}
static int span(uint64_t p,uint64_t bytes){return address(p,PAGE)&&bytes&&bytes<=LIMIT-p;}
static int overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m){return a<b+m&&b<a+n;}
static uint64_t rounded(uint64_t n){return (n+PAGE-1)&~(PAGE-1);}
static void zero(void*p,size_t bytes){uint8_t*b=p;for(size_t i=0;i<bytes;i++)b[i]=0;}
static void copy(void*p,const void*q,size_t bytes){uint8_t*b=p;const uint8_t*a=q;for(size_t i=0;i<bytes;i++)b[i]=a[i];}
static void u32(void*p,unsigned at,uint32_t v){uint8_t*b=p;for(unsigned i=0;i<4;i++)b[at+i]=(uint8_t)(v>>(i*8));}
static void u64(void*p,unsigned at,uint64_t v){uint8_t*b=p;for(unsigned i=0;i<8;i++)b[at+i]=(uint8_t)(v>>(i*8));}
static uint64_t q64(const void*p){const uint8_t*b=p;uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)b[i]<<(i*8);return v;}
static int path(const char*s,unsigned index,unsigned*length){
 unsigned n=0;while(n<256&&s[n]){unsigned c=(uint8_t)s[n];if(c<32||c==127||c=='\\'||c==':'||(c=='.'&&n&&s[n-1]=='.'))return 0;n++;}
 if(n==256||(!n&&index!=4)||(n&&index!=2&&s[0]!='/'))return 0;
 if(index==2)for(unsigned i=0;i<n;i++)if(s[i]=='/')return 0;
 *length=n;return 1;
}
__attribute__((section(".text.iz_ps2_pack_startup")))
uint32_t iz_ps2_pack_startup(const IzPs2StartupInput*in,uint64_t receipt,IzPs2StartupPages*out){
 if(!in||!out||in->version!=1||in->bytes!=sizeof *in||in->flags&~3u||in->reserved)return 0xeb01;
 /* Do not overwrite the input while constructing output, including unusual
  * caller aliases. This is a logical span check, not a mapping admission. */
 uint64_t a=(uint64_t)(uintptr_t)in,b=(uint64_t)(uintptr_t)out;
 if(a>UINT64_MAX-sizeof *in||b>UINT64_MAX-sizeof *out||overlap(a,sizeof *in,b,sizeof *out))return 0xeb01;
 const IzNativeEnvironment*e=&in->environment;const IzNativeImage*image=&in->image;
 if(e->version!=1||e->bytes!=sizeof *e||e->sdk_bytes!=IZ_PS2_SDK_BYTES||!e->application_pid||!e->ro_pid||e->application_pid>=(UINT64_C(1)<<32)||e->ro_pid>=(UINT64_C(1)<<32)||e->application_pid==e->ro_pid||!e->ro_handle||e->ro_handle>=0xffff8000u||!e->filesystem||!e->tick_hz||e->tick_hz>=1000000000)return 0xeb02;
 if(image->version!=1||image->bytes!=sizeof *image||!image->token||image->code_bytes!=rounded(IZ_PS2_CODE_BYTES)||image->data_bytes!=rounded(IZ_PS2_DATA_BYTES)||(uint64_t)(uintptr_t)image->entry!=(uint64_t)(uintptr_t)image->code||(uint64_t)(uintptr_t)image->spawn!=(uint64_t)(uintptr_t)image->code+120)return 0xeb03;
 if(!(q64(in->main_nonce)|q64(in->main_nonce+8))||!q64(in->pad_nonce)||!q64(in->pad_nonce+8))return 0xeb04;
 unsigned lengths[5];for(unsigned i=0;i<5;i++)if(!path(in->paths[i],i,lengths+i))return 0xeb05;
 int host=(in->flags&IZ_PS2_STARTUP_HOST_FILES)!=0,hwgs=(in->flags&IZ_PS2_STARTUP_HWGS)!=0;
 if((host?in->filesystem!=0:!in->filesystem)||(hwgs?!in->hwgs:in->hwgs!=0))return 0xeb06;
 int gpu=in->gpu_instance||in->gpu_physical||in->gpu_device;
 if(gpu&&(!address(in->gpu_instance,8)||!address(in->gpu_physical,8)||!address(in->gpu_device,8)))return 0xeb07;
 const Span ranges[]={
  {e->sdk,e->sdk_bytes},{(uint64_t)(uintptr_t)image->code,image->code_bytes},{(uint64_t)(uintptr_t)image->data,image->data_bytes},
  {in->context,PAGE},{in->stack,IZ_PS2_STACK_BYTES+2*PAGE},{in->control,PAGE},{in->log,PAGE},{in->ring,32768},{in->dispatch,PAGE},
  {in->vm,PAGE},{in->domains,PAGE},{in->av,PAGE},{in->filesystem,host?0:PAGE},{in->main_descriptor,PAGE},{in->pad,PAGE},{in->save,PAGE},{in->hwgs,hwgs?PAGE:0},
  {receipt,PAGE},{in->main,IZ_PS2_MAIN_BYTES},{in->jit_source,IZ_PS2_JIT_BYTES},{in->jit_shadow,IZ_PS2_JIT_BYTES},{in->jit_rx,IZ_PS2_JIT_BYTES}
 };
 for(unsigned i=0;i<sizeof ranges/sizeof *ranges;i++)if(ranges[i].bytes){if(!span(ranges[i].base,ranges[i].bytes))return 0xeb08;for(unsigned j=0;j<i;j++)if(ranges[j].bytes&&overlap(ranges[i].base,ranges[i].bytes,ranges[j].base,ranges[j].bytes))return 0xeb09;}
 zero(out,sizeof *out);
 /* Original HBX3 context, including its immutable runtime GPU subcontext. */
 uint8_t*c=out->context;u32(c,0,0x33425848);u32(c,4,3);u32(c,8,0x2b8);
 u64(c,48,e->tick_hz);u64(c,80,(uint64_t)(uintptr_t)image->data);u64(c,88,IZ_PS2_DATA_BYTES);u64(c,96,(uint64_t)(uintptr_t)image->code);u64(c,104,IZ_PS2_CODE_BYTES);
 u64(c,112,in->stack+PAGE);u64(c,120,IZ_PS2_STACK_BYTES);u64(c,128,in->control);u64(c,168,in->log);
 u64(c,0xc8,in->gpu_instance);u64(c,0xd0,in->gpu_physical);u64(c,0xd8,in->gpu_device);u64(c,0x188,in->dispatch);u64(c,0x250,e->sdk+0x1c7f30);
 const uint64_t args[]={in->vm,in->domains,in->av,in->filesystem,in->main_descriptor,in->pad,in->save,in->hwgs};
 for(unsigned i=0;i<8;i++)u64(c,0x208+i*8,args[i]);
 uint8_t*v=out->vm;u32(v,0,0x324d5650);u32(v,4,1);u32(v,8,1312);u32(v,12,2);u64(v,16,IZ_PS2_MAIN_BYTES);u64(v,24,IZ_PS2_JIT_BYTES);
 for(unsigned i=0;i<5;i++)copy(v+32+i*256,in->paths[i],lengths[i]);
 /* Existing cached coherent shadow descriptor v3, flags14/attributes0x60.
  * The caller still has to prove the actual Vulkan memory selection/type. */
 uint8_t*j=out->domains;u32(j,0,0x32434a48);u32(j,4,3);u32(j,8,96);u32(j,12,14);
 const uint64_t values[]={in->jit_source,in->jit_shadow,in->jit_rx,IZ_PS2_JIT_BYTES,e->application_pid,e->ro_pid,e->sdk+0x15845c};
 for(unsigned i=0;i<7;i++)u64(j,16+i*8,values[i]);u32(j,72,e->ro_handle);u32(j,76,0x60);u64(j,80,IZ_PS2_GS_OFFSET);u64(j,88,IZ_PS2_JIT_BYTES-IZ_PS2_GS_OFFSET);
 uint8_t*m=out->main;u32(m,0,0x32504d50);u32(m,4,1);u32(m,8,136);u32(m,12,1);
 const uint64_t main_values[]={in->main,IZ_PS2_MAIN_BYTES,(uint64_t)(uintptr_t)image->code,IZ_PS2_CODE_BYTES,(uint64_t)(uintptr_t)image->data,IZ_PS2_DATA_BYTES,in->context,PAGE,e->sdk,IZ_PS2_SDK_BYTES,receipt,112};
 for(unsigned i=0;i<12;i++)u64(m,16+i*8,main_values[i]);copy(m+112,in->main_nonce,16);u64(m,128,in->main_descriptor);
 uint8_t*r=out->main_receipt;u32(r,0,0x32524d50);u32(r,4,1);u32(r,8,112);u32(r,12,1);u64(r,16,in->main_descriptor);u64(r,24,in->main);u64(r,32,IZ_PS2_MAIN_BYTES);copy(r+40,in->main_nonce,16);u64(r,56,in->context);u64(r,64,(uint64_t)(uintptr_t)image->code);u64(r,72,(uint64_t)(uintptr_t)image->data);u64(r,80,e->sdk);
 uint8_t*p=out->pad;u32(p,0,0x32445048);u32(p,4,1);u32(p,8,48);u32(p,12,1);u64(p,16,e->sdk);u64(p,24,IZ_PS2_SDK_BYTES);copy(p+32,in->pad_nonce,16);
 u32(out->control,0,0x4c544348);u64(out->log,0,in->ring);u32(out->log,8,32768);return 0;
}
