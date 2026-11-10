/* SD -> owned RW source -> RO RX alias -> native entry. No host callback. */
#include "abi.h"
#include "../relocatable/loader.h"
#include "../instructions/checkpoint.h"
_Static_assert(sizeof(IzImage)<=4096,"Engine ownership state fits one page");
_Static_assert(IZ_GFX_COUNT==F_COUNT-F_CREATE_SHADER,"Public graphics function table order");
#define PTR(p) ((u64)(uintptr_t)(p))
static u32 retain(IzNative*n,u32 rc){n->unknown=1;n->fs->c->poisoned=1;n->fs->c->native_status=rc;return rc;}
static u32 ro(IzNative*n,u32 command,const u64 values[4],u64*out,unsigned count){
 IzContext*c=n->fs->c;_Alignas(4096) u8 message[4096];memset(message,0,sizeof message);u32*p=(void*)message;
 p[0]=4;p[1]=0x80000010u;p[2]=3;p[5]=0xffff8001u;p[8]=0x49434653;p[10]=command;memcpy(message+48,values,32);
 n->unknown=1;u32 rc=(u32)CALL(c,F_SEND,message,4096,c->ro_handle,0,0,0,0,0);if(rc)return retain(n,rc);
 if(!(p[1]&0x80000000u)||p[2]!=1||p[8]!=0x4f434653||((u64)p[3]|((u64)p[4]<<32))!=c->ro_pid||(p[1]&1023)<8+count*2)return retain(n,0xe401);
 if(p[10])return retain(n,p[10]);if(out)memcpy(out,message+48,count*8);n->unknown=0;return 0;
}
static u32 allocate(IzNative*n,u64 bytes,u64*out){
 *out=0;IzContext*c=n->fs->c;u32 rc=(u32)CALL(c,F_ALLOCATE,out,4096,bytes,0,0,0,0,0);if(rc)return *out?retain(n,0xe402):rc;
 if(!*out||*out%4096||*out>=(1ull<<39)||bytes>(1ull<<39)-*out)return retain(n,0xe402);memset((void*)(uintptr_t)*out,0,bytes);return 0;
}
static int path(IzNative*n,const char*name,char*out){
 if(!name||!name[0]||name[0]=='/')return 0;size_t k=0;while(k<512&&name[k])k++;if(k>=512)return 0;
 for(size_t i=0;i<k;){size_t start=i;while(i<k&&name[i]!='/'){unsigned ch=(u8)name[i++];if(ch<32||ch==127||ch=='\\'||ch==':')return 0;}size_t length=i-start;if(!length||(length==1&&name[start]=='.')||(length==2&&name[start]=='.'&&name[start+1]=='.'))return 0;i++;}
 size_t base=strlen(n->root);if(base+1+k>=769)return 0;memcpy(out,n->root,base);out[base++]='/';memcpy(out+base,name,k+1);return 1;
}
static u32 read_file(IzNativeApp*app,const char*name,u64 offset,void*out,u32 bytes,u32*actual){
 if(actual)*actual=0;if(!app||!actual||(!out&&bytes))return 0xe403;IzNative*n=app->owner;char full[769];if(!path(n,name,full))return 0xe403;u32 file=0;u64 size=0;u32 rc=iz_fs_open(n->fs,full,&file,&size);if(rc)return rc;
 if(offset>size)rc=0xe403;else{u64 count=size-offset;if(count>bytes)count=bytes;rc=iz_fs_read(n->fs,file,offset,out,(u32)count);if(!rc)*actual=(u32)count;}
 if(!n->fs->c->poisoned){u32 close=iz_fs_close(n->fs,file);if(!rc)rc=close;}return rc;
}
static u32 write_file(IzNativeApp*app,const char*name,const void*data,u32 bytes){if(!app||!data||!bytes||bytes>16u*1024*1024)return 0xe403;IzNative*n=app->owner;char full[769];if(!path(n,name,full))return 0xe403;return iz_fs_write_file(n->fs,full,data,bytes);}
static void request_exit(IzNativeApp*app){if(app&&app->controls)*(u32*)((u8*)app->controls+1016)=1;}
static u64 buttons(IzNativeApp*app){return app&&app->controls?*(u64*)((u8*)app->controls+8):0;}
static IzNative*image_owner(IzNativeApp*app){
 if(!app||!app->owner||!((app->version==3&&app->bytes==sizeof(IzNativeApp3))||(app->version==4&&app->bytes==sizeof(IzNativeApp4))||(app->version==5&&app->bytes==sizeof(IzNativeApp5))||(app->version==6&&app->bytes==sizeof(IzNativeApp6))))return 0;IzNative*n=app->owner;
 if(app!=&n->app||!n->initialized||!n->event_active||n->unknown||n->fs->c->poisoned)return 0;return n;
}
static u32 image_lock(IzNative*n){u32 expected=0;return __atomic_compare_exchange_n(&n->image_busy,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)?0:0xe619;}
static u32 load_image(IzNativeApp*app,const char*name,IzNativeImage*out){
 if(!out)return 0xe40b;IzNative*n=image_owner(app);char full[769];if(!n||!path(n,name,full))return 0xe40b;
 u32 rc=image_lock(n);if(rc)return rc;
 if(n->worker_state||n->image_token||n->image_generation==0xffffffffu||n->fs->c->native_launches>0xffffffffu){rc=0xe619;goto done;}
 memset(out,0,sizeof *out);
 if(!n->image_state){rc=allocate(n,4096,&n->image_state);if(rc)goto done;}
 IzImage*image=(void*)(uintptr_t)n->image_state;rc=iz_image_load(n->fs,image,full,0,0,0);if(rc)goto done;
 n->image_token=(n->fs->c->native_launches<<32)|++n->image_generation;
 out->version=1;out->bytes=sizeof *out;out->code=(void*)(uintptr_t)image->destination;out->code_bytes=image->header.memory_bytes[0];out->data=(void*)(uintptr_t)image->data;out->data_bytes=image->header.memory_bytes[1];out->entry=(void*)(uintptr_t)image->entry;out->spawn=(void*)(uintptr_t)image->spawn;out->token=n->image_token;
done:__atomic_store_n(&n->image_busy,0,__ATOMIC_RELEASE);return rc;
}
static u32 unload_image(IzNativeApp*app,u64 token){
 IzNative*n=image_owner(app);if(!n||!token||!n->image_state||n->image_async||n->worker_state||n->presentation_owned||token!=n->image_token)return 0xe40d;
 u32 rc=image_lock(n);if(rc)return rc;rc=iz_image_unload((void*)(uintptr_t)n->image_state);if(!rc)n->image_token=0;__atomic_store_n(&n->image_busy,0,__ATOMIC_RELEASE);return rc;
}
static void image_receipt(IzNative*n,IzNativeImage*out){
 IzImage*image=(void*)(uintptr_t)n->image_state;memset(out,0,sizeof *out);if(!image->ready)return;
 out->version=1;out->bytes=sizeof *out;out->code=(void*)(uintptr_t)image->destination;out->code_bytes=image->header.memory_bytes[0];out->data=(void*)(uintptr_t)image->data;out->data_bytes=image->header.memory_bytes[1];out->entry=(void*)(uintptr_t)image->entry;out->spawn=(void*)(uintptr_t)image->spawn;out->token=n->image_token;
}
static u32 image_begin_async(IzNativeApp*app,const char*name,u64*ticket){
 IzNative*n=image_owner(app);char full[769];if(!n||n->stopping||(app->version<4||app->version>6)||!ticket||!path(n,name,full))return 0xe40b;u32 rc=image_lock(n);if(rc)return rc;
 if(n->worker_state||n->image_token||n->image_generation==0xffffffffu||n->fs->c->native_launches>0xffffffffu){rc=0xe619;goto done;}
 if(!n->image_state){rc=allocate(n,4096,&n->image_state);if(rc)goto done;}rc=iz_image_begin(n->fs,(void*)(uintptr_t)n->image_state,full,0);if(rc)goto done;
 n->image_token=(n->fs->c->native_launches<<32)|++n->image_generation;n->image_async=1;*ticket=n->image_token;
 done:__atomic_store_n(&n->image_busy,0,__ATOMIC_RELEASE);return rc;
}
static u32 image_step_async(IzNativeApp*app,u64 ticket,u32 budget,IzNativeImageProgress*out,IzNativeImage*receipt){
 IzNative*n=image_owner(app);if(!n||(app->version<4||app->version>6)||!out||!receipt||!ticket||!n->image_async||ticket!=n->image_token)return 0xe40d;if(!budget||budget>64)return 0xe618;u32 rc=image_lock(n);if(rc)return rc;
 IzImage*image=(void*)(uintptr_t)n->image_state;u32 done=0;rc=iz_image_step(image,budget,&done);memset(out,0,sizeof *out);out->version=1;out->bytes=sizeof *out;out->stage=image->stage;out->error=image->error;out->done=done;
 out->state=image->pending?IZ_NATIVE_IMAGE_RETAINED:image->operation==1?IZ_NATIVE_IMAGE_LOADING:image->operation==2?IZ_NATIVE_IMAGE_CLOSING:image->ready?IZ_NATIVE_IMAGE_READY:image->error?IZ_NATIVE_IMAGE_FAILED:IZ_NATIVE_IMAGE_CLOSED;
 out->read_bytes=image->read_bytes;out->total_bytes=sizeof(IzRelocHeader)+(u64)image->header.file_bytes[0]+image->header.file_bytes[1]+2*(u64)image->header.record_count*sizeof(IzRelocRecord);out->cleared_bytes=image->cleared_bytes;out->mapped_pages=image->mapped;out->total_pages=image->pages;out->work_units=image->units;image_receipt(n,receipt);
 if(done&&(out->state==IZ_NATIVE_IMAGE_CLOSED||out->state==IZ_NATIVE_IMAGE_FAILED)){n->image_token=0;n->image_async=0;}if(rc)n->fs->c->native_status=rc;__atomic_store_n(&n->image_busy,0,__ATOMIC_RELEASE);return rc;
}
static u32 image_release_async(IzNativeApp*app,u64 ticket){
 IzNative*n=image_owner(app);if(!n||(app->version<4||app->version>6)||!ticket||!n->image_async||ticket!=n->image_token||n->worker_state||n->presentation_owned)return 0xe40d;u32 rc=image_lock(n);if(rc)return rc;rc=iz_image_close_begin((void*)(uintptr_t)n->image_state,0);__atomic_store_n(&n->image_busy,0,__ATOMIC_RELEASE);return rc;
}
/* Workers use only the current admitted engine entry. The supervisor retains
 * code, context and borrowed capabilities until a nonblocking wait and JOIN. */
