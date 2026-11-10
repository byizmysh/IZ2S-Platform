#include "application.h"
#include "sdk-memory.h"
#include "sdk-storage.h"
#include "sdk-jit-shadow.h"
#include "sdk-video.h"
#include "../relocatable/address.h"
#include "gpu-profile.h"
extern void*memcpy(void*,const void*,size_t);
extern void*memset(void*,int,size_t);
extern int memcmp(const void*,const void*,size_t);
#define P(p) ((uint64_t)(uintptr_t)(p))
#define LIMIT (UINT64_C(1)<<39)
#define MAGIC UINT64_C(0x3150504132535a49)
#define PAGE UINT64_C(4096)
enum {BIND_MEMORY=1,PARENT,BIND_FS,SCAN_BEGIN,SCAN,WAIT_SELECTION,OPEN_FILES,
 MEMORY_BEGIN,MAIN_STEP,IMAGE_BEGIN,IMAGE_STEP,MEMORY_EXTEND,MEMORY_STEP,
 BIND_SHADOW,SHADOW_BEGIN,SHADOW_STEP,BIND_VIDEO,VIDEO_BEGIN,VIDEO_STEP,
 WINDOW_BEGIN,WINDOW_STEP,LAUNCH_BEGIN,LAUNCH_STEP,
 CLEAN_VIDEO_BEGIN,CLEAN_VIDEO_STEP,CLEAN_SHADOW_BEGIN,CLEAN_SHADOW_STEP,
 CLEAN_FILES_BEGIN,CLEAN_FILES_STEP,CLEAN_IMAGE_BEGIN,CLEAN_IMAGE_STEP,
 CLEAN_MEMORY_BEGIN,CLEAN_MEMORY_STEP,FINISHED};
