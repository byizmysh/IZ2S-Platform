#include "launch.h"
#include "sdk-jit-shadow.h"
extern void*memcpy(void*,const void*,size_t);
extern void*memset(void*,int,size_t);
extern int memcmp(const void*,const void*,size_t);
#define P(p) ((uint64_t)(uintptr_t)(p))
#define PAGE UINT64_C(4096)
#define LIMIT (UINT64_C(1)<<39)
#define MAGIC UINT64_C(0x3148434e4c325a49)
#define CHUNK UINT64_C(65536)
enum {CLEAR=1,STARTUP,AV,FILES,DISPATCH,HWGS,LOW_GUARD,HIGH_GUARD,EXECUTE};
typedef struct {uint64_t base,bytes;} Span;
struct IzPs2Launch {
 uint64_t magic,self,total,execution,tail,tail_bytes;
 uint32_t busy,state,phase,error,stop_requested,published,index,writing,regions,reserved;
 uint64_t units,cleared_bytes,published_bytes,offset,walk,end;
 IzPs2ExecutionPlan plan;IzPs2Memory memory;IzPs2Shadow shadow;IzPs2Video video;
 IzNativeEnvironment environment;IzNativeGraphics graphics;
 void*app_owner;IzFs*filesystem;uint32_t game_handle,bios_handle;
 IzPs2Game game,bios;IzPs2StorageBindings bindings;
 IzPs2StartupPages startup;IzPs2StoragePages files;
 _Alignas(8) uint8_t av[4096],dispatch[4096],hwgs[4096];
 IzPs2ExecutionProgress progress;
};
static uint64_t round_page(uint64_t n){return (n+PAGE-1)&~(PAGE-1);}
size_t iz_ps2_launch_bytes(void){return (size_t)(round_page(sizeof(IzPs2Launch))+round_page(iz_ps2_execution_bytes())+13*PAGE+32768+2*CHUNK+IZ_PS2_AUDIO_BYTES+IZ_PS2_SCANOUT_BYTES);}
static int range(uint64_t p,uint64_t n){return p&&p<LIMIT&&!(p&(PAGE-1))&&n&&!(n&(PAGE-1))&&n<=LIMIT-p;}
static int overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m){return a<b+m&&b<a+n;}
static int inside(uint64_t p,uint64_t n,IzPs2MemorySpan s){return p>=s.base&&p-s.base<=s.bytes&&n<=s.bytes-(p-s.base);}
static int zero(const void*p,size_t n){const uint8_t*b=p;for(size_t i=0;i<n;i++)if(b[i])return 0;return 1;}
static uint32_t lock(IzPs2Launch*l){uint32_t n=0;return __atomic_compare_exchange_n(&l->busy,&n,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)?0:0xf519;}
static void unlock(IzPs2Launch*l){__atomic_store_n(&l->busy,0,__ATOMIC_RELEASE);}
static uint32_t retain(IzPs2Launch*l,uint32_t e){l->state=IZ_PS2_LAUNCH_RETAINED;l->error=e?e:0xf501;return l->error;}
static void put32(void*p,unsigned at,uint32_t v){memcpy((uint8_t*)p+at,&v,4);}
static void put64(void*p,unsigned at,uint64_t v){memcpy((uint8_t*)p+at,&v,8);}
static int token(const uint8_t*p){return !zero(p,16);}
static int leaf(const char*p,unsigned maximum){unsigned i=0;for(;i<256&&p[i];i++){unsigned c=(uint8_t)p[i];if(c<32||c==127||c=='/'||c=='\\'||c==':')return 0;}return i&&i<=maximum&&!(i==1&&p[0]=='.')&&!(i==2&&p[0]=='.'&&p[1]=='.');}
static void path(char*out,const char*prefix,const char*name){unsigned i=0;while(prefix[i]){out[i]=prefix[i];i++;}while(*name)out[i++]=*name++;out[i]=0;}
static int callbacks(const IzPs2Launch*l){
 const IzPs2Memory*m=l->plan.memory;const IzPs2Shadow*s=l->plan.shadow;const IzPs2Video*v=l->plan.video;const IzPs2Storage*f=l->plan.storage;const IzNativeApp6*a=l->plan.app;
 return a->base.environment&&a->graphics&&P(a->base.base.base.base.base.data)==l->memory.protected[1].base&&a->base.base.base.base.base.data_bytes==l->memory.protected[1].bytes&&a->base.base.base.base.base.magic==0x414e5a49&&a->base.base.base.base.base.version==6&&a->base.base.base.base.base.bytes==sizeof *a&&a->base.base.base.base.base.owner==l->app_owner&&
 !memcmp(a->base.environment,&l->environment,sizeof l->environment)&&!memcmp(a->graphics,&l->graphics,sizeof l->graphics)&&
 m->state==IZ_PS2_MEMORY_READY&&!m->entered&&!m->pending&&m->pid==l->environment.application_pid&&!memcmp(&m->ops,&l->memory.ops,sizeof m->ops)&&!memcmp(m->blocks,l->memory.blocks,sizeof m->blocks)&&!memcmp(m->protected,l->memory.protected,sizeof m->protected)&&
 s->state==IZ_PS2_SHADOW_READY&&!s->entered&&!s->pending&&s->memory==l->shadow.memory&&s->mapped==l->shadow.mapped&&!memcmp(&s->ops,&l->shadow.ops,sizeof s->ops)&&
 v->state==IZ_PS2_VIDEO_READY&&!v->entered&&!v->pending&&!memcmp(&v->ops,&l->video.ops,sizeof v->ops)&&!memcmp(v->slots,l->video.slots,sizeof v->slots)&&
 f->state==IZ_PS2_STORAGE_READY&&!f->entered&&f->fs==l->filesystem&&f->game_handle==l->game_handle&&f->bios_handle==l->bios_handle&&!f->directory&&!f->fs->c->poisoned&&f->selected<f->count&&f->bios_root<f->root_count&&
 !memcmp(f->games+f->selected,&l->game,sizeof l->game)&&!memcmp(f->bios+f->bios_root,&l->bios,sizeof l->bios);
}
static void ledger(IzPs2Launch*l){
 /* This component's actual SDK ownership inventory has five live blocks.
  * The dispatch entry is index3. The receipt never grants native ABI/PFN
  * execution permission; its qualified flag remains zero. */
 put32(l->hwgs,0,0x31445647);put32(l->hwgs,4,1);put32(l->hwgs,8,128);put32(l->hwgs,12,1);memcpy(l->hwgs+16,l->plan.av.token,16);
 put64(l->hwgs,32,l->plan.startup.context);put64(l->hwgs,40,l->plan.startup.hwgs);put64(l->hwgs,48,PAGE);put64(l->hwgs,56,l->plan.startup.dispatch);put64(l->hwgs,64,PAGE);put64(l->hwgs,72,3);put64(l->hwgs,80,5);
 const char*label="ps2-vulkan-dispatch";uint64_t hash=UINT64_C(14695981039346656037);for(unsigned i=0;label[i];i++)hash=(hash^(uint8_t)label[i])*UINT64_C(1099511628211);put64(l->hwgs,88,hash);
}
uint32_t iz_ps2_launch_begin(void*storage,size_t bytes,const IzPs2LaunchPlan*p){
 uint64_t base=P(storage),needed=iz_ps2_launch_bytes();
 if(!p||!storage||bytes<needed||!range(base,needed)||P(p)>UINT64_MAX-sizeof *p||overlap(base,needed,P(p),sizeof *p))return 0xf502;
 const IzPs2Memory*m=p->memory;const IzPs2Shadow*s=p->shadow;const IzPs2Video*v=p->video;const IzPs2Storage*f=p->storage;IzNativeApp6*a=p->app;
 if(!m||!s||!v||!f||!a||!a->base.environment||!a->graphics||!f->fs||!f->fs->c||!f->fs->buffer||a->base.base.base.base.base.magic!=0x414e5a49||a->base.base.base.base.base.version!=6||a->base.base.base.base.base.bytes!=sizeof *a||
 m->state!=IZ_PS2_MEMORY_READY||m->count!=5||m->wanted!=5||!m->hwgs||m->protected_count!=4||!m->ops.identity||!m->ops.query||!m->ops.release||m->entered||m->pending||m->busy||
 s->state!=IZ_PS2_SHADOW_READY||s->entered||s->pending||s->busy||v->state!=IZ_PS2_VIDEO_READY||v->entered||v->pending||v->busy||
 f->state!=IZ_PS2_STORAGE_READY||f->entered||f->busy||!f->count||f->count>IZ_PS2_STORAGE_GAMES||f->selected>=f->count||!f->root_count||f->root_count>IZ_PS2_STORAGE_ROOTS||f->bios_root>=f->root_count||!f->has_bios[f->bios_root]||
 !leaf(f->games[f->selected].name,256-sizeof("/sd/izmysh/ps2/games/"))||!leaf(f->bios[f->bios_root].name,255)||!token(p->main_nonce)||!token(p->pad_nonce)||!token(p->av_token)||!token(p->read_token)||!token(p->save_token))return 0xf503;
 const IzNativeApp*app=&a->base.base.base.base.base;IzPs2MemorySpan parent=m->protected[1];
 if(P(app->data)!=parent.base||app->data_bytes!=parent.bytes||!inside(base,needed,parent)||!range(parent.base,parent.bytes))return 0xf503;
 for(unsigned j=0;j<5;j++)if(!range(m->blocks[j].base,m->blocks[j].bytes)||overlap(parent.base,parent.bytes,m->blocks[j].base,m->blocks[j].bytes))return 0xf503;
 for(unsigned j=0;j<4;j++)if(j!=1&&(!range(m->protected[j].base,m->protected[j].bytes)||overlap(parent.base,parent.bytes,m->protected[j].base,m->protected[j].bytes)))return 0xf503;
 if(!range(s->mapped,IZ_PS2_JIT_BYTES)||overlap(parent.base,parent.bytes,s->mapped,IZ_PS2_JIT_BYTES))return 0xf503;
 const uint64_t objects[]={P(m),P(s),P(v),P(f)},sizes[]={sizeof *m,sizeof *s,sizeof *v,sizeof *f};
 for(unsigned j=0;j<4;j++)if(!inside(objects[j],sizes[j],parent)||overlap(base,needed,objects[j],sizes[j]))return 0xf503;
 if(overlap(base,needed,P(a),sizeof *a)||overlap(base,needed,P(a->base.environment),sizeof *a->base.environment)||overlap(base,needed,P(a->graphics),sizeof *a->graphics)||
 overlap(base,needed,P(f->fs),sizeof *f->fs)||overlap(base,needed,P(f->fs->c),sizeof *f->fs->c)||overlap(base,needed,P(f->fs->buffer),CHUNK)||
 inside(P(m->ops.owner),1,(IzPs2MemorySpan){base,needed})||inside(P(s->ops.owner),1,(IzPs2MemorySpan){base,needed})||inside(P(v->ops.owner),1,(IzPs2MemorySpan){base,needed}))return 0xf503;
 if(!zero(storage,sizeof(IzPs2Launch)))return 0xf519;
 IzPs2Launch*l=storage;l->self=base;l->total=needed;l->plan.app=a;l->plan.memory=p->memory;l->plan.shadow=p->shadow;l->plan.video=p->video;l->plan.storage=p->storage;
 l->memory=*m;l->shadow=*s;l->video=*v;l->environment=*a->base.environment;l->graphics=*a->graphics;l->app_owner=app->owner;l->filesystem=f->fs;l->game_handle=f->game_handle;l->bios_handle=f->bios_handle;l->game=f->games[f->selected];l->bios=f->bios[f->bios_root];
 l->execution=base+round_page(sizeof *l);l->tail=l->execution+round_page(iz_ps2_execution_bytes());l->tail_bytes=base+needed-l->tail;
 uint64_t at=l->tail;IzPs2StartupInput*i=&l->plan.startup;i->version=1;i->bytes=sizeof *i;i->flags=IZ_PS2_STARTUP_HWGS;i->environment=l->environment;i->image=p->image;
 uint64_t*pages[]={&i->context,&i->vm,&i->domains,&i->main_descriptor,&l->plan.main_receipt,&i->pad,&i->control,&i->log,&i->av,&l->bindings.mount,&l->bindings.files,&l->bindings.segments,&i->save};
 for(unsigned j=0;j<13;j++){*pages[j]=at;at+=PAGE;}
 i->filesystem=l->bindings.mount;l->bindings.save=i->save;l->bindings.sdk=i->environment.sdk;
 i->ring=at;at+=32768;l->bindings.read_cache=at;at+=CHUNK;l->bindings.save_cache=at;at+=CHUNK;
 IzPs2AvInput*av=&l->plan.av;av->version=1;av->bytes=sizeof *av;av->image_count=a->graphics->image_count;av->av_page=i->av;av->vulkan=a->graphics->vulkan;av->device=a->graphics->device;av->queue=a->graphics->queue;av->swapchain=a->graphics->swapchain;av->audio=at;at+=IZ_PS2_AUDIO_BYTES;av->scratch=at;av->scratch_bytes=IZ_PS2_SCANOUT_BYTES;at+=av->scratch_bytes;
 if(at!=base+needed){memset(l,0,sizeof *l);return 0xf502;}
 i->main=m->blocks[IZ_PS2_MAIN].base;i->jit_source=m->blocks[IZ_PS2_JIT_SOURCE].base;i->stack=m->blocks[IZ_PS2_ENTRY_STACK].base;i->dispatch=m->blocks[IZ_PS2_DISPATCH].base;i->hwgs=m->blocks[IZ_PS2_HWGS_LEDGER].base;i->jit_shadow=s->mapped;i->jit_rx=p->jit_rx;i->gpu_instance=a->graphics->instance;i->gpu_physical=a->graphics->physical;i->gpu_device=a->graphics->device;
 memcpy(i->main_nonce,p->main_nonce,16);memcpy(i->pad_nonce,p->pad_nonce,16);memcpy(av->token,p->av_token,16);memcpy(l->bindings.read_token,p->read_token,16);memcpy(l->bindings.save_token,p->save_token,16);
 path(i->paths[0],"/scratch","");path(i->paths[1],"/resources","");path(i->paths[2],"",l->bios.name);path(i->paths[3],"/sd/izmysh/ps2/games/",l->game.name);
 for(unsigned j=0;j<4;j++)if(j!=1)av->protected[av->protected_count++]=(IzPs2AvSpan){m->protected[j].base,m->protected[j].bytes};
 for(unsigned j=0;j<5;j++)av->protected[av->protected_count++]=(IzPs2AvSpan){m->blocks[j].base,m->blocks[j].bytes};av->protected[av->protected_count++]=(IzPs2AvSpan){s->mapped,IZ_PS2_JIT_BYTES};
 uint32_t rc=iz_ps2_pack_startup(i,l->plan.main_receipt,&l->startup);if(!rc)rc=iz_ps2_storage_pack(f,&l->bindings,&l->files);if(!rc)rc=iz_ps2_video_pack_av(p->video,av,l->av);if(!rc)rc=iz_ps2_gpu_dispatch(av->vulkan,l->dispatch);if(!rc)ledger(l);
 if(!rc)rc=iz_ps2_execution_begin((void*)(uintptr_t)l->execution,iz_ps2_execution_bytes(),&l->plan);
 if(rc){memset(l,0,sizeof *l);return rc;}
 l->magic=MAGIC;l->state=IZ_PS2_LAUNCH_PUBLISHING;l->phase=CLEAR;return 0;
}
static Span task(const IzPs2Launch*l,const uint8_t**source,uint8_t*fill){
 const IzPs2StartupInput*i=&l->plan.startup;*source=0;*fill=0;
 switch(l->phase){
 case CLEAR:return (Span){l->tail,l->tail_bytes};
 case STARTUP:{const uint64_t p[]={i->context,i->vm,i->domains,i->main_descriptor,l->plan.main_receipt,i->pad,i->control,i->log};*source=(const uint8_t*)&l->startup+l->index*PAGE;return (Span){p[l->index],PAGE};}
 case AV:*source=l->av;return (Span){i->av,PAGE};
 case FILES:{const uint64_t p[]={l->bindings.mount,l->bindings.files,l->bindings.segments,l->bindings.save};*source=(const uint8_t*)&l->files+l->index*PAGE;return (Span){p[l->index],PAGE};}
 case DISPATCH:*source=l->dispatch;return (Span){i->dispatch,PAGE};
 case HWGS:*source=l->hwgs;return (Span){i->hwgs,PAGE};
 case LOW_GUARD:*fill=0x6d;return (Span){i->stack,PAGE};
 case HIGH_GUARD:*fill=0x6d;return (Span){i->stack+PAGE+IZ_PS2_STACK_BYTES,PAGE};
 default:return (Span){0,0};
 }
}
static void next(IzPs2Launch*l){l->offset=l->walk=l->end=0;l->regions=l->writing=0;
 if((l->phase==STARTUP&&++l->index<8)||(l->phase==FILES&&++l->index<4))return;l->index=0;
 if(++l->phase==EXECUTE){l->published=1;l->state=IZ_PS2_LAUNCH_EXECUTING;}
}
static uint32_t publish(IzPs2Launch*l){
 if(!callbacks(l))return retain(l,0xf504);uint64_t pid=0;if(l->memory.ops.identity(l->memory.ops.owner,&pid)||pid!=l->environment.application_pid)return retain(l,0xf505);
 const uint8_t*source;uint8_t fill;Span span=task(l,&source,&fill);if(!span.base||l->offset>=span.bytes)return retain(l,0xf506);uint64_t n=span.bytes-l->offset;if(n>CHUNK)n=CHUNK;
 if(!l->writing){
  if(!l->walk){l->walk=span.base+l->offset;l->end=l->walk+n;l->regions=0;}
  IzPs2MemoryRegion r={0};uint32_t rc=l->memory.ops.query(l->memory.ops.owner,l->walk,&r);
  if(rc||++l->regions>16||!range(r.base,r.bytes)||r.base>l->walk||r.base+r.bytes<=l->walk||r.type!=5||r.permission!=3||r.attributes||r.ipc_refs||r.device_refs)return retain(l,0xf507);
  uint64_t end=r.base+r.bytes;l->walk=end<l->end?end:l->end;if(l->walk==l->end)l->writing=1;return 0;
 }
 void*out=(void*)(uintptr_t)(span.base+l->offset);if(source){memcpy(out,source+l->offset,(size_t)n);l->published_bytes+=n;}else{memset(out,fill,(size_t)n);l->cleared_bytes+=n;}
 l->offset+=n;l->writing=0;l->walk=l->end=0;if(l->offset==span.bytes)next(l);return 0;
}
uint32_t iz_ps2_launch_step(IzPs2Launch*l,uint32_t budget,IzPs2LaunchProgress*out){
 if(!l||!out||!budget||budget>64||l->magic!=MAGIC||l->self!=P(l)||P(out)>UINT64_MAX-sizeof *out||overlap(P(l),l->total,P(out),sizeof *out))return 0xf502;
 uint32_t rc=lock(l);if(rc)return rc;
 while(l->state!=IZ_PS2_LAUNCH_CLOSED&&l->state!=IZ_PS2_LAUNCH_RETAINED&&budget--){
  if(l->stop_requested&&l->state==IZ_PS2_LAUNCH_PUBLISHING){rc=iz_ps2_execution_stop((void*)(uintptr_t)l->execution);if(rc){retain(l,rc);break;}l->state=IZ_PS2_LAUNCH_RETIRING;}
  if(l->state==IZ_PS2_LAUNCH_PUBLISHING)rc=publish(l);
  else{rc=iz_ps2_execution_step((void*)(uintptr_t)l->execution,1,&l->progress);if(rc||l->progress.state==IZ_PS2_EXECUTION_RETAINED)retain(l,rc?rc:l->progress.error);else if(l->progress.state==IZ_PS2_EXECUTION_CLOSED)l->state=IZ_PS2_LAUNCH_CLOSED;}
  l->units++;if(rc)break;
 }
 *out=(IzPs2LaunchProgress){1,sizeof *out,l->state,l->phase,l->error,l->stop_requested,l->published,0,l->units,l->cleared_bytes,l->published_bytes,l->progress};rc=l->error;unlock(l);return rc;
}
uint32_t iz_ps2_launch_stop(IzPs2Launch*l){
 if(!l||l->magic!=MAGIC||l->self!=P(l))return 0xf502;uint32_t rc=lock(l);if(rc)return rc;
 if(l->state==IZ_PS2_LAUNCH_RETAINED)rc=l->error;else if(l->state!=IZ_PS2_LAUNCH_CLOSED){l->stop_requested=1;rc=iz_ps2_execution_stop((void*)(uintptr_t)l->execution);if(rc)retain(l,rc);}unlock(l);return rc;
}
uint32_t iz_ps2_launch_describe(IzPs2Launch*l,IzPs2ExecutionPlan*out){
 if(!l||!out||l->magic!=MAGIC||l->self!=P(l)||P(out)>UINT64_MAX-sizeof *out||overlap(P(l),l->total,P(out),sizeof *out))return 0xf502;uint32_t rc=lock(l);if(rc)return rc;*out=l->plan;unlock(l);return 0;
}
