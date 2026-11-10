#include "execution.h"
#include "execution-internal.h"
#include "execution-profile.h"
#include "receipts.h"
extern void*memcpy(void*,const void*,size_t);
extern void*memset(void*,int,size_t);
extern int memcmp(const void*,const void*,size_t);
extern uint32_t iz_ps2_memory_retire(IzPs2Memory*,const IzPs2Execution*);
extern uint32_t iz_ps2_shadow_retire(IzPs2Shadow*,const IzPs2Execution*);
extern uint32_t iz_ps2_video_retire(IzPs2Video*,const IzPs2Execution*);
extern uint32_t iz_ps2_storage_retire(IzPs2Storage*,const IzPs2Execution*);
#define P(p) ((uint64_t)(uintptr_t)(p))
#define LIMIT (UINT64_C(1)<<39)
#define MAGIC UINT64_C(0x31584532505a49)
#define SAFE UINT64_C(9007199254740991)
enum {ADMIT=1,ACQUIRE,ENTER,POLL,STOP_WRITE,JOIN,POST,PROOF,
 VIDEO_BEGIN,VIDEO_STEP,SHADOW_BEGIN,SHADOW_STEP,STORAGE_BEGIN,STORAGE_STEP,
 MEMORY_BEGIN,MEMORY_STEP,PRESENTATION,IMAGE_BEGIN,IMAGE_STEP,CLOSED};