struct IzPs2Application {
 uint64_t magic,self,total,scratch,launch_at,units,image_ticket,walk,end,last_buttons;
 uint32_t busy,state,phase,error,ui_error,cursor,stop_requested,memory_bound,regions,launch_started;
 IzNativeApp6*app;IzNativeApp6 callbacks;IzNativeEnvironment environment;IzNativeGraphics graphics;
 IzPs2ApplicationConfig config;IzNativeImage image;IzNativeImageProgress image_progress;
 IzPs2Memory memory;IzPs2Shadow shadow;IzPs2Video video;IzPs2Storage storage;
 IzPs2SdkMemory sdk_memory;IzPs2StorageIo io;IzPs2SdkShadow sdk_shadow;IzPs2SdkVideo sdk_video;
 IzAddressCursor window;IzPs2LaunchProgress launch_progress;
};
static uint64_t round_page(uint64_t n){return (n+4095)&~UINT64_C(4095);}
size_t iz_ps2_application_bytes(void){return (size_t)(round_page(sizeof(IzPs2Application))+65536+iz_ps2_launch_bytes());}
static IzNativeApp*base(IzNativeApp6*a){return &a->base.base.base.base.base;}
static int range(uint64_t p,uint64_t n){return p&&!(p&4095)&&p<LIMIT&&n&&!(n&4095)&&n<=LIMIT-p;}
static int overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m){return a<b+m&&b<a+n;}
static int inside(uint64_t p,uint64_t n,uint64_t b,uint64_t m){return p>=b&&p-b<=m&&n<=m-(p-b);}
static int zero(const void*p,size_t n){const uint8_t*b=p;for(size_t i=0;i<n;i++)if(b[i])return 0;return 1;}
static uint32_t lock(IzPs2Application*a){uint32_t n=0;return __atomic_compare_exchange_n(&a->busy,&n,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)?0:0xf619;}
static void unlock(IzPs2Application*a){__atomic_store_n(&a->busy,0,__ATOMIC_RELEASE);}
static uint32_t retain(IzPs2Application*a,uint32_t error){a->state=IZ_PS2_APP_RETAINED;a->error=error?error:0xf601;return a->error;}
static void close_begin(IzPs2Application*a,uint32_t error){if(!a->error)a->error=error;a->state=IZ_PS2_APP_CLOSING;a->phase=CLEAN_VIDEO_BEGIN;}
static int valid_path(const char*p){
 /* Installed companions are content addressed by the ordinary asset writer. */
 const char*prefix="asset-",*suffix=".izdata";if(memcmp(p,prefix,6)||memcmp(p+70,suffix,7)||p[77]||p[78]||p[79])return 0;
 for(unsigned i=6;i<70;i++)if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')))return 0;return 1;
}
static int callbacks(const IzPs2Application*a){const IzNativeApp6*x=a->app,*y=&a->callbacks;const IzNativeApp*p=&x->base.base.base.base.base,*q=&y->base.base.base.base.base;
 return p->magic==0x414e5a49&&p->version==6&&p->bytes==sizeof *x&&p->owner==q->owner&&p->data==q->data&&p->data_bytes==q->data_bytes&&p->controls==q->controls&&p->controls_bytes==q->controls_bytes&&p->buttons==q->buttons&&p->request_exit==q->request_exit&&
 x->base.environment==y->base.environment&&x->graphics==y->graphics&&x->base.base.image_begin==y->base.base.image_begin&&x->base.base.image_step==y->base.base.image_step&&x->base.base.image_release==y->base.base.image_release&&x->base.worker_begin==y->base.worker_begin&&x->base.worker_poll==y->base.worker_poll&&x->base.worker_join==y->base.worker_join&&x->presentation_acquire==y->presentation_acquire&&x->presentation_release==y->presentation_release&&x->base.base.base.base.ticks==y->base.base.base.base.ticks&&
 !memcmp(x->base.environment,&a->environment,sizeof a->environment)&&!memcmp(x->graphics,&a->graphics,sizeof a->graphics);
}
uint32_t iz_ps2_application_begin(void*storage,size_t bytes,IzNativeApp6*app,const IzPs2ApplicationConfig*c){
 uint64_t at=P(storage),needed=iz_ps2_application_bytes();if(!app||!c||!storage||bytes<needed||!range(at,needed))return 0xf602;IzNativeApp*p=base(app);
 if(p->magic!=0x414e5a49||p->version!=6||p->bytes!=sizeof *app||!p->data||!range(P(p->data),p->data_bytes)||!inside(at,needed,P(p->data),p->data_bytes)||!inside(P(c),sizeof *c,P(p->data),p->data_bytes)||overlap(at,needed,P(c),sizeof *c)||!range(P(p->controls),p->controls_bytes)||p->controls_bytes<5120||overlap(P(p->data),p->data_bytes,P(p->controls),p->controls_bytes)||!p->buttons||!p->request_exit||!app->base.base.base.base.ticks||!app->base.environment||!app->graphics||!app->base.base.image_begin||!app->base.base.image_step||!app->base.base.image_release||!app->base.worker_begin||!app->base.worker_poll||!app->base.worker_join||!app->presentation_acquire||!app->presentation_release)return 0xf602;
 if(c->magic!=IZ_PS2_APPLICATION_CONFIG||c->version!=1||c->bytes!=sizeof *c||c->state_offset<4096||c->state_offset!=at-P(p->data)||c->vertex_offset<4096||c->fragment_offset<4096||!c->root_count||c->root_count>3||!c->budget||c->budget>8||!zero(c->reserved,sizeof c->reserved)||!valid_path(c->image))return 0xf602;
 const uint64_t shaders[]={P(p->data)+c->vertex_offset,P(p->data)+c->fragment_offset},sizes[]={c->vertex_bytes,c->fragment_bytes};
 for(unsigned i=0;i<2;i++)if(!sizes[i]||sizes[i]<20||sizes[i]>65536||(shaders[i]&3)||(sizes[i]&3)||!inside(shaders[i],sizes[i],P(p->data),p->data_bytes)||overlap(at,needed,shaders[i],sizes[i])||overlap(P(c),sizeof *c,shaders[i],sizes[i]))return 0xf602;
 if(overlap(shaders[0],sizes[0],shaders[1],sizes[1])||overlap(at,needed,P(app),sizeof *app)||overlap(at,needed,P(app->base.environment),sizeof *app->base.environment)||overlap(at,needed,P(app->graphics),sizeof *app->graphics)||overlap(at,needed,P(p->controls),p->controls_bytes))return 0xf602;
 if(!zero(storage,sizeof(IzPs2Application)))return 0xf619;
 IzPs2Application*a=storage;a->self=at;a->total=needed;a->app=app;a->callbacks=*app;a->environment=*app->base.environment;a->graphics=*app->graphics;a->config=*c;a->scratch=at+round_page(sizeof *a);a->launch_at=a->scratch+65536;a->state=IZ_PS2_APP_PREPARING;a->phase=BIND_MEMORY;a->magic=MAGIC;return 0;
}
static uint32_t identity(IzPs2Application*a){uint64_t pid=0;if(!a->memory_bound||a->sdk_memory.ops.identity(a->sdk_memory.ops.owner,&pid)||pid!=a->environment.application_pid)return retain(a,0xf603);return 0;}
static uint32_t image_step(IzPs2Application*a,int closing){
 IzNativeImageProgress p={0};IzNativeImage image={0};uint32_t rc=a->app->base.base.image_step(base(a->app),a->image_ticket,1,&p,&image);a->image_progress=p;
 if(p.version!=1||p.bytes!=sizeof p||p.done>1||p.state==IZ_NATIVE_IMAGE_RETAINED)return retain(a,rc?rc:0xf604);
 if(p.done){
  if(p.state==IZ_NATIVE_IMAGE_CLOSED||p.state==IZ_NATIVE_IMAGE_FAILED){if(!zero(&image,sizeof image)||(p.state==IZ_NATIVE_IMAGE_CLOSED&&(rc||p.error))||(p.state==IZ_NATIVE_IMAGE_FAILED&&(!p.error||rc!=p.error)))return retain(a,0xf604);a->image_ticket=0;memset(&a->image,0,sizeof a->image);if(p.error&&!a->error)a->error=p.error;return 0;}
  if(closing||p.state!=IZ_NATIVE_IMAGE_READY||rc||p.error||image.version!=1||image.bytes!=sizeof image||image.token!=a->image_ticket)return retain(a,rc?rc:0xf604);a->image=image;return 0;
 }
 if(rc||(p.state!=IZ_NATIVE_IMAGE_LOADING&&p.state!=IZ_NATIVE_IMAGE_CLOSING)||(closing&&p.state!=IZ_NATIVE_IMAGE_CLOSING))return retain(a,rc?rc:0xf604);return 0;
}
static uint32_t video_begin(IzPs2Application*a){
 IzPs2VideoPlan p={0};IzNativeApp*b=base(a->app);p.vertex=(uint8_t*)b->data+a->config.vertex_offset;p.vertex_bytes=a->config.vertex_bytes;p.fragment=(uint8_t*)b->data+a->config.fragment_offset;p.fragment_bytes=a->config.fragment_bytes;
 for(unsigned i=0;i<4;i++)p.protected[p.protected_count++]=(IzPs2AvSpan){a->memory.protected[i].base,a->memory.protected[i].bytes};
 for(unsigned i=0;i<5;i++)p.protected[p.protected_count++]=(IzPs2AvSpan){a->memory.blocks[i].base,a->memory.blocks[i].bytes};
 p.protected[p.protected_count++]=(IzPs2AvSpan){a->shadow.mapped,IZ_PS2_JIT_BYTES};p.protected[p.protected_count++]=(IzPs2AvSpan){a->graphics.vulkan,IZ_PS2_VULKAN_BYTES};
 p.protected[p.protected_count++]=(IzPs2AvSpan){P(b->controls),b->controls_bytes};
 return iz_ps2_video_begin(&a->video,&a->sdk_video.ops,&a->environment,&a->graphics,&p);
}
static uint64_t mixed(uint64_t n){n^=n>>30;n*=UINT64_C(0xbf58476d1ce4e5b9);n^=n>>27;n*=UINT64_C(0x94d049bb133111eb);return (n^(n>>31))|1;}
static uint32_t launch_begin(IzPs2Application*a){
 uint64_t tick=a->app->base.base.base.base.ticks(base(a->app));IzPs2LaunchPlan p={0};p.app=a->app;p.memory=&a->memory;p.shadow=&a->shadow;p.video=&a->video;p.storage=&a->storage;p.image=a->image;p.jit_rx=a->window.plan.code_base;
 uint8_t*tokens[]={p.main_nonce,p.pad_nonce,p.av_token,p.read_token,p.save_token};for(unsigned i=0;i<5;i++){uint64_t words[]={mixed(tick^a->environment.application_pid^a->self^(i+1)),mixed(tick^a->environment.ro_pid^a->units^((uint64_t)(i+1)<<32))};memcpy(tokens[i],words,16);}
 uint32_t rc=iz_ps2_launch_begin((void*)(uintptr_t)a->launch_at,iz_ps2_launch_bytes(),&p);if(!rc)a->launch_started=1;return rc;
}
static uint32_t advance(IzPs2Application*a){
 if(!callbacks(a))return retain(a,0xf605);uint32_t rc=0,done=0;
 /* Component steps perform their own current identity checks. The FS and
  * address planner steps borrow this controller's guard. Qualification binds
  * are fixed finite read-only batches, not one-provider-call units. */
 if(a->memory_bound&&a->phase!=MAIN_STEP&&a->phase!=MEMORY_STEP&&a->phase!=SHADOW_STEP&&a->phase!=VIDEO_STEP&&a->phase!=LAUNCH_STEP&&a->phase!=CLEAN_VIDEO_STEP&&a->phase!=CLEAN_SHADOW_STEP&&a->phase!=CLEAN_MEMORY_STEP){rc=identity(a);if(rc)return rc;}
 if(a->stop_requested&&a->phase<CLEAN_VIDEO_BEGIN&&a->phase!=LAUNCH_STEP){close_begin(a,0);return 0;}
 switch(a->phase){
 case BIND_MEMORY:rc=iz_ps2_sdk_memory_bind(&a->sdk_memory,&a->environment);if(rc){close_begin(a,rc);break;}a->memory_bound=1;a->walk=P(base(a->app)->data);a->end=a->walk+base(a->app)->data_bytes;a->phase=PARENT;break;
 case PARENT:{IzPs2MemoryRegion r={0};rc=a->sdk_memory.ops.query(a->sdk_memory.ops.owner,a->walk,&r);if(rc||++a->regions>4096||!range(r.base,r.bytes)||r.base>a->walk||r.base+r.bytes<=a->walk||r.type!=5||r.permission!=3||r.attributes||r.ipc_refs||r.device_refs)return retain(a,rc?rc:0xf606);uint64_t end=r.base+r.bytes;a->walk=end<a->end?end:a->end;if(a->walk==a->end)a->phase=BIND_FS;break;}
 case BIND_FS:rc=iz_ps2_sdk_storage_bind(&a->io,&a->environment,(void*)(uintptr_t)a->scratch,65536);if(rc)close_begin(a,rc);else a->phase=SCAN_BEGIN;break;
 case SCAN_BEGIN:rc=iz_ps2_storage_begin(&a->storage,&a->io.filesystem,a->config.roots,a->config.root_count);if(rc)close_begin(a,rc);else a->phase=SCAN;break;
 case SCAN:rc=iz_ps2_storage_step(&a->storage,1,&done);if(a->storage.state==IZ_PS2_STORAGE_RETAINED)return retain(a,rc);if(done){if(a->storage.state==IZ_PS2_STORAGE_CATALOG&&!rc){a->state=IZ_PS2_APP_CATALOG;a->phase=WAIT_SELECTION;}else close_begin(a,rc?rc:0xf607);}break;
 case WAIT_SELECTION:return 0;
 case OPEN_FILES:rc=iz_ps2_storage_step(&a->storage,1,&done);if(a->storage.state==IZ_PS2_STORAGE_RETAINED)return retain(a,rc);if(done){if(a->storage.state==IZ_PS2_STORAGE_READY&&!rc)a->phase=MEMORY_BEGIN;else close_begin(a,rc?rc:0xf607);}break;
 case MEMORY_BEGIN:rc=iz_ps2_memory_begin(&a->memory,&a->sdk_memory.ops,&a->environment,P(base(a->app)->data),base(a->app)->data_bytes,1);if(rc)close_begin(a,rc);else a->phase=MAIN_STEP;break;
 case MAIN_STEP:case MEMORY_STEP:rc=iz_ps2_memory_step(&a->memory,1,&done);if(a->memory.state==IZ_PS2_MEMORY_RETAINED)return retain(a,rc);if(done){if(a->memory.state==IZ_PS2_MEMORY_READY&&!rc)a->phase=a->phase==MAIN_STEP?IMAGE_BEGIN:BIND_SHADOW;else close_begin(a,rc?rc:0xf608);}break;
 case IMAGE_BEGIN:a->image_ticket=0;rc=a->app->base.base.image_begin(base(a->app),a->config.image,&a->image_ticket);if(rc||!a->image_ticket)return retain(a,rc?rc:0xf604);a->phase=IMAGE_STEP;break;
 case IMAGE_STEP:rc=image_step(a,0);if(rc)return rc;if(a->image_progress.done){if(a->image_ticket&&a->image_progress.state==IZ_NATIVE_IMAGE_READY)a->phase=MEMORY_EXTEND;else close_begin(a,a->error?a->error:0xf604);}break;
 case MEMORY_EXTEND:rc=iz_ps2_memory_after_image(&a->memory,&a->image);if(rc)close_begin(a,rc);else a->phase=MEMORY_STEP;break;
 case BIND_SHADOW:rc=iz_ps2_sdk_shadow_bind(&a->sdk_shadow,&a->environment,&a->graphics);if(rc)close_begin(a,rc);else a->phase=SHADOW_BEGIN;break;
 case SHADOW_BEGIN:rc=iz_ps2_shadow_begin(&a->shadow,&a->sdk_shadow.ops,&a->environment,&a->graphics,&a->memory);if(rc)close_begin(a,rc);else a->phase=SHADOW_STEP;break;
 case SHADOW_STEP:rc=iz_ps2_shadow_step(&a->shadow,1,&done);if(a->shadow.state==IZ_PS2_SHADOW_RETAINED)return retain(a,rc);if(done){if(a->shadow.state==IZ_PS2_SHADOW_READY&&!rc){if(overlap(a->shadow.mapped,IZ_PS2_JIT_BYTES,P(base(a->app)->controls),base(a->app)->controls_bytes))return retain(a,0xf609);a->phase=BIND_VIDEO;}else close_begin(a,rc?rc:0xf609);}break;
 case BIND_VIDEO:rc=iz_ps2_sdk_video_bind(&a->sdk_video,&a->environment,&a->graphics);if(rc)close_begin(a,rc);else a->phase=VIDEO_BEGIN;break;
 case VIDEO_BEGIN:rc=video_begin(a);if(rc)close_begin(a,rc);else a->phase=VIDEO_STEP;break;
 case VIDEO_STEP:rc=iz_ps2_video_step(&a->video,1,&done);if(a->video.state==IZ_PS2_VIDEO_RETAINED)return retain(a,rc);if(done){if(a->video.state==IZ_PS2_VIDEO_READY&&!rc)a->phase=WINDOW_BEGIN;else close_begin(a,rc?rc:0xf60a);}break;
 case WINDOW_BEGIN:rc=iz_address_begin(&a->io.context,&a->window,IZ_PS2_JIT_BYTES,0);if(rc)return retain(a,rc);a->phase=WINDOW_STEP;break;
 case WINDOW_STEP:rc=iz_address_step(&a->io.context,&a->window,&done);if(rc==IZ_ADDRESS_FULL){close_begin(a,rc);break;}if(rc)return retain(a,rc);if(done)a->phase=LAUNCH_BEGIN;break;
 case LAUNCH_BEGIN:rc=launch_begin(a);if(rc==0xf519||rc==0xf619)return retain(a,rc);if(rc)close_begin(a,rc);else{a->phase=LAUNCH_STEP;a->state=IZ_PS2_APP_RUNNING;}break;
 case LAUNCH_STEP:rc=iz_ps2_launch_step((void*)(uintptr_t)a->launch_at,1,&a->launch_progress);if(rc||a->launch_progress.state==IZ_PS2_LAUNCH_RETAINED)return retain(a,rc);if(a->launch_progress.state==IZ_PS2_LAUNCH_CLOSED){a->image_ticket=0;a->state=IZ_PS2_APP_CLOSED;a->phase=FINISHED;}break;
 case CLEAN_VIDEO_BEGIN:rc=iz_ps2_video_cancel(&a->video,0);if(rc)return retain(a,rc);a->phase=(a->video.state==IZ_PS2_VIDEO_IDLE||a->video.state==IZ_PS2_VIDEO_CLOSED||a->video.state==IZ_PS2_VIDEO_FAILED)?CLEAN_SHADOW_BEGIN:CLEAN_VIDEO_STEP;break;
 case CLEAN_VIDEO_STEP:rc=iz_ps2_video_step(&a->video,1,&done);if(a->video.state==IZ_PS2_VIDEO_RETAINED)return retain(a,rc);if(done){if(a->video.state!=IZ_PS2_VIDEO_CLOSED&&a->video.state!=IZ_PS2_VIDEO_FAILED)return retain(a,0xf60a);a->phase=CLEAN_SHADOW_BEGIN;}break;
 case CLEAN_SHADOW_BEGIN:rc=iz_ps2_shadow_cancel(&a->shadow,0);if(rc)return retain(a,rc);a->phase=(a->shadow.state==IZ_PS2_SHADOW_IDLE||a->shadow.state==IZ_PS2_SHADOW_CLOSED||a->shadow.state==IZ_PS2_SHADOW_FAILED)?CLEAN_FILES_BEGIN:CLEAN_SHADOW_STEP;break;
 case CLEAN_SHADOW_STEP:rc=iz_ps2_shadow_step(&a->shadow,1,&done);if(a->shadow.state==IZ_PS2_SHADOW_RETAINED)return retain(a,rc);if(done){if(a->shadow.state!=IZ_PS2_SHADOW_CLOSED&&a->shadow.state!=IZ_PS2_SHADOW_FAILED)return retain(a,0xf609);a->phase=CLEAN_FILES_BEGIN;}break;
 case CLEAN_FILES_BEGIN:rc=iz_ps2_storage_cancel(&a->storage,0);if(rc)return retain(a,rc);a->phase=(a->storage.state==IZ_PS2_STORAGE_IDLE||a->storage.state==IZ_PS2_STORAGE_CLOSED||a->storage.state==IZ_PS2_STORAGE_FAILED)?CLEAN_IMAGE_BEGIN:CLEAN_FILES_STEP;break;
 case CLEAN_FILES_STEP:rc=iz_ps2_storage_step(&a->storage,1,&done);if(a->storage.state==IZ_PS2_STORAGE_RETAINED)return retain(a,rc);if(done){if(a->storage.state!=IZ_PS2_STORAGE_CLOSED&&a->storage.state!=IZ_PS2_STORAGE_FAILED)return retain(a,0xf607);a->phase=CLEAN_IMAGE_BEGIN;}break;
 case CLEAN_IMAGE_BEGIN:if(!a->image_ticket){a->phase=CLEAN_MEMORY_BEGIN;break;}rc=a->app->base.base.image_release(base(a->app),a->image_ticket);if(rc)return retain(a,rc);a->phase=CLEAN_IMAGE_STEP;break;
 case CLEAN_IMAGE_STEP:rc=image_step(a,1);if(rc)return rc;if(a->image_progress.done){if(a->image_ticket)return retain(a,0xf604);a->phase=CLEAN_MEMORY_BEGIN;}break;
 case CLEAN_MEMORY_BEGIN:rc=iz_ps2_memory_cancel(&a->memory,0);if(rc)return retain(a,rc);a->phase=(a->memory.state==IZ_PS2_MEMORY_IDLE||a->memory.state==IZ_PS2_MEMORY_CLOSED||a->memory.state==IZ_PS2_MEMORY_FAILED)?FINISHED:CLEAN_MEMORY_STEP;break;
 case CLEAN_MEMORY_STEP:rc=iz_ps2_memory_step(&a->memory,1,&done);if(a->memory.state==IZ_PS2_MEMORY_RETAINED)return retain(a,rc);if(done){if(a->memory.state!=IZ_PS2_MEMORY_CLOSED&&a->memory.state!=IZ_PS2_MEMORY_FAILED)return retain(a,0xf608);a->phase=FINISHED;}break;
 case FINISHED:a->state=IZ_PS2_APP_CLOSED;break;
 default:return retain(a,0xf60b);
 }return 0;
}
uint32_t iz_ps2_application_step(IzPs2Application*a,uint32_t budget,IzPs2ApplicationProgress*out){
 if(!a||!out||!budget||budget>64||a->magic!=MAGIC||a->self!=P(a)||P(out)>UINT64_MAX-sizeof *out||overlap(P(a),a->total,P(out),sizeof *out))return 0xf602;uint32_t rc=lock(a);if(rc)return rc;
 while(a->state!=IZ_PS2_APP_CLOSED&&a->state!=IZ_PS2_APP_RETAINED&&budget--){rc=advance(a);a->units++;if(rc||a->state==IZ_PS2_APP_CATALOG)break;}
 *out=(IzPs2ApplicationProgress){1,sizeof *out,a->state,a->phase,a->error,a->ui_error,a->storage.count,a->cursor,a->stop_requested,0,a->units,a->image_progress.read_bytes,a->image_progress.total_bytes,a->window.plan.code_base,a->launch_progress};rc=(a->state==IZ_PS2_APP_CLOSED||a->state==IZ_PS2_APP_RETAINED)?a->error:0;unlock(a);return rc;
}
static uint32_t select_game(IzPs2Application*a,uint32_t rank){
 if(a->state!=IZ_PS2_APP_CATALOG||a->stop_requested)return 0xf60c;uint32_t rc=iz_ps2_storage_select(&a->storage,rank);a->ui_error=rc;if(!rc){a->cursor=rank;a->state=IZ_PS2_APP_STARTING;a->phase=OPEN_FILES;}return rc;
}
uint32_t iz_ps2_application_select(IzPs2Application*a,uint32_t rank){if(!a||a->magic!=MAGIC||a->self!=P(a))return 0xf602;uint32_t rc=lock(a);if(rc)return rc;rc=select_game(a,rank);unlock(a);return rc;}
static uint32_t stop(IzPs2Application*a){if(a->state==IZ_PS2_APP_RETAINED)return a->error;if(a->state==IZ_PS2_APP_CLOSED)return 0;a->stop_requested=1;if(a->launch_started){uint32_t rc=iz_ps2_launch_stop((void*)(uintptr_t)a->launch_at);if(rc)return retain(a,rc);a->state=IZ_PS2_APP_CLOSING;}return 0;}
uint32_t iz_ps2_application_stop(IzPs2Application*a){if(!a||a->magic!=MAGIC||a->self!=P(a))return 0xf602;uint32_t rc=lock(a);if(rc)return rc;rc=stop(a);unlock(a);return rc;}
uint32_t iz_ps2_application_input(IzPs2Application*a,uint64_t buttons){
 if(!a||a->magic!=MAGIC||a->self!=P(a))return 0xf602;uint32_t rc=lock(a);if(rc)return rc;uint64_t pressed=buttons&~a->last_buttons;a->last_buttons=buttons;
 /* Plus is deliberately never inspected. Game-time buttons belong to the
  * unchanged engine; the supervisor handles its ordinary double-R3 exit. */
 if(a->state==IZ_PS2_APP_CATALOG){if(pressed&((UINT64_C(1)<<13)|(UINT64_C(1)<<17))){if(a->cursor)a->cursor--;a->ui_error=0;}else if(pressed&((UINT64_C(1)<<15)|(UINT64_C(1)<<19))){if(a->cursor+1<a->storage.count)a->cursor++;a->ui_error=0;}if(pressed&2)rc=stop(a);else if((pressed&1)&&a->storage.count)rc=select_game(a,a->cursor);}
 else if((a->state==IZ_PS2_APP_PREPARING||a->state==IZ_PS2_APP_STARTING)&&(pressed&2))rc=stop(a);unlock(a);return rc;
}
uint32_t iz_ps2_application_ui(IzPs2Application*a,IzPs2ApplicationUi*out){
 if(!a||!out||a->magic!=MAGIC||a->self!=P(a)||P(out)>UINT64_MAX-sizeof *out||overlap(P(a),a->total,P(out),sizeof *out))return 0xf602;uint32_t rc=lock(a);if(rc)return rc;memset(out,0,sizeof *out);out->magic=IZ_PS2_APPLICATION_UI;out->version=1;out->bytes=sizeof *out;out->state=a->state;out->error=a->error;out->ui_error=a->ui_error;out->count=a->storage.count;out->cursor=a->cursor;out->phase=a->phase;out->image_read_bytes=a->image_progress.read_bytes;out->image_total_bytes=a->image_progress.total_bytes;out->first=(a->cursor/12)*12;
 if(a->state==IZ_PS2_APP_CATALOG)for(unsigned j=out->first;j<a->storage.count&&out->rows<12;j++){const IzPs2Game*g=a->storage.games+a->storage.order[j];IzPs2UiGame*r=out->games+out->rows++;r->rank=j;r->root=g->root;r->bytes=g->bytes;memcpy(r->name,g->name,256);}unlock(a);return 0;
}
typedef struct {uint64_t magic,self,app,data,data_bytes;uint32_t state,state_offset,budget,error;} Entry;
_Static_assert(sizeof(IzPs2ApplicationConfig)+sizeof(Entry)<4096,"Native entry lifetime header");
uint32_t iz_ps2_application_event(IzNativeApp*app,uint32_t event){
 if(!app||app->magic!=0x414e5a49||app->version!=6||app->bytes!=sizeof(IzNativeApp6)||!app->data||!range(P(app->data),app->data_bytes)||app->data_bytes<4096||!app->controls||!range(P(app->controls),app->controls_bytes)||app->controls_bytes<5120)return 0xf602;
 const IzPs2ApplicationConfig*c=app->data;Entry*entry=(void*)((uint8_t*)app->data+sizeof *c);
 if(event==IZ_NATIVE_INIT){
  if(!zero(entry,sizeof *entry))return 0xf619;entry->magic=MAGIC;entry->self=P(entry);entry->app=P(app);entry->data=P(app->data);entry->data_bytes=app->data_bytes;entry->state=1;
  uint32_t rc=0;if(c->magic!=IZ_PS2_APPLICATION_CONFIG||c->version!=1||c->bytes!=sizeof *c||c->state_offset<4096||(c->state_offset&4095)||c->state_offset>app->data_bytes||iz_ps2_application_bytes()>app->data_bytes-c->state_offset)rc=0xf602;
  if(!rc){entry->state_offset=c->state_offset;entry->budget=c->budget;rc=iz_ps2_application_begin((uint8_t*)app->data+c->state_offset,app->data_bytes-c->state_offset,(IzNativeApp6*)app,c);}
  entry->state=rc?2:3;entry->error=rc;return rc;
 }
 if(entry->magic!=MAGIC||entry->self!=P(entry)||entry->app!=P(app)||entry->data!=P(app->data)||entry->data_bytes!=app->data_bytes)return 0xf602;
 /* A rejected pure INIT has created no child resource. Preserve this fact
  * independently of the malformed configuration, so ordinary STOP can close
  * the native module. A live controller never inherits that shortcut. */
 if(entry->state==2)return event==IZ_NATIVE_STOP?0:entry->error;
 if(entry->state!=3||entry->state_offset<4096||(entry->state_offset&4095)||entry->state_offset>app->data_bytes||iz_ps2_application_bytes()>app->data_bytes-entry->state_offset||!entry->budget||entry->budget>8)return 0xf602;
 IzPs2Application*a=(void*)((uint8_t*)app->data+entry->state_offset);
 if(event==IZ_NATIVE_STOP){uint32_t rc=iz_ps2_application_stop(a);if(rc)return rc;IzPs2ApplicationProgress p={0};rc=iz_ps2_application_step(a,entry->budget,&p);if(p.state==IZ_PS2_APP_CLOSED)return 0;return rc?rc:IZ_NATIVE_STOP_PENDING;}
 if(event!=IZ_NATIVE_FRAME)return 0xf602;
 uint32_t input=iz_ps2_application_input(a,app->buttons(app));if(input&&a->state==IZ_PS2_APP_RETAINED)return input;IzPs2ApplicationProgress p={0};uint32_t rc=iz_ps2_application_step(a,entry->budget,&p);IzPs2ApplicationUi ui;uint32_t display=iz_ps2_application_ui(a,&ui);if(display)return display;memcpy((uint8_t*)app->controls+1024,&ui,sizeof ui);
 if(p.state==IZ_PS2_APP_CLOSED){app->result=p.error;if(!p.error)app->request_exit(app);}return rc;
}
