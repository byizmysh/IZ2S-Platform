/* PIC public native-app ABI 5 owner. Only serialized callbacks load/close the engine.
 * After return, the SD supervisor handles input and displays our framebuffer. */
#include "bridge.h"
#include "services.h"
#include "native-menu.h"
#include "sd-asset.h"
typedef uint32_t(*Engine)(IzNativeApp*,uint32_t);
typedef uint32_t(*Release)(IzNativeApp*);
static void loading(IzNativeApp*app,IzpsxBridge*b){
 IzNativeApp2*x=(void*)app;uint32_t*h=x->private_memory;
 uint64_t total=b->progress.total_bytes+b->progress.total_pages*4096+9*1024*1024;
 uint64_t work=b->progress.read_bytes+b->progress.cleared_bytes+b->progress.mapped_pages*4096;
 unsigned progress=total?(unsigned)(work*1000/total):0;
 ps1_loading_draw(h+1024,progress,b->phase==3?"CLOSING YOUR GAME":"LOADING IZPSX");h[0]=640;h[1]=480;h[2]++;h[3]=h[4]=h[5]=h[7]=0;h[6]=100;__atomic_thread_fence(__ATOMIC_RELEASE);
}
static uint32_t start(IzNativeApp*app,IzpsxBridge*b){
 uint32_t rc=((IzNativeApp4*)app)->image_begin(app,IZPSX_ENGINE_ASSET,&b->ticket);
 if(!rc)b->phase=1;return rc;
}
static uint32_t stop(IzNativeApp*app,IzpsxBridge*b){
 IzNativeApp4*x=(void*)app;uint32_t rc;
 if(b->initialized){
  rc=((Engine)b->engine.entry)(app,IZ_NATIVE_STOP);if(rc)return rc;
  rc=((Release)b->engine.spawn)(app);if(rc)return rc;b->initialized=0;
 }
 if(!b->ticket)return 0;
 if(b->phase!=3){rc=x->image_release(app,b->ticket);if(rc)return rc;b->phase=3;}
 rc=x->image_step(app,b->ticket,16,&b->progress,&b->engine);if(rc&&!(b->progress.done&&b->progress.state==IZ_NATIVE_IMAGE_FAILED))return rc;
 if(!b->progress.done)return IZ_NATIVE_STOP_PENDING;
 if(b->progress.state!=IZ_NATIVE_IMAGE_CLOSED&&b->progress.state!=IZ_NATIVE_IMAGE_FAILED)return 0xf211;
 b->ticket=0;b->phase=0;return 0;
}
__attribute__((section(".text.iz_app"))) uint32_t iz_app(IzNativeApp*app,uint32_t event){
 if(!ps1_sd_public_app(app)||!ps1_sd_environment(app))return 0xf210;
 IzNativeApp4*x=(void*)app;IzpsxBridge*b=app->data;
 if(!x->base.base.private_memory||x->base.base.private_bytes!=IZPSX_PRIVATE_BYTES||!x->image_begin||!x->image_step||!x->image_release)return 0xf210;
 if(event==IZ_NATIVE_STOP)return stop(app,b);
 if(event==IZ_NATIVE_INIT){app->flags|=IZ_NATIVE_MANAGED_INPUT;uint32_t rc=start(app,b);if(!rc)loading(app,b);return rc;}
 if(event!=IZ_NATIVE_FRAME)return 0xf210;
 if(b->reload){uint32_t rc=stop(app,b);loading(app,b);if(rc==IZ_NATIVE_STOP_PENDING)return 0;if(rc)return rc;b->reload=0;b->error=0;return start(app,b);}
 if(b->phase==1){
  uint32_t rc=x->image_step(app,b->ticket,16,&b->progress,&b->engine);loading(app,b);
  if(rc){b->error=rc;if(b->progress.done&&b->progress.state==IZ_NATIVE_IMAGE_FAILED){b->ticket=0;b->phase=0;}app->request_exit(app);return rc;}
  if(b->progress.state==IZ_NATIVE_IMAGE_READY){
   if(!b->engine.entry||!b->engine.spawn)return 0xf212;
   b->initialized=1;rc=((Engine)b->engine.entry)(app,IZ_NATIVE_INIT);
   if(rc){b->error=rc;app->request_exit(app);return rc;}else b->phase=2;
  }return 0;
 }
 if(b->phase==2){uint32_t rc=((Engine)b->engine.entry)(app,IZ_NATIVE_FRAME);if(rc){b->error=rc;app->request_exit(app);return rc;}return 0;}
 return 0;
}