static u64 worker_call(IzNative*n,unsigned function,u64 a,u64 b,u64 d,u64 e){
 IzFunction f=(IzFunction)(uintptr_t)n->fs->c->worker_functions[function];return f(a,b,d,e,0,0,0,0);
}
static u64 worker_entry(void*opaque){
 IzNative*n=opaque;u64 result=((u64(*)(void*,u64))(uintptr_t)n->worker_entry)((void*)(uintptr_t)n->worker_context,0);
 n->worker_result=result;__atomic_store_n(&n->worker_done,1,__ATOMIC_RELEASE);return 0;
}
static int worker_context(IzNative*n,void*context,u64 bytes){
 u64 p=PTR(context);if(!p||p%8||!bytes||bytes>128u*1024*1024)return 0;
 if(p>=n->data&&p-n->data<n->data_bytes&&bytes<=n->data_bytes-(p-n->data))return 1;
 IzImage*image=(void*)(uintptr_t)n->image_state;return image&&p>=image->data&&p-image->data<image->header.memory_bytes[1]&&bytes<=image->header.memory_bytes[1]-(p-image->data);
}
static u32 worker_begin(IzNativeApp*app,u64 image_token,void*context,u64 bytes,u64*ticket){
 IzNative*n=image_owner(app);if(!n||(app->version<5||app->version>6)||!ticket||!image_token||image_token!=n->image_token||!n->image_state||n->stopping)return 0xe620;
 if(app->version==6&&!n->presentation_owned)return 0xe646;
 if(n->worker_state||n->worker_thread||n->worker_generation==0xffffffffu)return 0xe621;
 IzImage*image=(void*)(uintptr_t)n->image_state;if(!image->ready||image->operation||image->pending||image->busy||!image->entry||!worker_context(n,context,bytes))return 0xe620;
 n->worker_entry=image->entry;n->worker_context=PTR(context);n->worker_context_bytes=bytes;n->worker_result=n->worker_return=0;__atomic_store_n(&n->worker_done,0,__ATOMIC_RELEASE);
 /* create may start the child before returning. Only a known rejection with
  * a canonical absent output is safe to unwind; never retry an uncertain one. */
 n->worker_state=IZ_WORKER_RUNNING;n->worker_thread=0;
 u64 rc=worker_call(n,0,PTR(&n->worker_thread),0,PTR(worker_entry),PTR(n));
 if(rc){if(n->worker_thread)return retain(n,0xe622);n->worker_state=0;n->worker_entry=n->worker_context=n->worker_context_bytes=0;return 0xe623;}
 if(!n->worker_thread)return retain(n,0xe622);
 n->worker_token=(n->fs->c->native_launches<<32)|++n->worker_generation;*ticket=n->worker_token;return 0;
}
static u32 worker_poll(IzNativeApp*app,u64 ticket,IzNativeWorkerProgress*out){
 IzNative*n=image_owner(app);if(!n||(app->version<5||app->version>6)||!out||!ticket||ticket!=n->worker_token||!n->worker_state||!n->worker_thread)return 0xe624;
 if(n->worker_state==IZ_WORKER_RUNNING){u64 rc=worker_call(n,1,n->worker_thread,0,0,0);if(rc>1)return retain(n,0xe625);if(rc){if(!__atomic_load_n(&n->worker_done,__ATOMIC_ACQUIRE))return retain(n,0xe626);n->worker_state=IZ_WORKER_FINISHED;}}
 memset(out,0,sizeof *out);out->version=1;out->bytes=sizeof *out;out->state=n->worker_state;if(n->worker_state==IZ_WORKER_FINISHED)out->result=n->worker_result;return 0;
}
static u32 worker_join(IzNativeApp*app,u64 ticket,u64*result){
 IzNative*n=image_owner(app);if(!n||(app->version<5||app->version>6)||!result||!ticket||ticket!=n->worker_token||!n->worker_state||!n->worker_thread)return 0xe624;
 if(n->worker_state!=IZ_WORKER_FINISHED)return 0xe627;
 if(worker_call(n,2,n->worker_thread,PTR(&n->worker_return),0,0)||n->worker_return)return retain(n,0xe628);
 *result=n->worker_result;n->worker_thread=n->worker_token=n->worker_entry=n->worker_context=n->worker_context_bytes=0;n->worker_state=0;return 0;
}

