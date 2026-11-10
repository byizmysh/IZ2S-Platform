#include "sd-runtime/bridge.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
uint32_t iz_app(IzNativeApp*,uint32_t);
static unsigned begins,steps,releases,inits,frames,stops,stack_releases,exit_requested;
static unsigned close_steps,load_steps,closing,fail_load,fail_stop,fail_init,fail_frame;
static uint32_t engine(IzNativeApp*a,uint32_t event){(void)a;if(event==IZ_NATIVE_INIT){inits++;if(fail_init)return 0xab01;}else if(event==IZ_NATIVE_FRAME){frames++;if(fail_frame)return 0xab02;}else{assert(event==IZ_NATIVE_STOP);stops++;if(fail_stop)return 0xeeee;}return 0;}
static uint32_t release_stack(IzNativeApp*a){(void)a;stack_releases++;return 0;}
static uint32_t begin(IzNativeApp*a,const char*name,uint64_t*ticket){(void)a;assert(!strncmp(name,"asset-",6));begins++;*ticket=begins;closing=0;load_steps=0;return 0;}
static uint32_t step(IzNativeApp*a,uint64_t ticket,uint32_t budget,IzNativeImageProgress*p,IzNativeImage*image){
 (void)a;assert(ticket&&budget==16);steps++;memset(p,0,sizeof *p);p->total_bytes=100;
 if(closing){p->state=++close_steps>=2?IZ_NATIVE_IMAGE_CLOSED:IZ_NATIVE_IMAGE_CLOSING;p->done=close_steps>=2;return 0;}
 if(fail_load){p->state=IZ_NATIVE_IMAGE_FAILED;p->done=1;return 0xdddd;}
 if(++load_steps>=2){p->state=IZ_NATIVE_IMAGE_READY;p->done=1;*image=(IzNativeImage){.version=1,.bytes=64,.entry=(void*)engine,.spawn=(void*)release_stack,.token=ticket};}else p->state=IZ_NATIVE_IMAGE_LOADING;return 0;
}
static uint32_t release(IzNativeApp*a,uint64_t ticket){(void)a;assert(ticket);releases++;closing=1;close_steps=0;return 0;}
static void request_exit(IzNativeApp*a){(void)a;exit_requested++;}
int main(void){
 IzNativeApp5 public={0};IzNativeApp4*a=&public.base;IzNativeApp*b=&a->base.base.base;a->image_begin=begin;a->image_step=step;a->image_release=release;
 IzNativeEnvironment environment={.version=1,.bytes=sizeof environment,.sdk=0x100000,.sdk_bytes=6164480,.application_pid=1,.ro_pid=2,.ro_handle=3,.tick_hz=1000000};public.environment=&environment;b->tick_hz=environment.tick_hz;b->controls=calloc(1,1024);b->controls_bytes=1024;b->owner=(void*)1;
 b->magic=0x414e5a49;b->version=5;b->bytes=sizeof public;b->data=calloc(1,IZPSX_APP_DATA_BYTES);b->data_bytes=IZPSX_APP_DATA_BYTES;b->request_exit=request_exit;
 a->base.base.private_memory=calloc(1,IZPSX_PRIVATE_BYTES);a->base.base.private_bytes=IZPSX_PRIVATE_BYTES;assert(b->data&&a->base.base.private_memory);IzpsxBridge*s=b->data;
 assert(!iz_app(b,IZ_NATIVE_INIT)&&begins==1&&!inits);assert(!iz_app(b,IZ_NATIVE_FRAME)&&!inits);assert(!iz_app(b,IZ_NATIVE_FRAME)&&inits==1);assert(!iz_app(b,IZ_NATIVE_FRAME)&&frames==1);
 s->reload=1;assert(!iz_app(b,IZ_NATIVE_FRAME)&&stops==1&&stack_releases==1&&releases==1);assert(!iz_app(b,IZ_NATIVE_FRAME)&&begins==2&&!s->initialized);assert(!iz_app(b,IZ_NATIVE_FRAME));assert(!iz_app(b,IZ_NATIVE_FRAME)&&inits==2);
 fail_stop=1;assert(iz_app(b,IZ_NATIVE_STOP)==0xeeee&&s->initialized&&releases==1&&stack_releases==1);fail_stop=0;
 assert(iz_app(b,IZ_NATIVE_STOP)==IZ_NATIVE_STOP_PENDING&&releases==2&&stack_releases==2);assert(!iz_app(b,IZ_NATIVE_STOP)&&!s->ticket&&!s->initialized);
 /* Cancel while loading, and a known failed load already fully cleaned by
  * the loader: neither calls an uninitialized engine or releases a stale token. */
 assert(!iz_app(b,IZ_NATIVE_INIT));assert(iz_app(b,IZ_NATIVE_STOP)==IZ_NATIVE_STOP_PENDING);assert(!iz_app(b,IZ_NATIVE_STOP)&&stops==3);
 fail_load=1;assert(!iz_app(b,IZ_NATIVE_INIT));unsigned old=releases;assert(iz_app(b,IZ_NATIVE_FRAME)==0xdddd&&exit_requested==1&&!s->ticket);assert(!iz_app(b,IZ_NATIVE_STOP)&&releases==old);
 fail_load=0;fail_init=1;assert(!iz_app(b,IZ_NATIVE_INIT));assert(!iz_app(b,IZ_NATIVE_FRAME));assert(iz_app(b,IZ_NATIVE_FRAME)==0xab01&&s->error==0xab01);assert(iz_app(b,IZ_NATIVE_STOP)==IZ_NATIVE_STOP_PENDING);assert(!iz_app(b,IZ_NATIVE_STOP));
 fail_init=0;assert(!iz_app(b,IZ_NATIVE_INIT));assert(!iz_app(b,IZ_NATIVE_FRAME));assert(!iz_app(b,IZ_NATIVE_FRAME));fail_frame=1;assert(iz_app(b,IZ_NATIVE_FRAME)==0xab02&&s->error==0xab02);assert(iz_app(b,IZ_NATIVE_STOP)==IZ_NATIVE_STOP_PENDING);assert(!iz_app(b,IZ_NATIVE_STOP));
 b->version=3;assert(iz_app(b,IZ_NATIVE_INIT)!=0);free(b->controls);free(b->data);free(a->base.base.private_memory);
 printf("{\"passed\":true,\"begins\":%u,\"steps\":%u,\"releases\":%u,\"stops\":%u,\"stackReleases\":%u,\"consoleExecuted\":false}\n",begins,steps,releases,stops,stack_releases);return 0;
}