struct IzPs2Execution {
 uint64_t magic,self;uint32_t busy,state,phase,error,joined,proof_valid;
 uint32_t stop_requested,stop_written,preentry,task,regions,reserved;
 uint64_t units,worker_ticket,presentation_ticket,poll_result,walk,end;
 IzPs2ExecutionPlan plan;IzNativeEnvironment environment;IzNativeGraphics graphics;
 IzNativeApp6 app;IzPs2Memory memory;IzPs2Shadow shadow;IzPs2Video video;
 IzFs*filesystem;uint32_t game_handle,bios_handle;
 IzPs2StartupPages initial;_Alignas(8) uint8_t av[4096];
 IzPs2ReceiptCapsule capsule;IzPs2ReceiptScratch scratch;IzPs2Receipts receipts;
};
static int span(uint64_t p,uint64_t n){return p&&p<LIMIT&&n&&n<=LIMIT-p;}
static int inside(uint64_t p,uint64_t n,IzPs2MemorySpan s){return span(p,n)&&p>=s.base&&p-s.base<=s.bytes&&n<=s.bytes-(p-s.base);}
static int overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m){return a<b+m&&b<a+n;}
static uint32_t r32(const void*p){uint32_t n;memcpy(&n,p,4);return n;}
static uint64_t r64(const void*p){uint64_t n;memcpy(&n,p,8);return n;}
static int zero(const void*p,size_t n){const uint8_t*b=p;for(size_t i=0;i<n;i++)if(b[i])return 0;return 1;}
static uint32_t lock(IzPs2Execution*e){uint32_t n=0;return __atomic_compare_exchange_n(&e->busy,&n,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)?0:0xf419;}
static void unlock(IzPs2Execution*e){__atomic_store_n(&e->busy,0,__ATOMIC_RELEASE);}
static uint32_t retain(IzPs2Execution*e,uint32_t rc){e->state=IZ_PS2_EXECUTION_RETAINED;e->error=rc?rc:0xf401;return e->error;}
static int callbacks(const IzPs2Execution*e){const IzNativeApp6*a=e->plan.app;
 return a->base.base.base.base.base.version==6&&a->base.base.base.base.base.bytes==sizeof *a&&a->base.base.base.base.base.owner==e->app.base.base.base.base.base.owner&&
 a->base.environment==e->app.base.environment&&a->graphics==e->app.graphics&&
 a->base.worker_begin==e->app.base.worker_begin&&a->base.worker_poll==e->app.base.worker_poll&&a->base.worker_join==e->app.base.worker_join&&
 a->presentation_acquire==e->app.presentation_acquire&&a->presentation_release==e->app.presentation_release&&
 a->base.base.image_release==e->app.base.base.image_release&&a->base.base.image_step==e->app.base.base.image_step&&
 !memcmp(a->base.environment,&e->environment,sizeof e->environment)&&!memcmp(a->graphics,&e->graphics,sizeof e->graphics);
}
static int owners(const IzPs2Execution*e){const IzPs2Memory*m=e->plan.memory;const IzPs2Shadow*s=e->plan.shadow;const IzPs2Video*v=e->plan.video;const IzPs2Storage*f=e->plan.storage;
 return !m->pending&&!s->pending&&!v->pending&&m->pid==e->environment.application_pid&&s->pid==m->pid&&v->pid==m->pid&&
 !memcmp(&m->ops,&e->memory.ops,sizeof m->ops)&&!memcmp(&s->ops,&e->shadow.ops,sizeof s->ops)&&!memcmp(&v->ops,&e->video.ops,sizeof v->ops)&&
 !memcmp(m->blocks,e->memory.blocks,sizeof m->blocks)&&!memcmp(m->protected,e->memory.protected,sizeof m->protected)&&
 m->count==e->memory.count&&m->wanted==e->memory.wanted&&m->hwgs==e->memory.hwgs&&m->protected_count==4&&
 s->memory==e->shadow.memory&&s->mapped==e->shadow.mapped&&s->map_known==1&&s->device==e->shadow.device&&
 !memcmp(&s->source,&e->shadow.source,sizeof s->source)&&!memcmp(v->slots,e->video.slots,sizeof v->slots)&&
 !memcmp(&v->graphics,&e->graphics,sizeof e->graphics)&&f->fs==e->filesystem&&f->game_handle==e->game_handle&&f->bios_handle==e->bios_handle&&!f->directory&&!f->fs->c->poisoned;
}
size_t iz_ps2_execution_bytes(void){return sizeof(IzPs2Execution);}
uint32_t iz_ps2_execution_begin(void*storage,size_t bytes,const IzPs2ExecutionPlan*p){
 if(!storage||!p||bytes<sizeof(IzPs2Execution)||(P(storage)&7)||P(storage)>UINT64_MAX-sizeof(IzPs2Execution)||P(p)>UINT64_MAX-sizeof *p||overlap(P(storage),sizeof(IzPs2Execution),P(p),sizeof *p))return 0xf402;
 if(!zero(storage,sizeof(IzPs2Execution)))return 0xf419;
 const IzPs2StartupInput*i=&p->startup;IzPs2Memory*m=p->memory;IzPs2Shadow*s=p->shadow;IzPs2Video*v=p->video;IzPs2Storage*f=p->storage;IzNativeApp6*a=p->app;
 if(!m||!s||!v||!f||!a||!a->base.environment||!a->graphics||!a->base.worker_begin||!a->base.worker_poll||!a->base.worker_join||!a->presentation_acquire||!a->presentation_release||!a->base.base.image_release||!a->base.base.image_step||a->base.base.base.base.base.version!=6||a->base.base.base.base.base.bytes!=sizeof *a||
 !m->ops.identity||!m->ops.query||!m->ops.release||!s->ops.identity||!s->ops.query||!s->ops.unmap||!s->ops.release||!v->ops.identity||!v->ops.query||!v->ops.call||m->busy||s->busy||v->busy||f->busy||m->pending||s->pending||v->pending||m->entered||s->entered||v->entered||f->entered||
 m->state!=IZ_PS2_MEMORY_READY||s->state!=IZ_PS2_SHADOW_READY||v->state!=IZ_PS2_VIDEO_READY||f->state!=IZ_PS2_STORAGE_READY||m->count!=5||m->wanted!=5||!m->hwgs||m->protected_count!=4||
 memcmp(&i->environment,a->base.environment,sizeof i->environment)||memcmp(a->graphics,&v->graphics,sizeof v->graphics)||m->pid!=i->environment.application_pid||s->pid!=m->pid||v->pid!=m->pid||
 i->flags!=IZ_PS2_STARTUP_HWGS||i->main!=m->blocks[0].base||i->jit_source!=m->blocks[1].base||i->stack!=m->blocks[2].base||i->dispatch!=m->blocks[3].base||i->hwgs!=m->blocks[4].base||
 i->jit_shadow!=s->mapped||!s->memory||!s->map_known||s->source.base!=i->jit_source||s->source.bytes!=IZ_PS2_JIT_BYTES||s->instance!=a->graphics->instance||s->physical!=a->graphics->physical||s->device!=a->graphics->device||s->selection.memory_type!=2||s->selection.flags!=15||s->selection.heap||!s->selection.coherent||s->selection.heap_bytes<IZ_PS2_JIT_BYTES||
 m->protected[2].base!=P(i->image.code)||m->protected[2].bytes!=i->image.code_bytes||m->protected[3].base!=P(i->image.data)||m->protected[3].bytes!=i->image.data_bytes||
 !f->fs||!f->fs->c||f->fs->c->poisoned||f->fs->c->filesystem!=i->environment.filesystem||!f->game_handle||!f->bios_handle||f->directory)return 0xf403;
 const uint64_t block_sizes[]={IZ_PS2_MAIN_BYTES,IZ_PS2_JIT_BYTES,IZ_PS2_STACK_BYTES+8192,4096,4096};
 for(unsigned j=0;j<5;j++)if(m->blocks[j].bytes!=block_sizes[j])return 0xf403;
 if(m->protected[0].base!=i->environment.sdk||m->protected[0].bytes!=i->environment.sdk_bytes||f->game_handle==f->bios_handle||f->game_handle==i->environment.filesystem||f->bios_handle==i->environment.filesystem)return 0xf403;
 unsigned game=0,bios=0;for(unsigned j=0;j<32;j++){game+=f->fs->handles[j]==f->game_handle;bios+=f->fs->handles[j]==f->bios_handle;}if(game!=1||bios!=1)return 0xf403;
 for(unsigned j=0;j<2;j++)if(!v->slots[j].pipeline||!v->slots[j].shaders[0]||!v->slots[j].shaders[1]||!v->slots[j].mapped_known)return 0xf403;
 IzPs2MemorySpan parent=m->protected[1];if(!inside(P(storage),sizeof(IzPs2Execution),parent))return 0xf403;
 const uint64_t object_at[]={P(m),P(s),P(v),P(f)},object_bytes[]={sizeof *m,sizeof *s,sizeof *v,sizeof *f};
 for(unsigned j=0;j<4;j++){if(!inside(object_at[j],object_bytes[j],parent)||overlap(object_at[j],object_bytes[j],P(storage),sizeof(IzPs2Execution)))return 0xf403;for(unsigned k=0;k<j;k++)if(overlap(object_at[j],object_bytes[j],object_at[k],object_bytes[k]))return 0xf403;}
 uint64_t pages[]={i->context,i->vm,i->domains,i->main_descriptor,p->main_receipt,i->pad,i->control,i->log,i->av};
 for(unsigned j=0;j<9;j++){if(!inside(pages[j],4096,parent)||overlap(pages[j],4096,P(storage),sizeof(IzPs2Execution)))return 0xf403;for(unsigned k=0;k<4;k++)if(overlap(pages[j],4096,object_at[k],object_bytes[k]))return 0xf403;}
 IzPs2Execution*e=storage;e->plan=*p;e->environment=*a->base.environment;e->graphics=*a->graphics;e->app=*a;e->memory=*m;e->shadow=*s;e->video=*v;e->filesystem=f->fs;e->game_handle=f->game_handle;e->bios_handle=f->bios_handle;
 uint32_t rc=iz_ps2_pack_startup(i,p->main_receipt,&e->initial);if(!rc)rc=iz_ps2_video_pack_av(v,&p->av,e->av);
 if(rc){memset(e,0,sizeof *e);return rc;}
 if(p->av.av_page!=i->av||i->gpu_instance!=a->graphics->instance||i->gpu_physical!=a->graphics->physical||i->gpu_device!=a->graphics->device){memset(e,0,sizeof *e);return 0xf403;}
 e->self=P(e);e->magic=MAGIC;e->state=IZ_PS2_EXECUTION_ADMIT;e->phase=ADMIT;return 0;
}
static uint64_t address(const IzPs2Execution*e,unsigned n){const IzPs2StartupInput*i=&e->plan.startup;
 const uint64_t a[]={i->context,i->vm,i->domains,i->main_descriptor,e->plan.main_receipt,i->pad,i->control,i->log,i->av,i->stack,i->stack+4096+IZ_PS2_STACK_BYTES,P(i->image.data),i->jit_rx};return a[n];
}
static uint64_t task_bytes(const IzPs2Execution*e,unsigned n){return n==11?e->plan.startup.image.data_bytes:n==12?IZ_PS2_JIT_BYTES:4096;}
static int match_context(const uint8_t*a,const uint8_t*b){
 if(r32(a)!=0x33425848||r32(a+4)!=3||r32(a+8)!=0x2b8||r32(a+12)||memcmp(a+0xc8,b+0xc8,256))return 0;
 const unsigned offsets[]={48,64,72,80,88,96,104,112,120,128,168,0xc8,0xd0,0xd8,0x188,0x250};
 for(unsigned i=0;i<sizeof offsets/sizeof *offsets;i++)if(r64(a+offsets[i])!=r64(b+offsets[i]))return 0;
 return !memcmp(a+0x208,b+0x208,64);
}
static uint32_t match_task(IzPs2Execution*e,int post){const uint8_t*p=(const void*)(uintptr_t)address(e,e->task);unsigned n=e->task;
 if(n<8){const uint8_t*original=(const uint8_t*)&e->initial+n*4096;
  if(!post)return memcmp(p,original,4096)?0xf404:0;
  if(n==0)return match_context(p,original)?0:0xf404;
  if(n==3)return memcmp(p,original,4096)?0xf404:0;
  if(n==4){uint8_t expected[112];memcpy(expected,e->initial.main_receipt,112);uint32_t state=3;uint64_t one=1;memcpy(expected+12,&state,4);memcpy(expected+88,&one,8);memcpy(expected+96,&one,8);return memcmp(p,expected,112)?0xf405:0;}
 }else if(n==8)return memcmp(p,e->av,4096)?0xf406:0;
 else if(n==9||n==10){for(unsigned i=0;i<4096;i++)if(p[i]!=0x6d)return 0xf407;}
 return 0;
}
static uint32_t query_task(IzPs2Execution*e,int post){
 if(post&&(e->task==1||e->task==2||e->task==5||e->task==6||e->task==7)){e->task++;return 0;}
 if(!e->walk){e->walk=address(e,e->task);e->end=e->walk+task_bytes(e,e->task);e->regions=0;}
 if(++e->regions>32768)return retain(e,0xf408);IzPs2MemoryRegion r={0};uint32_t rc=e->memory.ops.query(e->memory.ops.owner,e->walk,&r);
 int free_range=e->task==12;
 if(rc||r.base>=LIMIT||!r.bytes||r.bytes>LIMIT-r.base||(r.base&4095)||(r.bytes&4095)||r.base>e->walk||r.base+r.bytes<=e->walk||r.attributes||r.ipc_refs||r.device_refs||r.type!=(free_range?0u:5u)||r.permission!=(free_range?0u:3u))return retain(e,0xf408);
 e->walk=r.base+r.bytes<e->end?r.base+r.bytes:e->end;
 if(e->walk==e->end){e->walk=0;rc=match_task(e,post);if(rc)return retain(e,rc);if(++e->task==13){e->phase=post?PROOF:ACQUIRE;e->task=0;}}
 return 0;
}
static int safe_words(const void*p,unsigned count){for(unsigned i=0;i<count;i++)if(r64((const uint8_t*)p+i*8)>SAFE)return 0;return 1;}
static const uint8_t*export_at(const IzPs2Execution*e,unsigned n){return (const uint8_t*)e->plan.startup.image.data+iz_ps2_exports[n].offset;}
static uint32_t prove(IzPs2Execution*e){
 const unsigned zeros[]={IZ_PS2_EXPORT_G_THREADS,IZ_PS2_EXPORT_G_NCHUNKS,IZ_PS2_EXPORT_G_USED_PAGES,IZ_PS2_EXPORT_G_MEMORY_POISONED,IZ_PS2_EXPORT_G_STACK_CREATOR_COUNT};
 for(unsigned i=0;i<sizeof zeros/sizeof *zeros;i++)if(!zero(export_at(e,zeros[i]),iz_ps2_exports[zeros[i]].bytes))return retain(e,0xf409);
 const uint8_t*t=export_at(e,IZ_PS2_EXPORT_PS2_SW_TEMPORAL_STATISTICS),*g=export_at(e,IZ_PS2_EXPORT_PS2_GS_PORTABLE_STATS);
 if(!safe_words(t,5)||r64(t)!=r64(t+8)||r64(t+16)||!safe_words(g,8)||!r64(g)||r64(g)!=r64(g+8)||r64(g+16)||r64(g+24)||r64(g+40)||r64(g)>SAFE/4194304||r64(g+48)!=r64(g)*4194304||r64(g+56)!=r64(g+48)||r64(g+32)<4194304||r64(g+32)>16*4194304||r64(g+32)%4194304)return retain(e,0xf409);
 memcpy(&e->capsule,export_at(e,IZ_PS2_EXPORT_PS2_LOCAL_RECEIPTS),sizeof e->capsule);
 IzPs2ReceiptImage image={e->plan.startup.context,P(e->plan.startup.image.code),IZ_PS2_CODE_BYTES,P(e->plan.startup.image.data),IZ_PS2_DATA_BYTES};
 uint32_t rc=iz_ps2_decode_receipts(&e->receipts,&e->scratch,&e->capsule,&image);if(rc||iz_ps2_validate_storage_status(export_at(e,IZ_PS2_EXPORT_PS2_LOCAL_STORAGE_STATUS)))return retain(e,0xf40a);
 uint64_t maps=r64(export_at(e,IZ_PS2_EXPORT_G_SDK_STACK_MAPS)),scopes=r64(export_at(e,IZ_PS2_EXPORT_G_SDK_STACK_SCOPES_ENTERED));uint32_t exit=r32(export_at(e,IZ_PS2_EXPORT_PS2_EXIT_REQUESTED));
 if(maps<2||maps>SAFE||scopes<e->receipts.created||scopes>SAFE||exit>1||(exit&&!e->receipts.exit_receipt_present)||!owners(e))return retain(e,0xf40b);
 e->proof_valid=1;e->state=IZ_PS2_EXECUTION_RETIRING;e->phase=VIDEO_BEGIN;return 0;
}
uint32_t iz_ps2_execution_release_match(const IzPs2Execution*e,const void*owner,uint32_t kind){
 if(!e||e->magic!=MAGIC||e->self!=P(e)||!e->busy||e->state!=IZ_PS2_EXECUTION_RETIRING||!e->joined||!e->proof_valid||e->preentry||e->error||!callbacks(e))return 0;
 if(kind==IZ_PS2_RETIRE_VIDEO)return e->phase==VIDEO_BEGIN&&owner==e->plan.video&&owners(e);
 if(kind==IZ_PS2_RETIRE_SHADOW)return e->phase==SHADOW_BEGIN&&owner==e->plan.shadow&&e->plan.video->state==IZ_PS2_VIDEO_CLOSED&&e->plan.shadow->memory==e->shadow.memory&&e->plan.shadow->mapped==e->shadow.mapped&&!memcmp(&e->plan.shadow->ops,&e->shadow.ops,sizeof e->shadow.ops);
 if(kind==IZ_PS2_RETIRE_STORAGE)return e->phase==STORAGE_BEGIN&&owner==e->plan.storage&&e->plan.shadow->state==IZ_PS2_SHADOW_CLOSED&&e->plan.storage->fs==e->filesystem&&e->plan.storage->game_handle==e->game_handle&&e->plan.storage->bios_handle==e->bios_handle;
 if(kind==IZ_PS2_RETIRE_MEMORY)return e->phase==MEMORY_BEGIN&&owner==e->plan.memory&&e->plan.storage->state==IZ_PS2_STORAGE_CLOSED&&!memcmp(e->plan.memory->blocks,e->memory.blocks,sizeof e->memory.blocks)&&!memcmp(&e->plan.memory->ops,&e->memory.ops,sizeof e->memory.ops);
 return 0;
}
static uint32_t advance(IzPs2Execution*e){uint32_t rc=0,done=0;IzNativeApp*app=&e->plan.app->base.base.base.base.base;
 if(!callbacks(e))return retain(e,0xf40c);
 /* The resource steppers perform their own fresh identity query. */
 if(e->phase!=VIDEO_STEP&&e->phase!=SHADOW_STEP&&e->phase!=MEMORY_STEP){uint64_t pid=0;if(e->memory.ops.identity(e->memory.ops.owner,&pid)||pid!=e->environment.application_pid)return retain(e,0xf40d);}
 if(e->stop_requested&&e->phase<=ENTER){e->preentry=1;e->state=IZ_PS2_EXECUTION_RETIRING;e->phase=VIDEO_BEGIN;}
 switch(e->phase){
 case ADMIT:return query_task(e,0);
 case ACQUIRE:if(!owners(e))return retain(e,0xf40e);e->presentation_ticket=0;rc=e->app.presentation_acquire(app,&e->presentation_ticket);if(rc||!e->presentation_ticket)return retain(e,0xf40f);e->phase=ENTER;break;
 case ENTER:if(!owners(e))return retain(e,0xf40e);
  if(iz_ps2_memory_entered(e->plan.memory)||iz_ps2_shadow_entered(e->plan.shadow)||iz_ps2_video_entered(e->plan.video)||iz_ps2_storage_entered(e->plan.storage))return retain(e,0xf410);
  e->worker_ticket=0;rc=e->app.base.worker_begin(app,e->plan.startup.image.token,(void*)(uintptr_t)e->plan.startup.context,4096,&e->worker_ticket);if(rc||!e->worker_ticket)return retain(e,0xf411);e->state=IZ_PS2_EXECUTION_RUNNING;e->phase=POLL;break;
 case POLL:if(e->stop_requested&&!e->stop_written){e->phase=STOP_WRITE;break;}{IzNativeWorkerProgress p={0};rc=e->app.base.worker_poll(app,e->worker_ticket,&p);if(rc||p.version!=1||p.bytes!=sizeof p||p.reserved||(p.state!=IZ_WORKER_RUNNING&&p.state!=IZ_WORKER_FINISHED)||(p.state==IZ_WORKER_RUNNING&&p.result))return retain(e,0xf412);if(p.state==IZ_WORKER_FINISHED){e->poll_result=p.result;e->phase=JOIN;e->state=IZ_PS2_EXECUTION_JOINING;}break;}
 case STOP_WRITE:{uint64_t at=P(export_at(e,IZ_PS2_EXPORT_PS2_EXIT_REQUESTED));IzPs2MemoryRegion r={0};rc=e->memory.ops.query(e->memory.ops.owner,at,&r);if(rc||!span(r.base,r.bytes)||r.base>at||at-r.base>r.bytes||4>r.bytes-(at-r.base)||r.type!=5||r.permission!=3||r.attributes||r.ipc_refs||r.device_refs)return retain(e,0xf408);__atomic_store_n((uint32_t*)(uintptr_t)at,1,__ATOMIC_RELEASE);e->stop_written=1;e->phase=POLL;break;}
 case JOIN:{uint64_t result=UINT64_MAX;rc=e->app.base.worker_join(app,e->worker_ticket,&result);if(rc)return retain(e,0xf413);e->joined=1;if(result||result!=e->poll_result)return retain(e,0xf414);e->state=IZ_PS2_EXECUTION_VERIFYING;e->phase=POST;e->task=0;e->walk=0;break;}
 case POST:return query_task(e,1);
 case PROOF:return prove(e);
 case VIDEO_BEGIN:rc=e->preentry?iz_ps2_video_cancel(e->plan.video,0):iz_ps2_video_retire(e->plan.video,e);if(!rc)e->phase=VIDEO_STEP;break;
 case VIDEO_STEP:rc=iz_ps2_video_step(e->plan.video,1,&done);if(!rc&&done){if(e->plan.video->state!=IZ_PS2_VIDEO_CLOSED)rc=0xf415;else e->phase=SHADOW_BEGIN;}break;
 case SHADOW_BEGIN:rc=e->preentry?iz_ps2_shadow_cancel(e->plan.shadow,0):iz_ps2_shadow_retire(e->plan.shadow,e);if(!rc)e->phase=SHADOW_STEP;break;
 case SHADOW_STEP:rc=iz_ps2_shadow_step(e->plan.shadow,1,&done);if(!rc&&done){if(e->plan.shadow->state!=IZ_PS2_SHADOW_CLOSED)rc=0xf415;else e->phase=STORAGE_BEGIN;}break;
 case STORAGE_BEGIN:rc=e->preentry?iz_ps2_storage_cancel(e->plan.storage,0):iz_ps2_storage_retire(e->plan.storage,e);if(!rc)e->phase=STORAGE_STEP;break;
 case STORAGE_STEP:rc=iz_ps2_storage_step(e->plan.storage,1,&done);if(!rc&&done){if(e->plan.storage->state!=IZ_PS2_STORAGE_CLOSED)rc=0xf415;else e->phase=MEMORY_BEGIN;}break;
 case MEMORY_BEGIN:rc=e->preentry?iz_ps2_memory_cancel(e->plan.memory,0):iz_ps2_memory_retire(e->plan.memory,e);if(!rc)e->phase=MEMORY_STEP;break;
 case MEMORY_STEP:rc=iz_ps2_memory_step(e->plan.memory,1,&done);if(!rc&&done){if(e->plan.memory->state!=IZ_PS2_MEMORY_CLOSED)rc=0xf415;else e->phase=PRESENTATION;}break;
 /* ABI 6 refuses image release while presentation is held. All engine users
  * and owned GPU graphs are already retired here; release the actual lease
  * before asking the supervisor to unload the image. Unknown release keeps
  * the image and its owner retained, with no second attempt. */
 case PRESENTATION:if(e->presentation_ticket){rc=e->app.presentation_release(app,e->presentation_ticket);if(rc)break;e->presentation_ticket=0;}e->phase=IMAGE_BEGIN;break;
 case IMAGE_BEGIN:rc=e->app.base.base.image_release(app,e->plan.startup.image.token);if(!rc)e->phase=IMAGE_STEP;break;
 case IMAGE_STEP:{IzNativeImageProgress p={0};IzNativeImage image={0};rc=e->app.base.base.image_step(app,e->plan.startup.image.token,1,&p,&image);if(!rc){if(p.version!=1||p.bytes!=sizeof p||p.error)rc=0xf416;else if(p.done){if(p.state!=IZ_NATIVE_IMAGE_CLOSED)rc=0xf416;else{e->state=IZ_PS2_EXECUTION_CLOSED;e->phase=CLOSED;}}}break;}
 default:return retain(e,0xf417);
 }return rc?retain(e,rc):0;
}
uint32_t iz_ps2_execution_step(IzPs2Execution*e,uint32_t budget,IzPs2ExecutionProgress*out){
 if(!e||!out||!budget||budget>64||e->magic!=MAGIC||e->self!=P(e)||P(out)>UINT64_MAX-sizeof *out||overlap(P(e),sizeof *e,P(out),sizeof *out))return 0xf402;
 uint32_t rc=lock(e);if(rc)return rc;while(e->state!=IZ_PS2_EXECUTION_CLOSED&&e->state!=IZ_PS2_EXECUTION_RETAINED&&budget--){rc=advance(e);e->units++;if(rc)break;}
 *out=(IzPs2ExecutionProgress){1,sizeof *out,e->state,e->phase,e->error,e->joined,e->proof_valid,e->stop_requested,e->units,e->worker_ticket,e->presentation_ticket};rc=e->error;unlock(e);return rc;
}
uint32_t iz_ps2_execution_stop(IzPs2Execution*e){if(!e||e->magic!=MAGIC||e->self!=P(e))return 0xf402;uint32_t rc=lock(e);if(rc)return rc;if(e->state==IZ_PS2_EXECUTION_RETAINED)rc=e->error;else e->stop_requested=1;unlock(e);return rc;}