static u32 presentation_acquire(IzNativeApp*app,u64*ticket){
 IzNative*n=image_owner(app);if(!n||app->version!=6||!ticket||n->stopping||n->presentation_owned||n->worker_state||n->worker_thread||!n->image_state||!n->image_token||n->presentation_generation==0xffffffffu||n->fs->c->native_launches>0x7fffffffu)return 0xe640;
 IzImage*image=(void*)(uintptr_t)n->image_state;if(!image->ready||image->operation||image->pending||image->busy)return 0xe640;
 n->presentation_token=0x8000000000000000ull|(n->fs->c->native_launches<<32)|++n->presentation_generation;n->presentation_owned=1;*ticket=n->presentation_token;return 0;
}
static u32 presentation_release(IzNativeApp*app,u64 ticket){
 IzNative*n=image_owner(app);if(!n||app->version!=6||!ticket||ticket!=n->presentation_token||!n->presentation_owned||n->worker_state||n->worker_thread)return 0xe642;
 IzContext*c=n->fs->c;u32 rc=(u32)CALL(c,F_QUEUE_IDLE,c->queue,0,0,0,0,0,0,0);if(rc)return retain(n,0xe643);
 n->presentation_owned=0;n->presentation_token=0;return 0;
}

u32 iz_native_event(IzNative*n,u32 event){if(!n->destination||n->mapped!=n->pages||n->unknown||n->event_active)return 0xe404;
 n->event_active=event;u32 result=((IzNativeEntry)(uintptr_t)n->destination)(&n->app,event);n->event_active=0;n->app.result=result;if(n->fs->c->poisoned)return retain(n,0xe405);return result;}
