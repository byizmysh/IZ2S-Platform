#include "video.h"
#include "execution-internal.h"
extern void*memset(void*,int,size_t);
extern void*memcpy(void*,const void*,size_t);
#define P(p) ((uint64_t)(uintptr_t)(p))
#define W(p,o,v) (*(uint32_t*)((uint8_t*)(p)+(o))=(uint32_t)(v))
#define Q(p,o,v) (*(uint64_t*)((uint8_t*)(p)+(o))=(uint64_t)(v))
enum {BUFFER=1,REQUIREMENTS,ALLOCATE,BIND,MAP,QUERY,CLEAR,SET_LAYOUT,LAYOUT,POOL,SET,UPDATE,VERTEX,FRAGMENT,PIPELINE,SEMAPHORE,COMMAND,RECORD,CLEAN_IDLE,CLEAN_SLOT};
static void st(void*p,size_t n,uint32_t type){memset(p,0,n);W(p,0,type);}
static int range(uint64_t p,uint64_t n){return p&&!(p&4095)&&n&&!(n&4095)&&n<=UINT64_MAX-p;}
static int overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m){return a<b+m&&b<a+n;}
static uint32_t get32(const uint8_t*b){uint32_t v;memcpy(&v,b,4);return v;}
static uint64_t get64(const uint8_t*b){uint64_t v;memcpy(&v,b,8);return v;}
static uint32_t lock(IzPs2Video*s){uint32_t n=0;return __atomic_compare_exchange_n(&s->busy,&n,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)?0:0xf319;}
static void unlock(IzPs2Video*s){__atomic_store_n(&s->busy,0,__ATOMIC_RELEASE);}
static uint32_t retain(IzPs2Video*s,uint32_t e){s->state=IZ_PS2_VIDEO_RETAINED;s->pending=1;s->error=e?e:0xf301;return s->error;}
static void closing(IzPs2Video*s,uint32_t e){s->state=IZ_PS2_VIDEO_CLOSING;s->error=e;s->phase=CLEAN_IDLE;s->slot=1;s->closing_stage=0;s->index=0;}
static uint32_t call(IzPs2Video*s,uint32_t f,const uint64_t args[8],uint64_t*result){s->pending=1;s->calls++;*result=0;uint32_t rc=s->ops.call(s->ops.owner,f,args,result);if(rc)return retain(s,0xf302);s->pending=0;return 0;}
#define CALL(f,a,b,c,d,e,g,h,i) call(s,f,(uint64_t[8]){(a),(b),(c),(d),(e),(g),(h),(i)},&result)
static int known_oom(uint32_t r){return r==(uint32_t)-1||r==(uint32_t)-2;}
static uint32_t created(IzPs2Video*s,uint32_t result,uint64_t value){if(result){if(known_oom(result)&&!value){closing(s,result);return 0;}return retain(s,0xf303);}return value?0:retain(s,0xf303);}
static uint32_t observed(IzPs2Video*s,uint32_t result){return result?retain(s,0xf304):0;}
static uint32_t select_memory(IzPs2Video*s,IzPs2VideoSlot*v){
 const uint8_t*p=s->graphics.memory_properties;uint32_t count=get32(p),heaps=get32(p+260);if(!count||count>32||!heaps||heaps>16||v->requirements[0]!=IZ_PS2_VIDEO_BYTES||!v->requirements[1]||(v->requirements[1]&(v->requirements[1]-1))||v->requirements[1]>4096)return 0xf305;
 for(unsigned i=0;i<count;i++){uint32_t flags=get32(p+4+i*8),heap=get32(p+8+i*8);if(heap>=heaps)return 0xf305;if((v->requirements[2]&(UINT64_C(1)<<i))&&(flags&6)==6&&get64(p+264+heap*16)>=IZ_PS2_VIDEO_BYTES){v->memory_type=i;v->source.memory_flags=flags;return 0;}}return 0xf305;
}
static uint32_t mapping(IzPs2Video*s){
 IzPs2MemoryRegion r={0};s->queries++;if(++s->regions>2048||s->ops.query(s->ops.owner,s->walk,&r)||!range(r.base,r.bytes)||r.base>s->walk||r.base+r.bytes<=s->walk||r.type!=5||r.permission!=3||r.attributes!=0x60||r.ipc_refs||r.device_refs)return retain(s,0xf306);uint64_t end=r.base+r.bytes;s->walk=end<s->end?end:s->end;return 0;
}
static void walk(IzPs2Video*s){s->walk=s->slots[s->slot].source.mapped;s->end=s->walk+IZ_PS2_VIDEO_BYTES;s->regions=0;}
static uint32_t source_unique(IzPs2Video*s){
 IzPs2VideoSource*a=&s->slots[0].source,*b=&s->slots[1].source;
 if(s->slot&&((b->buffer&&a->buffer==b->buffer)||(b->memory&&a->memory==b->memory)||(b->set_layout&&a->set_layout==b->set_layout)||(b->layout&&a->layout==b->layout)||(b->pool&&a->pool==b->pool)||(b->descriptor&&a->descriptor==b->descriptor)))return retain(s,0xf307);return 0;
}
static uint32_t object_created(IzPs2Video*s,uint32_t result,uint64_t value){uint32_t rc=created(s,result,value);return rc||s->state!=IZ_PS2_VIDEO_PREPARING?rc:source_unique(s);}
uint32_t iz_ps2_video_begin(IzPs2Video*s,const IzPs2VideoOps*ops,const IzNativeEnvironment*e,const IzNativeGraphics*g,const IzPs2VideoPlan*p){
 if(!s||!ops||!e||!g||!p||!ops->identity||!ops->query||!ops->call||e->version!=1||e->bytes!=sizeof *e||!e->application_pid||e->application_pid>=(UINT64_C(1)<<32)||g->version!=1||g->bytes!=sizeof *g||g->function_count!=IZ_GFX_COUNT||!g->image_count||g->image_count>8||!g->device||!g->queue||!g->swapchain||!g->command_pool||!p->vertex||!p->fragment||p->vertex_bytes<20||p->fragment_bytes<20||p->vertex_bytes%4||p->fragment_bytes%4||p->vertex_bytes>65536||p->fragment_bytes>65536||p->protected_count>24||p->reserved)return 0xf308;
 if(get32(p->vertex)!=0x07230203||get32(p->fragment)!=0x07230203)return 0xf308;
 for(unsigned i=0;i<g->image_count;i++){if(!g->images[i]||!g->views[i])return 0xf308;for(unsigned j=0;j<i;j++)if(g->images[i]==g->images[j]||g->views[i]==g->views[j])return 0xf308;}
 for(unsigned i=0;i<p->protected_count;i++){if(!range(p->protected[i].base,p->protected[i].bytes))return 0xf308;for(unsigned j=0;j<i;j++)if(overlap(p->protected[i].base,p->protected[i].bytes,p->protected[j].base,p->protected[j].bytes))return 0xf308;}
 uint32_t rc=lock(s);if(rc)return rc;
 /* A retained/entered or nonempty owner is never cleared by a new begin. */
 int nonempty=s->pid||s->phase;const uint8_t*owned=(const void*)s->slots;for(size_t i=0;i<sizeof s->slots;i++)nonempty|=owned[i];
 if(s->pending||s->entered||s->state!=IZ_PS2_VIDEO_IDLE||nonempty){unlock(s);return 0xf319;}
 s->ops=*ops;s->graphics=*g;s->plan=*p;s->pid=e->application_pid;s->state=IZ_PS2_VIDEO_PREPARING;s->phase=BUFFER;unlock(s);return 0;
}
static uint32_t pipeline(IzPs2Video*s,IzPs2VideoSlot*v){
 _Alignas(8) uint8_t info[144],stages[96],vertex[48],assembly[32],view[48],raster[64],sample[48],blend[56],attachment[32],format_info[40];const char name[]="main";uint32_t format=37,viewport[]={0,0,0x44f00000,0x44870000,0,0x3f800000},scissor[]={0,0,1920,1080};uint64_t result;
 for(unsigned i=0;i<2;i++){st(stages+i*48,48,18);W(stages,i*48+20,i?16:1);Q(stages,i*48+24,v->shaders[i]);Q(stages,i*48+32,P(name));}
 st(vertex,sizeof vertex,19);st(assembly,sizeof assembly,20);W(assembly,20,3);st(view,sizeof view,22);W(view,20,1);W(view,32,1);Q(view,24,P(viewport));Q(view,40,P(scissor));st(raster,sizeof raster,23);W(raster,56,0x3f800000);st(sample,sizeof sample,24);W(sample,20,1);memset(attachment,0,sizeof attachment);W(attachment,28,15);st(blend,sizeof blend,26);W(blend,28,1);Q(blend,32,P(attachment));st(format_info,sizeof format_info,1000044002);W(format_info,20,1);Q(format_info,24,P(&format));
 st(info,sizeof info,28);Q(info,8,P(format_info));W(info,20,2);Q(info,24,P(stages));Q(info,32,P(vertex));Q(info,40,P(assembly));Q(info,56,P(view));Q(info,64,P(raster));Q(info,72,P(sample));Q(info,88,P(blend));Q(info,104,v->source.layout);W(info,136,0xffffffff);
 uint32_t rc=CALL(IZ_GFX_CREATE_GRAPHICS,s->graphics.device,0,1,P(info),0,P(&v->pipeline),0,0);return rc?rc:created(s,(uint32_t)result,v->pipeline);
}
static uint32_t image_barrier(IzPs2Video*s,uint64_t command,uint32_t old,uint32_t next){
 _Alignas(8) uint8_t b[96],dep[64];uint64_t result;st(b,96,1000314002);Q(b,16,old?0x400:0);Q(b,24,old?0x100:0);Q(b,32,next==2?0x400:0);Q(b,40,next==2?0x100:0);W(b,48,old);W(b,52,next);W(b,56,0xffffffff);W(b,60,0xffffffff);Q(b,64,s->graphics.images[s->index]);W(b,72,1);W(b,80,1);W(b,88,1);st(dep,64,1000314003);W(dep,48,1);Q(dep,56,P(b));return CALL(IZ_GFX_BARRIER,command,P(dep),0,0,0,0,0,0);
}
static uint32_t record(IzPs2Video*s,IzPs2VideoSlot*v){
 _Alignas(8) uint8_t b[72],a[72],barrier[48];uint64_t command=v->source.commands[s->index],result;uint32_t rc;
 switch(s->record_step){
 case 0:st(b,32,42);rc=CALL(IZ_GFX_BEGIN_COMMAND,command,P(b),0,0,0,0,0,0);if(!rc)rc=observed(s,(uint32_t)result);break;
 case 1:st(barrier,48,1000314000);Q(barrier,16,0x4000);Q(barrier,24,0x4000);Q(barrier,32,0x80);Q(barrier,40,0x20);st(b,64,1000314003);W(b,20,1);Q(b,24,P(barrier));rc=CALL(IZ_GFX_BARRIER,command,P(b),0,0,0,0,0,0);break;
 case 2:rc=image_barrier(s,command,0,2);break;
 case 3:st(b,64,1000545003);W(b,16,49);Q(b,24,v->source.layout);W(b,36,1);Q(b,40,P(&v->source.descriptor));rc=CALL(IZ_GFX_BIND_SET,command,P(b),0,0,0,0,0,0);break;
 case 4:st(a,72,1000044001);Q(a,16,s->graphics.views[s->index]);W(a,24,2);W(a,44,1);W(a,64,0x3f800000);st(b,72,1000044000);W(b,28,1920);W(b,32,1080);W(b,36,1);W(b,44,1);Q(b,48,P(a));rc=CALL(IZ_GFX_BEGIN_RENDER,command,P(b),0,0,0,0,0,0);break;
 case 5:rc=CALL(IZ_GFX_BIND_PIPELINE,command,0,v->pipeline,0,0,0,0,0);break;
 case 6:rc=CALL(IZ_GFX_DRAW,command,3,1,0,0,0,0,0);break;
 case 7:rc=CALL(IZ_GFX_END_RENDER,command,0,0,0,0,0,0,0);break;
 case 8:rc=image_barrier(s,command,2,1000001002);break;
 case 9:rc=CALL(IZ_GFX_END_COMMAND,command,0,0,0,0,0,0,0);if(!rc)rc=observed(s,(uint32_t)result);break;
 default:return retain(s,0xf30a);
 }if(rc)return rc;if(++s->record_step==10){s->record_step=0;if(++s->index==s->graphics.image_count){s->index=0;if(++s->slot==2)s->state=IZ_PS2_VIDEO_READY;else s->phase=BUFFER;}else s->phase=COMMAND;}return 0;
}
static uint32_t prepare(IzPs2Video*s){
 IzPs2VideoSlot*v=&s->slots[s->slot];IzPs2VideoSource*r=&v->source;_Alignas(8) uint8_t b[72],bindings[24];uint64_t result;uint32_t rc=0;uint64_t device=s->graphics.device;
 switch(s->phase){
 case BUFFER:st(b,56,12);Q(b,24,IZ_PS2_VIDEO_BYTES);W(b,32,0x20);rc=CALL(IZ_GFX_CREATE_BUFFER,device,P(b),0,P(&r->buffer),0,0,0,0);if(!rc)rc=object_created(s,(uint32_t)result,r->buffer);if(!rc&&s->state==IZ_PS2_VIDEO_PREPARING)s->phase=REQUIREMENTS;break;
 case REQUIREMENTS:rc=CALL(IZ_GFX_BUFFER_REQUIREMENTS,device,r->buffer,P(v->requirements),0,0,0,0,0);if(!rc){rc=select_memory(s,v);if(rc){closing(s,rc);rc=0;}else s->phase=ALLOCATE;}break;
 case ALLOCATE:st(b,32,5);Q(b,16,IZ_PS2_VIDEO_BYTES);W(b,24,v->memory_type);rc=CALL(IZ_GFX_ALLOCATE_MEMORY,device,P(b),0,P(&r->memory),0,0,0,0);if(!rc)rc=object_created(s,(uint32_t)result,r->memory);if(!rc&&s->state==IZ_PS2_VIDEO_PREPARING)s->phase=BIND;break;
 case BIND:rc=CALL(IZ_GFX_BIND_BUFFER,device,r->buffer,r->memory,0,0,0,0,0);if(!rc)rc=observed(s,(uint32_t)result);if(!rc)s->phase=MAP;break;
 case MAP:st(b,48,1000271000);Q(b,24,r->memory);Q(b,40,IZ_PS2_VIDEO_BYTES);rc=CALL(IZ_GFX_MAP_MEMORY,device,P(b),P(&r->mapped),0,0,0,0,0);if(rc)break;
  if((uint32_t)result){if((known_oom((uint32_t)result)||(uint32_t)result==(uint32_t)-5)&&!r->mapped){closing(s,(uint32_t)result);break;}return retain(s,0xf30b);}if(!range(r->mapped,IZ_PS2_VIDEO_BYTES))return retain(s,0xf30b);v->mapped_known=1;r->mapped_bytes=IZ_PS2_VIDEO_BYTES;
  if(overlap(r->mapped,IZ_PS2_VIDEO_BYTES,P(s),sizeof *s)||overlap(r->mapped,IZ_PS2_VIDEO_BYTES,P(s->plan.vertex),s->plan.vertex_bytes)||overlap(r->mapped,IZ_PS2_VIDEO_BYTES,P(s->plan.fragment),s->plan.fragment_bytes))return retain(s,0xf30c);
  for(unsigned i=0;i<s->plan.protected_count;i++)if(overlap(r->mapped,IZ_PS2_VIDEO_BYTES,s->plan.protected[i].base,s->plan.protected[i].bytes))return retain(s,0xf30c);
  if(s->slot&&overlap(r->mapped,IZ_PS2_VIDEO_BYTES,s->slots[0].source.mapped,IZ_PS2_VIDEO_BYTES))return retain(s,0xf30c);walk(s);s->phase=QUERY;break;
 case QUERY:rc=mapping(s);if(!rc&&s->walk==s->end){s->cleared=0;s->phase=CLEAR;}break;
 case CLEAR:{uint64_t n=IZ_PS2_VIDEO_BYTES-s->cleared;if(n>65536)n=65536;memset((void*)(uintptr_t)(r->mapped+s->cleared),0,(size_t)n);s->cleared+=n;if(s->cleared==IZ_PS2_VIDEO_BYTES)s->phase=SET_LAYOUT;break;}
 case SET_LAYOUT:memset(bindings,0,sizeof bindings);W(bindings,0,1);W(bindings,4,7);W(bindings,8,1);W(bindings,12,16);st(b,32,32);W(b,20,1);Q(b,24,P(bindings));rc=CALL(IZ_GFX_CREATE_SET_LAYOUT,device,P(b),0,P(&r->set_layout),0,0,0,0);if(!rc)rc=object_created(s,(uint32_t)result,r->set_layout);if(!rc&&s->state==IZ_PS2_VIDEO_PREPARING)s->phase=LAYOUT;break;
 case LAYOUT:st(b,48,30);W(b,20,1);Q(b,24,P(&r->set_layout));rc=CALL(IZ_GFX_CREATE_LAYOUT,device,P(b),0,P(&r->layout),0,0,0,0);if(!rc)rc=object_created(s,(uint32_t)result,r->layout);if(!rc&&s->state==IZ_PS2_VIDEO_PREPARING)s->phase=POOL;break;
 case POOL:{uint32_t size[]={7,1};st(b,40,33);W(b,20,1);W(b,24,1);Q(b,32,P(size));rc=CALL(IZ_GFX_CREATE_POOL,device,P(b),0,P(&r->pool),0,0,0,0);if(!rc)rc=object_created(s,(uint32_t)result,r->pool);if(!rc&&s->state==IZ_PS2_VIDEO_PREPARING)s->phase=SET;break;}
 case SET:st(b,40,34);Q(b,16,r->pool);W(b,24,1);Q(b,32,P(&r->set_layout));rc=CALL(IZ_GFX_ALLOCATE_SET,device,P(b),P(&r->descriptor),0,0,0,0,0);if(!rc)rc=object_created(s,(uint32_t)result,r->descriptor);if(!rc&&s->state==IZ_PS2_VIDEO_PREPARING)s->phase=UPDATE;break;
 case UPDATE:{uint64_t info[]={r->buffer,0,IZ_PS2_VIDEO_BYTES};st(b,64,35);Q(b,16,r->descriptor);W(b,24,1);W(b,32,1);W(b,36,7);Q(b,48,P(info));rc=CALL(IZ_GFX_UPDATE_SET,device,1,P(b),0,0,0,0,0);if(!rc)s->phase=VERTEX;break;}
 case VERTEX:case FRAGMENT:{unsigned k=s->phase==FRAGMENT;st(b,40,16);Q(b,24,k?s->plan.fragment_bytes:s->plan.vertex_bytes);Q(b,32,P(k?s->plan.fragment:s->plan.vertex));rc=CALL(IZ_GFX_CREATE_SHADER,device,P(b),0,P(&v->shaders[k]),0,0,0,0);if(!rc)rc=created(s,(uint32_t)result,v->shaders[k]);if(!rc&&s->state==IZ_PS2_VIDEO_PREPARING)s->phase=k?PIPELINE:FRAGMENT;break;}
 case PIPELINE:rc=pipeline(s,v);if(!rc&&s->state==IZ_PS2_VIDEO_PREPARING){s->index=0;s->phase=SEMAPHORE;}break;
 case SEMAPHORE:st(b,24,9);rc=CALL(IZ_PS2_VIDEO_CREATE_SEMAPHORE,device,P(b),0,P(&r->semaphores[s->index]),0,0,0,0);if(!rc)rc=created(s,(uint32_t)result,r->semaphores[s->index]);if(rc||s->state!=IZ_PS2_VIDEO_PREPARING)break;
  for(unsigned j=0;j<=s->slot;j++)for(unsigned k=0;k<(j==s->slot?s->index:s->graphics.image_count+1);k++)if(s->slots[j].source.semaphores[k]==r->semaphores[s->index])return retain(s,0xf30d);
  if(++s->index==s->graphics.image_count+1){s->index=0;s->phase=COMMAND;}break;
 case COMMAND:st(b,32,40);Q(b,16,s->graphics.command_pool);W(b,28,1);rc=CALL(IZ_GFX_ALLOCATE_COMMANDS,device,P(b),P(&r->commands[s->index]),0,0,0,0,0);if(!rc)rc=created(s,(uint32_t)result,r->commands[s->index]);if(rc||s->state!=IZ_PS2_VIDEO_PREPARING)break;
  for(unsigned j=0;j<=s->slot;j++)for(unsigned k=0;k<(j==s->slot?s->index:s->graphics.image_count);k++)if(s->slots[j].source.commands[k]==r->commands[s->index])return retain(s,0xf30d);s->record_step=0;s->phase=RECORD;break;
 case RECORD:rc=record(s,v);break;
 default:return retain(s,0xf30a);
 }return rc;
}
static uint32_t clean(IzPs2Video*s){
 IzPs2VideoSlot*v=&s->slots[s->slot];IzPs2VideoSource*r=&v->source;uint64_t result,device=s->graphics.device;uint32_t rc=0;
 if(s->phase==CLEAN_IDLE){int any=0;for(unsigned j=0;j<2;j++)for(unsigned i=0;i<8;i++)if(s->slots[j].source.commands[i])any=1;if(any){s->queue_waits++;rc=CALL(IZ_GFX_QUEUE_IDLE,s->graphics.queue,0,0,0,0,0,0,0);if(!rc)rc=observed(s,(uint32_t)result);if(rc)return rc;}s->phase=CLEAN_SLOT;return 0;}
 switch(s->closing_stage){
 case 0:while(s->index<9&&!r->semaphores[8-s->index])s->index++;if(s->index<9){unsigned i=8-s->index;rc=CALL(IZ_PS2_VIDEO_DESTROY_SEMAPHORE,device,r->semaphores[i],0,0,0,0,0,0);if(!rc)r->semaphores[i]=0;s->index++;}else{s->index=0;s->closing_stage++;}break;
 case 1:while(s->index<8&&!r->commands[7-s->index])s->index++;if(s->index<8){unsigned i=7-s->index;rc=CALL(IZ_GFX_FREE_COMMANDS,device,s->graphics.command_pool,1,P(&r->commands[i]),0,0,0,0);if(!rc)r->commands[i]=0;s->index++;}else{s->index=0;s->closing_stage++;}break;
 case 2:if(v->pipeline){rc=CALL(IZ_GFX_DESTROY_PIPELINE,device,v->pipeline,0,0,0,0,0,0);if(!rc)v->pipeline=0;}else s->closing_stage++;break;
 case 3:while(s->index<2&&!v->shaders[1-s->index])s->index++;if(s->index<2){unsigned i=1-s->index;rc=CALL(IZ_GFX_DESTROY_SHADER,device,v->shaders[i],0,0,0,0,0,0);if(!rc)v->shaders[i]=0;s->index++;}else{s->index=0;s->closing_stage++;}break;
 case 4:if(r->pool){rc=CALL(IZ_GFX_DESTROY_POOL,device,r->pool,0,0,0,0,0,0);if(!rc)r->pool=r->descriptor=0;}else s->closing_stage++;break;
 case 5:if(r->layout){rc=CALL(IZ_GFX_DESTROY_LAYOUT,device,r->layout,0,0,0,0,0,0);if(!rc)r->layout=0;}else s->closing_stage++;break;
 case 6:if(r->set_layout){rc=CALL(IZ_GFX_DESTROY_SET_LAYOUT,device,r->set_layout,0,0,0,0,0,0);if(!rc)r->set_layout=0;}else{if(v->mapped_known)walk(s);s->closing_stage++;}break;
 case 7:if(v->mapped_known&&s->walk<s->end)rc=mapping(s);else s->closing_stage++;break;
 case 8:if(v->mapped_known){rc=CALL(IZ_GFX_UNMAP_MEMORY,device,r->memory,0,0,0,0,0,0);if(!rc){v->mapped_known=0;r->mapped=r->mapped_bytes=0;}}else s->closing_stage++;break;
 case 9:if(r->buffer){rc=CALL(IZ_GFX_DESTROY_BUFFER,device,r->buffer,0,0,0,0,0,0);if(!rc)r->buffer=0;}else s->closing_stage++;break;
 case 10:if(r->memory){rc=CALL(IZ_GFX_FREE_MEMORY,device,r->memory,0,0,0,0,0,0);if(!rc)r->memory=0;}else s->closing_stage++;break;
 case 11:if(s->slot){s->slot=0;s->index=s->closing_stage=0;}else s->state=s->error?IZ_PS2_VIDEO_FAILED:IZ_PS2_VIDEO_CLOSED;break;
 default:return retain(s,0xf30a);
 }return rc;
}
uint32_t iz_ps2_video_step(IzPs2Video*s,uint32_t budget,uint32_t*done){
 if(!s||!done||!budget||budget>64)return 0xf308;*done=0;uint32_t rc=lock(s);if(rc)return rc;
 if(s->state==IZ_PS2_VIDEO_IDLE||s->state==IZ_PS2_VIDEO_ENTERED){*done=1;unlock(s);return 0xf308;}
 while((s->state==IZ_PS2_VIDEO_PREPARING||s->state==IZ_PS2_VIDEO_CLOSING)&&budget--){uint64_t pid=0;if(s->ops.identity(s->ops.owner,&pid)||pid!=s->pid){rc=retain(s,0xf30e);break;}rc=s->state==IZ_PS2_VIDEO_PREPARING?prepare(s):clean(s);s->units++;if(rc)break;}
 if(s->state!=IZ_PS2_VIDEO_PREPARING&&s->state!=IZ_PS2_VIDEO_CLOSING){*done=1;rc=s->error;}unlock(s);return rc;
}
uint32_t iz_ps2_video_cancel(IzPs2Video*s,uint32_t reason){
 if(!s)return 0xf308;uint32_t rc=lock(s);if(rc)return rc;if(s->pending||s->entered||s->state==IZ_PS2_VIDEO_RETAINED)rc=retain(s,s->error?s->error:0xf310);else if(s->state==IZ_PS2_VIDEO_PREPARING||s->state==IZ_PS2_VIDEO_READY)closing(s,reason);unlock(s);return rc;
}
uint32_t iz_ps2_video_entered(IzPs2Video*s){if(!s)return 0xf308;uint32_t rc=lock(s);if(rc)return rc;if(s->state!=IZ_PS2_VIDEO_READY||s->pending||s->entered)rc=0xf311;else{s->entered=1;s->state=IZ_PS2_VIDEO_ENTERED;}unlock(s);return rc;}
#ifdef IZ_PS2_EXECUTION
uint32_t iz_ps2_video_retire(IzPs2Video*s,const IzPs2Execution*e){if(!s)return 0xf308;uint32_t rc=lock(s);if(rc)return rc;
 if(s->state!=IZ_PS2_VIDEO_ENTERED||!s->entered||s->pending||!iz_ps2_execution_release_match(e,s,IZ_PS2_RETIRE_VIDEO))rc=0xf312;else closing(s,0);unlock(s);return rc;
}
#endif
uint32_t iz_ps2_video_pack_av(IzPs2Video*s,const IzPs2AvInput*in,uint8_t out[4096]){
 if(!s||!in||!out||P(out)>UINT64_MAX-4096||P(in)>UINT64_MAX-sizeof *in||P(s)>UINT64_MAX-sizeof *s||overlap(P(out),4096,P(s),sizeof *s)||overlap(P(out),4096,P(in),sizeof *in))return 0xf308;uint32_t rc=lock(s);if(rc)return rc;
 if(s->state!=IZ_PS2_VIDEO_READY||s->pending||s->entered||in->image_count!=s->graphics.image_count||in->vulkan!=s->graphics.vulkan||in->device!=s->graphics.device||in->queue!=s->graphics.queue||in->swapchain!=s->graphics.swapchain)rc=0xf311;
 else{IzPs2AvInput value=*in;value.sources[0]=s->slots[0].source;value.sources[1]=s->slots[1].source;rc=iz_ps2_pack_av(&value,out);}unlock(s);return rc;
}