u32 iz_native_unload(IzNative*n){
 if(!n->fs)return 0;IzContext*c=n->fs->c;if(n->unknown||c->poisoned)return 0xe406;
 if(n->initialized){n->stopping=1;u32 rc=iz_native_event(n,IZ_NATIVE_STOP);if(rc==IZ_NATIVE_STOP_PENDING&&n->app.version>=4)return rc;if(rc)return retain(n,rc);n->initialized=0;}
 if(n->worker_state||n->worker_thread)return retain(n,0xe62a);
 if(n->presentation_owned||n->presentation_token)return retain(n,0xe645);
 if(n->image_state){IzImage*image=(void*)(uintptr_t)n->image_state;if(n->image_busy||n->image_token||image->busy||image->pending||image->ready||image->source||image->data||image->mapped||image->file||image->operation)return retain(n,0xe40e);CALL(c,F_FREE,n->image_state,0,0,0,0,0,0,0);n->image_state=0;}
 while(n->mapped){u64 page=n->mapped-1,values[]={c->pid,n->source+page*4096,n->destination+page*4096,4096};u32 rc=ro(n,0x524f0013,values,0,0);if(rc)return rc;n->mapped--;}
 if(n->source)CALL(c,F_FREE,n->source,0,0,0,0,0,0,0);if(n->data)CALL(c,F_FREE,n->data,0,0,0,0,0,0,0);c->native_data=0;c->native_data_bytes=0;memset(n,0,sizeof *n);return 0;
}
static u64 ticks(IzNativeApp*app){IzNative*n=app->owner;IzContext*c=n->fs->c;return CALL(c,F_TICK,0,0,0,0,0,0,0,0)-c->checkpoint_time_bias;}
static void sleep_ns(IzNativeApp*app,u64 ns){IzNative*n=app->owner;CALL(n->fs->c,F_SLEEP,ns,0,0,0,0,0,0,0);}
u32 iz_native_load_saved(IzFs*fs,const IzAppHeader*h,const u8*image,IzNative*n,void*private_memory,u64 private_bytes,const void*saved){
 if(n->source||n->data||n->mapped||n->image_state)return 0xe407;memset(n,0,sizeof *n);n->fs=fs;IzContext*c=fs->c;
 if(!c->ro_handle||!c->pid||!c->ro_pid||!(h->flags&IZ_APP_NATIVE))return 0xe407;
 unsigned code=h->version==2?SEC_NATIVE_CODE:SEC_BEGIN,initial=h->version==2?SEC_NATIVE_DATA:SEC_AFTER;
 if(h->flags&IZ_APP_WORKER){if(!c->sdk||!c->pid||!c->ro_pid||!c->ro_handle||!c->filesystem)return 0xe62b;for(unsigned i=0;i<3;i++)if(!c->worker_functions[i])return 0xe62b;}
 if(h->flags&IZ_APP_PRESENTATION){
  if(!c->graphics_vulkan||!c->graphics_instance||!c->graphics_physical||!c->device||!c->queue||!c->swapchain||!c->command_pool||!c->image_count||c->image_count>8)return 0xe644;
  for(unsigned i=F_CREATE_SHADER;i<F_COUNT;i++)if(!c->functions[i])return 0xe644;
  for(unsigned i=0;i<c->image_count;i++)if(!c->images[i]||!c->views[i])return 0xe644;
  u32 types=0,heaps=0;memcpy(&types,c->graphics_memory_properties,4);memcpy(&heaps,c->graphics_memory_properties+260,4);if(!types||types>32||!heaps||heaps>16)return 0xe644;
 }
 if(h->flags&IZ_APP_AUDIO_OUT)for(unsigned i=0;i<IZ_AUDIO_FUNCTIONS;i++)if(!c->audio_functions[i])return 0xe40a;
 n->pages=((u64)h->section[code].bytes+4095)/4096;n->data_bytes=h->reserved[0];
 u32 rc=allocate(n,n->pages*4096,&n->source);if(rc)return rc;rc=allocate(n,n->data_bytes,&n->data);if(rc){iz_native_unload(n);return rc;}
 memcpy((void*)(uintptr_t)n->source,image+h->section[code].offset,h->section[code].bytes);if(h->section[initial].bytes)memcpy((void*)(uintptr_t)n->data,image+h->section[initial].offset,h->section[initial].bytes);
 if(saved){u32*program=(void*)(uintptr_t)n->data;u64 at=(u64)program[3]+IZ_INSTRUCTION_MAILBOX;if(h->version!=2||program[0]!=0x434c5a49u||at>n->data_bytes||sizeof(IzInstructionSaved)>n->data_bytes-at)return 0xe732;memcpy((u8*)(uintptr_t)n->data+at,saved,sizeof(IzInstructionSaved));}
 u64 values[]={c->pid,0,0,0},info[13];rc=ro(n,0x524f0010,values,info,13);if(rc)return rc;n->destination=info[11];
 if(!n->destination||n->destination%4096||n->destination>=(1ull<<39)||n->pages*4096>(1ull<<39)-n->destination)return retain(n,0xe408);
 values[1]=n->destination;rc=ro(n,0x524f0010,values,info,13);if(rc)return rc;
 if(info[3]||info[6]||(u32)info[7]||info[4]>n->destination||info[5]<n->pages*4096||n->destination-info[4]>info[5]-n->pages*4096)return retain(n,0xe409);
 for(;n->mapped<n->pages;n->mapped++){u64 map[]={c->pid,n->source+n->mapped*4096,n->destination+n->mapped*4096,4096};rc=ro(n,0x524f0012,map,0,0);if(rc)return rc;}
 IzNativeApp*app=&n->app;app->magic=0x414e5a49;app->version=(h->flags&IZ_APP_PRESENTATION)?6:(h->flags&IZ_APP_WORKER)?5:(h->flags&IZ_APP_COOPERATIVE)?4:(h->flags&IZ_APP_ENGINE_IMAGE)?3:h->version==2?2:1;app->bytes=app->version==6?sizeof n->app6:app->version==5?sizeof n->app5:app->version==4?sizeof n->app4:app->version==3?sizeof n->app3:app->version==2?sizeof n->app2:sizeof *app;app->flags=(app->version>=3?h->flags:0)|(c->ram_only?IZ_NATIVE_SOURCE_RAM:0);app->data=(void*)(uintptr_t)n->data;app->data_bytes=n->data_bytes;app->controls=(void*)(uintptr_t)c->controls;app->controls_bytes=c->controls_bytes;app->owner=n;app->read_file=read_file;app->write_file=write_file;app->request_exit=request_exit;app->buttons=buttons;app->tick_hz=c->tick_hz;n->app2.private_memory=private_memory;n->app2.private_bytes=private_bytes;n->app2.ticks=ticks;n->app2.sleep=sleep_ns;
 if(app->version>=3){n->app3.load_image=load_image;n->app3.unload_image=unload_image;}if(app->version>=4){n->app4.image_begin=image_begin_async;n->app4.image_step=image_step_async;n->app4.image_release=image_release_async;}
 if(app->version>=5){n->environment.version=1;n->environment.bytes=sizeof n->environment;n->environment.sdk=c->sdk;n->environment.sdk_bytes=6164480;n->environment.application_pid=c->pid;n->environment.ro_pid=c->ro_pid;n->environment.ro_handle=c->ro_handle;n->environment.filesystem=c->filesystem;n->environment.tick_hz=c->tick_hz;n->app5.environment=&n->environment;n->app5.worker_begin=worker_begin;n->app5.worker_poll=worker_poll;n->app5.worker_join=worker_join;}
 if(app->version==6){
  IzNativeGraphics*g=&n->graphics;g->version=1;g->bytes=sizeof *g;g->function_count=IZ_GFX_COUNT;g->image_count=(u32)c->image_count;g->vulkan=c->graphics_vulkan;g->instance=c->graphics_instance;g->physical=c->graphics_physical;g->device=c->device;g->queue=c->queue;g->swapchain=c->swapchain;g->command_pool=c->command_pool;
  memcpy(g->images,c->images,sizeof g->images);memcpy(g->views,c->views,sizeof g->views);memcpy(g->functions,c->functions+F_CREATE_SHADER,sizeof g->functions);memcpy(g->memory_properties,c->graphics_memory_properties,sizeof g->memory_properties);
  n->app6.graphics=g;n->app6.presentation_acquire=presentation_acquire;n->app6.presentation_release=presentation_release;
 }
 if(h->flags&IZ_APP_AUDIO_OUT){n->audio.version=1;n->audio.bytes=sizeof n->audio;for(unsigned i=0;i<IZ_AUDIO_FUNCTIONS;i++)n->audio.functions[i]=(IzNativeAudioFunction)(uintptr_t)c->audio_functions[i];n->app2.audio=&n->audio;}
 size_t root=sizeof(IZ_SD_ROOT)-1;memcpy(n->root,IZ_SD_ROOT,root);n->root[root++]='/';memcpy(n->root+root,h->id,strlen(h->id)+1);
 c->native_data=n->data;c->native_data_bytes=n->data_bytes;c->native_code_reads+=h->section[code].bytes;c->native_launches++;n->initialized=1;rc=iz_native_event(n,IZ_NATIVE_INIT);c->native_status=rc;return rc;
}
u32 iz_native_load(IzFs*fs,const IzAppHeader*h,const u8*image,IzNative*n,void*p,u64 bytes){return iz_native_load_saved(fs,h,image,n,p,bytes,0);}
