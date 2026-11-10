#include "abi.h"
#include "checkpoint.h"
#include "host-frames.h"
#include "host-input.h"
#define P(v) ((u64)(uintptr_t)(v))
#define W(p,o,v) (*(u32*)((u8*)(p)+(o))=(u32)(v))
#define Q(p,o,v) (*(u64*)((u8*)(p)+(o))=(u64)(v))
#define GET(p,o) (*(u32*)((u8*)(p)+(o)))
#define WG(p,o) (*(u64*)((u8*)(p)+(o)))
#define VK(c,f,a,b,d,e,g,h,i,j) ((u32)CALL(c,f,a,b,d,e,g,h,i,j))
static void st(void*p,size_t n,u32 type){memset(p,0,n);W(p,0,type);}
static int stopping(IzContext*c){return __atomic_load_n(&c->stop,__ATOMIC_ACQUIRE)!=0;}
static void file_path(char*out,const char*id){size_t n=sizeof(IZ_SD_ROOT)-1;memcpy(out,IZ_SD_ROOT,n);out[n++]='/';size_t k=strlen(id);memcpy(out+n,id,k);n+=k;memcpy(out+n,"/launcher.izapp",sizeof "/launcher.izapp");}
typedef struct {IzAppHeader h;char folder[48];u32 host;} Entry;
static int host_valid(IzContext*c){
 if(!c->host_mode)return 1;
 if(c->host_mode!=1||!c->parent_snapshot||c->host_count>IZ_MAX_APPS||c->ram_only)return 0;
 for(unsigned i=0;i<c->host_count;i++){
  const char*id=c->host_catalog[i].id;unsigned n=0;while(n<48&&id[n]){char ch=id[n];if(!((ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||(n&&ch=='-')))return 0;n++;}
  if(!n||!c->host_catalog[i].name[0])return 0;
  for(unsigned j=0;j<i;j++)if(!memcmp(id,c->host_catalog[j].id,48))return 0;
 }
 const u8*parent=(void*)(uintptr_t)c->parent_snapshot;return GET(parent,112)==c->host_count&&GET(parent,1020)==c->host_count;
}
static void host_cancel(IzContext*c){
 /* No grant has been issued while the native loop is running. A late PC
  * claim for this token can therefore be rejected without touching graphics. */
 u32 request=__atomic_load_n(&c->host_display_request,__ATOMIC_ACQUIRE);
 if(request!=__atomic_load_n(&c->host_display_release,__ATOMIC_ACQUIRE)){
  __atomic_store_n(&c->host_display_release,request,__ATOMIC_RELEASE);
 }
 c->host_launch=c->host_remove=0;
}
static void host_request(IzContext*c,u32 launch,u32 remove){
 host_cancel(c);u32 previous=__atomic_load_n(&c->host_display_request,__ATOMIC_ACQUIRE);
 /* Capturing an offer must not freeze the compute menu while the PC is
  * disconnected. Restore its action only after the exact claim is granted. */
 W((void*)(uintptr_t)c->controls,1012,0);W((void*)(uintptr_t)c->controls,940,0);W((void*)(uintptr_t)c->controls,944,0);
 W((void*)(uintptr_t)c->controls,960,0);W((void*)(uintptr_t)c->controls,964,0);iz_menu_changed(c);
 if(previous==0xffffffffu){c->last_app_error=0xe903;return;}
 u32 token=(previous+1u)|1u;
 c->host_launch=launch;c->host_remove=remove;__atomic_store_n(&c->host_display_request,token,__ATOMIC_RELEASE);
}
static void sort_dirs(char dirs[][48],u32 count){for(unsigned i=1;i<count;i++){char name[48];memcpy(name,dirs[i],48);unsigned j=i;while(j&&memcmp(name,dirs[j-1],48)<0){memcpy(dirs[j],dirs[j-1],48);j--;}memcpy(dirs[j],name,48);}}
static u32 signature(u32 value,const char*id,u64 size,const IzAppHeader*h){value=iz_crc_update(value,id,48);value=iz_crc_update(value,&size,8);return h?iz_crc_update(value,h,sizeof *h):value;}
typedef struct {char dirs[IZ_MAX_DIRS][48];u32 count,index,value;int active;} InventoryWatch;
static u32 inventory_step(IzFs*fs,InventoryWatch*w,int*done){
 *done=0;
 if(!w->active){w->count=w->index=w->value=0;u32 rc=iz_fs_dirs(fs,w->dirs,&w->count);if(rc)return rc;sort_dirs(w->dirs,w->count);w->active=1;}
 else if(w->index<w->count){
  const char*id=w->dirs[w->index++];char path[128];file_path(path,id);u32 file=0;u64 size=0;u32 rc=iz_fs_open(fs,path,&file,&size);
  if(rc){if(fs->c->poisoned)return rc;}
  else{IzAppHeader h;int header=size>=sizeof h;if(header)rc=iz_fs_read(fs,file,0,&h,sizeof h);u32 closed=iz_fs_close(fs,file);if(rc)return rc;if(closed)return closed;w->value=signature(w->value,id,size,header?&h:0);}
 }
 /* At most one 512-byte application header per presented frame, no tiles
  * or executable data. No child file handle survives a frame boundary. */
 if(w->index==w->count){w->active=0;*done=1;}return 0;
}
typedef struct {
 u64 shaders[7],pipeline,compute[4],commands[IZ_MAX_IMAGES],secondary;
 u64 buffer,memory,mapped,set_layout,layout,pool,descriptor;
 u32 slot,ready;
} Gpu;
static u32 read_section(IzFs*fs,u32 file,const IzSection*s,void*out){
 if(!s->bytes)return 0;return iz_fs_read_crc(fs,file,s->offset,out,s->bytes,s->crc32);
}
static u32 scan(IzFs*fs,Entry*apps){
 IzContext*c=fs->c;char dirs[IZ_MAX_DIRS][48];u32 count=0;if(!host_valid(c))return 0xe902;
 char selected[48]={0};u32 old_index=GET((void*)(uintptr_t)c->snapshot,80),selected_source=0;if(old_index<c->catalog_count){memcpy(selected,c->catalog[old_index].id,48);selected_source=c->catalog_source[old_index];}
 __atomic_add_fetch(&c->catalog_revision,1,__ATOMIC_RELEASE);c->catalog_count=0;memset(c->catalog,0,sizeof c->catalog);memset(c->catalog_source,0,sizeof c->catalog_source);c->count=0;c->rejected=0;
 u32 rc=iz_fs_dirs(fs,dirs,&count);if(rc)return rc;
 sort_dirs(dirs,count);u32 fingerprint=0;
 u8*snapshot=(void*)(uintptr_t)c->snapshot;
 memset(snapshot+1024+IZ_ATLAS_BYTES,0,16*IZ_TILE_BYTES);
 if(c->host_mode)for(unsigned i=0;i<c->host_count;i++){
  memset(&apps[i],0,sizeof apps[i]);apps[i].host=1;memcpy(apps[i].folder,c->host_catalog[i].id,48);memcpy(&c->catalog[i],&c->host_catalog[i],sizeof c->catalog[i]);c->catalog_source[i]=1;
  u8*tile=snapshot+1024+IZ_ATLAS_BYTES+i*IZ_TILE_BYTES;memcpy(tile,(u8*)(uintptr_t)c->parent_snapshot+1024+IZ_ATLAS_BYTES+i*IZ_TILE_BYTES,IZ_TILE_BYTES);W(tile,102512,0);c->count++;
 }
 for(unsigned i=0;i<count&&!stopping(c);i++){
  char path[128];file_path(path,dirs[i]);u32 file=0;u64 size=0;rc=iz_fs_open(fs,path,&file,&size);
  if(rc){if(c->poisoned)return rc;continue;}
  IzAppHeader h;rc=size<sizeof h?0xe111:iz_fs_read(fs,file,0,&h,sizeof h);if(!rc||size<sizeof h)fingerprint=signature(fingerprint,dirs[i],size,size>=sizeof h?&h:0);if(!rc)rc=iz_validate_header(&h,size);
  if(!rc&&memcmp(h.id,dirs[i],48))rc=0xe112;
  if(!rc&&c->count==IZ_MAX_APPS)rc=0xe113;
  if(!rc){u8*tile=snapshot+1024+IZ_ATLAS_BYTES+c->count*IZ_TILE_BYTES;rc=read_section(fs,file,&h.section[SEC_TILE],tile);
   if(!rc){W(tile,102512,0x44534453u);W(tile,102516,(h.flags&IZ_APP_MANAGED)?2:1);apps[c->count].h=h;apps[c->count].host=(h.flags&IZ_APP_MANAGED)?2:0;c->catalog_source[c->count]=apps[c->count].host;memcpy(apps[c->count].folder,dirs[i],48);memcpy(c->catalog[c->count].id,h.id,48);memcpy(c->catalog[c->count].name,h.name,64);c->count++;}}
  u32 close=iz_fs_close(fs,file);if(close)return close;if(rc){c->rejected++;c->last_app_error=rc;}
 }
 u32 index=0;for(unsigned i=0;i<c->count;i++)if(c->catalog_source[i]==selected_source&&!memcmp(c->catalog[i].id,selected,48)){index=i;break;}
 u32 ordinal=0;if(index){u32 exponent=0,value=index;while(value>>1){exponent++;value>>=1;}ordinal=((127+exponent)<<23)|((index-(1u<<exponent))<<(23-exponent));}
 W(snapshot,80,index);W(snapshot,68,ordinal);W(snapshot,112,c->count);W(snapshot,1020,c->count);W(snapshot,116,0);
 W(snapshot,1008,0);W(snapshot,1012,0);W(snapshot,1016,0);W(snapshot,784,0);W(snapshot,800,0);W(snapshot,804,0);W(snapshot,812,0);W(snapshot,836,0);W(snapshot,940,0);W(snapshot,944,0);W(snapshot,948,0);
 if(!c->host_mode){W(snapshot,808,0);W(snapshot,832,0);W(snapshot,840,0);W(snapshot,844,0);}
 W(snapshot,960,0);W(snapshot,964,0);W(snapshot,0x700004,0);W(snapshot,0x700d7c,c->host_mode?2:1);c->auto_scan_signature=fingerprint;c->auto_scan_error=0;c->scan_generation++;c->catalog_count=c->count;c->host_applied=c->host_revision;iz_library_menu(c);iz_menu_changed(c);__atomic_add_fetch(&c->catalog_revision,1,__ATOMIC_RELEASE);
 return 0;
}
static u32 private_buffer(IzContext*c,const IzAppHeader*h,Gpu*g){
 u8 b[1024];u64 requirements[3];
 st(b,56,12);Q(b,24,h->private_bytes);W(b,32,0x20);
 u32 rc=VK(c,F_CREATE_BUFFER,c->device,b,0,&g->buffer,0,0,0,0);if(rc)return rc;
 CALL(c,F_BUFFER_REQUIREMENTS,c->device,g->buffer,requirements,0,0,0,0,0);u32 bits=(u32)requirements[2]&c->memory_type;
 if(!bits||requirements[0]<h->private_bytes||requirements[0]>(u64)h->private_bytes+65536)return 0xe201;
 unsigned type=0;while(!(bits&(1u<<type)))type++;
 st(b,32,5);Q(b,16,requirements[0]);W(b,24,type);
 rc=VK(c,F_ALLOCATE_MEMORY,c->device,b,0,&g->memory,0,0,0,0);if(rc)return rc;
 rc=VK(c,F_BIND_BUFFER,c->device,g->buffer,g->memory,0,0,0,0,0);if(rc)return rc;
 st(b,48,1000271000);Q(b,24,g->memory);Q(b,40,requirements[0]);rc=VK(c,F_MAP_MEMORY,c->device,b,&g->mapped,0,0,0,0,0);if(rc)return rc;
 if(!g->mapped||((h->flags&IZ_APP_AUDIO_OUT)&&g->mapped%4096))return 0xe201;
 u8 bindings[48];memset(bindings,0,sizeof bindings);for(unsigned i=0;i<2;i++){W(bindings,i*24,i);W(bindings,i*24+4,7);W(bindings,i*24+8,1);W(bindings,i*24+12,49);}
 st(b,32,32);W(b,20,2);Q(b,24,P(bindings));rc=VK(c,F_CREATE_SET_LAYOUT,c->device,b,0,&g->set_layout,0,0,0,0);if(rc)return rc;
 st(b,48,30);W(b,20,1);Q(b,24,P(&g->set_layout));rc=VK(c,F_CREATE_LAYOUT,c->device,b,0,&g->layout,0,0,0,0);if(rc)return rc;
 u32 pool_size[]={7,2};st(b,40,33);W(b,20,1);W(b,24,1);Q(b,32,P(pool_size));rc=VK(c,F_CREATE_POOL,c->device,b,0,&g->pool,0,0,0,0);if(rc)return rc;
 st(b,40,34);Q(b,16,g->pool);W(b,24,1);Q(b,32,P(&g->set_layout));rc=VK(c,F_ALLOCATE_SET,c->device,b,&g->descriptor,0,0,0,0,0);if(rc)return rc;
 for(unsigned i=0;i<2;i++){u64 info[]={i?g->buffer:c->controls_buffer,0,i?h->private_bytes:c->controls_bytes};st(b,64,35);Q(b,16,g->descriptor);W(b,24,i);W(b,32,1);W(b,36,7);Q(b,48,P(info));CALL(c,F_UPDATE_SET,c->device,1,b,0,0,0,0,0);}
 return 0;
}
static u32 pipelines(IzContext*c,const IzAppHeader*h,const u8*image,Gpu*g){
 u8 info[144],stages[96],vertex[48],assembly[32],view[48],raster[64],sample[48],blend[56],attachment[32],format_info[40];
 const char main_name[]="main";u32 format=37;
 const unsigned section[]={SEC_VERTEX,SEC_FRAGMENT,SEC_COMPUTE,SEC_BEGIN,SEC_AFTER,SEC_PAD};
 for(unsigned i=0;i<6;i++){
  if(h->version==1&&(h->flags&IZ_APP_NATIVE)&&(i==3||i==4))continue;
  const IzSection*s=&h->section[section[i]];if(!s->bytes)continue;const u8*bytes=image+s->offset;
  if(GET(bytes,0)!=0x07230203)return 0xe202;
  st(info,40,16);Q(info,24,s->bytes);Q(info,32,P(bytes));u32 rc=VK(c,F_CREATE_SHADER,c->device,info,0,&g->shaders[i],0,0,0,0);if(rc)return rc;
 }
 memset(stages,0,sizeof stages);for(unsigned i=0;i<2;i++){st(stages+i*48,48,18);W(stages,i*48+20,i?16:1);Q(stages,i*48+24,g->shaders[i]);Q(stages,i*48+32,P(main_name));}
 st(vertex,sizeof vertex,19);st(assembly,sizeof assembly,20);W(assembly,20,3);
 u32 viewport[]={0,0,0x44f00000,0x44870000,0,0x3f800000};u32 scissor[]={0,0,1920,1080};
 st(view,sizeof view,22);W(view,20,1);W(view,32,1);Q(view,24,P(viewport));Q(view,40,P(scissor));
 st(raster,sizeof raster,23);W(raster,56,0x3f800000);st(sample,sizeof sample,24);W(sample,20,1);
 memset(attachment,0,sizeof attachment);W(attachment,28,15);st(blend,sizeof blend,26);W(blend,28,1);Q(blend,32,P(attachment));
 st(format_info,sizeof format_info,1000044002);W(format_info,20,1);Q(format_info,24,P(&format));
 st(info,sizeof info,28);Q(info,8,P(format_info));W(info,20,2);Q(info,24,P(stages));Q(info,32,P(vertex));Q(info,40,P(assembly));Q(info,56,P(view));Q(info,64,P(raster));Q(info,72,P(sample));Q(info,88,P(blend));Q(info,104,g->layout);W(info,136,0xffffffff);
 u32 rc=VK(c,F_CREATE_GRAPHICS,c->device,0,1,info,0,&g->pipeline,0,0);if(rc)return rc;
 for(unsigned i=0;i<4;i++){
  unsigned shader=i==3?5:i+2;if(!g->shaders[shader])continue;
  st(info,96,29);st(info+24,48,18);W(info,44,32);Q(info,48,g->shaders[shader]);Q(info,56,P(main_name));Q(info,72,g->layout);W(info,88,0xffffffff);
  rc=VK(c,F_CREATE_COMPUTE,c->device,0,1,info,0,&g->compute[i],0,0);if(rc)return rc;
 }return 0;
}
static void bind_set(IzContext*c,Gpu*g,u64 command){u8 b[64];st(b,64,1000545003);W(b,16,49);Q(b,24,g->layout);W(b,36,1);Q(b,40,P(&g->descriptor));CALL(c,F_BIND_SET,command,b,0,0,0,0,0,0);}
static void memory_barrier(IzContext*c,u64 command,u64 ss,u64 sa,u64 ds,u64 da){u8 barrier[48],dep[64];st(barrier,48,1000314000);Q(barrier,16,ss);Q(barrier,24,sa);Q(barrier,32,ds);Q(barrier,40,da);st(dep,64,1000314003);W(dep,20,1);Q(dep,24,P(barrier));CALL(c,F_BARRIER,command,dep,0,0,0,0,0,0);}
static void image_barrier(IzContext*c,u64 command,u64 image,u32 old,u32 next,u64 ss,u64 sa,u64 ds,u64 da){u8 b[96],dep[64];st(b,96,1000314002);Q(b,16,ss);Q(b,24,sa);Q(b,32,ds);Q(b,40,da);W(b,48,old);W(b,52,next);W(b,56,0xffffffff);W(b,60,0xffffffff);Q(b,64,image);W(b,72,1);W(b,80,1);W(b,88,1);st(dep,64,1000314003);W(dep,48,1);Q(dep,56,P(b));CALL(c,F_BARRIER,command,dep,0,0,0,0,0,0);}
static u32 command_allocate(IzContext*c,u64*out,int secondary){u8 info[32];st(info,32,40);Q(info,16,c->command_pool);W(info,24,secondary?1:0);W(info,28,1);return VK(c,F_ALLOCATE_COMMANDS,c->device,info,out,0,0,0,0,0);}
static void dispatch(IzContext*c,u64 command,u64 pipeline,u32 x,u32 y,u32 z){CALL(c,F_BIND_PIPELINE,command,1,pipeline,0,0,0,0,0);CALL(c,F_DISPATCH,command,x,y,z,0,0,0,0);}
static u32 record(IzContext*c,const IzAppHeader*h,Gpu*g){
 u8 info[72],inheritance[56];u32 rc;
 if(h->repeat){
  rc=command_allocate(c,&g->secondary,1);if(rc)return rc;st(inheritance,56,41);st(info,32,42);W(info,16,4);Q(info,24,P(inheritance));rc=VK(c,F_BEGIN_COMMAND,g->secondary,info,0,0,0,0,0,0);if(rc)return rc;bind_set(c,g,g->secondary);
  dispatch(c,g->secondary,g->compute[0],h->groups[0],h->groups[1],h->groups[2]);memory_barrier(c,g->secondary,0x800,0x40,0x800,0x60);
  dispatch(c,g->secondary,g->compute[2],1,1,1);memory_barrier(c,g->secondary,0x800,0x40,0x800,0x60);rc=VK(c,F_END_COMMAND,g->secondary,0,0,0,0,0,0,0);if(rc)return rc;
 }
 for(unsigned i=0;i<c->image_count;i++){
  rc=command_allocate(c,&g->commands[i],0);if(rc)return rc;u64 command=g->commands[i];st(info,32,42);rc=VK(c,F_BEGIN_COMMAND,command,info,0,0,0,0,0,0);if(rc)return rc;
  memory_barrier(c,command,0x4888,0x4060,0x800,0x60);bind_set(c,g,command);dispatch(c,command,g->compute[3],1,1,1);memory_barrier(c,command,0x800,0x40,0x800,0x60);
  if(g->secondary){dispatch(c,command,g->compute[1],1,1,1);memory_barrier(c,command,0x800,0x40,0x800,0x60);u64 handles[512];for(unsigned j=0;j<h->repeat;j++)handles[j]=g->secondary;CALL(c,F_EXECUTE,command,h->repeat,handles,0,0,0,0,0);bind_set(c,g,command);}
  else dispatch(c,command,g->compute[0],h->groups[0],h->groups[1],h->groups[2]);
  memory_barrier(c,command,0x800,0x40,0x88,0x20);image_barrier(c,command,c->images[i],0,2,0,0,0x400,0x100);
  u8 attachment[72];st(attachment,72,1000044001);Q(attachment,16,c->views[i]);W(attachment,24,2);W(attachment,44,1);W(attachment,52,0x3c449ba6);W(attachment,56,0x3ca3d70a);W(attachment,60,0x3d3851ec);W(attachment,64,0x3f800000);
  st(info,72,1000044000);W(info,28,1920);W(info,32,1080);W(info,36,1);W(info,44,1);Q(info,48,P(attachment));CALL(c,F_BEGIN_RENDER,command,info,0,0,0,0,0,0);
  CALL(c,F_BIND_PIPELINE,command,0,g->pipeline,0,0,0,0,0);CALL(c,F_DRAW,command,3,1,0,0,0,0,0);CALL(c,F_END_RENDER,command,0,0,0,0,0,0,0);
  image_barrier(c,command,c->images[i],2,1000001002,0x400,0x100,0,0);memory_barrier(c,command,0x800,0x40,0x4000,0x2000);
  rc=VK(c,F_END_COMMAND,command,0,0,0,0,0,0,0);if(rc)return rc;
 }return 0;
}
static void destroy(IzContext*c,Gpu*g){
 for(unsigned i=0;i<c->image_count;i++)if(g->commands[i])CALL(c,F_FREE_COMMANDS,c->device,c->command_pool,1,&g->commands[i],0,0,0,0);
 if(g->secondary)CALL(c,F_FREE_COMMANDS,c->device,c->command_pool,1,&g->secondary,0,0,0,0);
 if(g->pipeline)CALL(c,F_DESTROY_PIPELINE,c->device,g->pipeline,0,0,0,0,0,0);
 for(unsigned i=0;i<4;i++)if(g->compute[i])CALL(c,F_DESTROY_PIPELINE,c->device,g->compute[i],0,0,0,0,0,0);
 for(unsigned i=0;i<7;i++)if(g->shaders[i])CALL(c,F_DESTROY_SHADER,c->device,g->shaders[i],0,0,0,0,0,0);
 if(g->pool)CALL(c,F_DESTROY_POOL,c->device,g->pool,0,0,0,0,0,0);
 if(g->set_layout){CALL(c,F_DESTROY_LAYOUT,c->device,g->layout,0,0,0,0,0,0);CALL(c,F_DESTROY_SET_LAYOUT,c->device,g->set_layout,0,0,0,0,0,0);}
 if(g->mapped)CALL(c,F_UNMAP_MEMORY,c->device,g->memory,0,0,0,0,0,0);
 if(g->buffer)CALL(c,F_DESTROY_BUFFER,c->device,g->buffer,0,0,0,0,0,0);
 if(g->memory)CALL(c,F_FREE_MEMORY,c->device,g->memory,0,0,0,0,0,0);memset(g,0,sizeof *g);
}
static u32 native_drain(IzContext*c,IzNative*n){u32 rc;do{rc=iz_native_unload(n);if(rc==IZ_NATIVE_STOP_PENDING)CALL(c,F_SLEEP,0,0,0,0,0,0,0,0);}while(rc==IZ_NATIVE_STOP_PENDING);return rc;}
static u32 launch(IzFs*fs,Entry*entry,unsigned slot,Gpu*g,IzNative*n,IzCheckpointSession*checkpoint){
 IzContext*c=fs->c;c->last_app_error=0;c->load_stage=1;char path[128];file_path(path,entry->folder);u32 file=0;u64 size=0;u32 rc=iz_fs_open(fs,path,&file,&size);if(rc)return rc;
 IzAppHeader h,loaded;memset(&h,0,sizeof h);rc=iz_fs_read(fs,file,0,&h,sizeof h);if(!rc)rc=iz_validate_header(&h,size);if(!rc&&memcmp(&h,&entry->h,sizeof h))rc=0xe114;
 /* Keep only the small executable/shader/control sections in CPU staging.
  * Large private images go straight from bounded SD reads to their final
  * coherent GPU allocation. Never duplicate a complete game in SDK heap. */
 u64 staging=512,temporary=0;loaded=h;
 if(!rc)for(unsigned i=0;i<SEC_COUNT;i++)if(i!=SEC_PRIVATE&&h.section[i].bytes){staging=(staging+3)&~3ull;loaded.section[i].offset=(u32)staging;staging+=h.section[i].bytes;}
 c->staging_bytes=(u32)staging;c->load_stage=2;u8*image=(void*)(uintptr_t)c->arena;
 if(!rc&&staging>c->arena_bytes){u64 capacity=(staging+4095)&~4095ull;rc=(u32)CALL(c,F_ALLOCATE,&temporary,4096,capacity,0,0,0,0,0);if(!rc){if(!temporary||temporary%4096||temporary>=(1ull<<39)||capacity>(1ull<<39)-temporary){c->poisoned=1;rc=0xe115;}else image=(void*)(uintptr_t)temporary;}}
 if(!rc)for(unsigned i=0;i<SEC_COUNT;i++)if(i!=SEC_PRIVATE&&h.section[i].bytes){rc=read_section(fs,file,&h.section[i],image+loaded.section[i].offset);if(rc)break;}
 if(!rc&&(!g->ready||g->slot!=slot)){destroy(c,g);g->layout=c->menu_layout;g->descriptor=c->menu_descriptor;if(h.private_bytes){c->load_stage=3;rc=private_buffer(c,&h,g);}if(!rc){c->load_stage=4;rc=pipelines(c,&loaded,image,g);}if(!rc)rc=record(c,&h,g);if(rc){if(!c->poisoned)destroy(c,g);}else{g->slot=slot;g->ready=1;}}
 if(!rc){
  u8*controls=(void*)(uintptr_t)c->controls;memset(controls,0,c->controls_bytes);memcpy(controls,image+loaded.section[SEC_STATE].offset,1024);memcpy(controls+1024,image+loaded.section[SEC_DATA].offset,h.section[SEC_DATA].bytes);
  W(controls,1008,0);W(controls,1012,0);W(controls,1016,0);W(controls,960,0);
  if(g->mapped){memset((void*)(uintptr_t)g->mapped,0,h.private_bytes);c->load_stage=5;rc=read_section(fs,file,&h.section[SEC_PRIVATE],(void*)(uintptr_t)g->mapped);}
 }
 if(!c->poisoned){u32 close=iz_fs_close(fs,file);if(!rc)rc=close;}
 if(!rc){c->load_stage=7;rc=cp_restore(fs,&h,c->checkpoint_enabled?cp_kind(&h,&loaded,image):0,(void*)(uintptr_t)g->mapped,checkpoint);}
 if(!rc&&(h.flags&IZ_APP_NATIVE)){c->load_stage=6;rc=iz_native_load_saved(fs,&loaded,image,n,(void*)(uintptr_t)g->mapped,h.private_bytes,checkpoint->kind==2&&checkpoint->generation?&checkpoint->portable:0);if(rc&&!c->poisoned){u32 closed=native_drain(c,n);if(closed)rc=closed;}if(!rc&&checkpoint->generation)n->app.frames=checkpoint->frames;}
 if(!rc){c->load_stage=8;memcpy(c->last_id,h.id,48);c->active=slot+1;c->transitions++;}
 else if(!c->poisoned)destroy(c,g);
 if(temporary&&!c->poisoned)CALL(c,F_FREE,temporary,0,0,0,0,0,0,0);return rc;
}
static u32 present_at(IzContext*c,const u64*commands,u64 controls_address){
 // The old host sweep is a frozen snapshot while its renderer is joined.
 // Both shell shaders and app pad-normalizers must use our fresh primary pad.
 // Do this after transitions, which can restore the original menu snapshot.
 W((void*)(uintptr_t)controls_address,0x7f0000,0);
 if(controls_address!=c->controls){
  const u8*sample=(void*)(uintptr_t)(c->controls+IZ_CONSOLE_SENSOR_AT);u8*target=(void*)(uintptr_t)(controls_address+IZ_CONSOLE_SENSOR_AT);
  /* Copy only the independently sampled console/network status. The old PC
   * pad sweep is never copied over this frame's fresh native controller. */
  if(GET(sample,0)==IZ_CONSOLE_SENSOR_MAGIC){memcpy(target,sample,IZ_CONSOLE_SENSOR_BYTES);if(GET(sample,0)!=IZ_CONSOLE_SENSOR_MAGIC)W(target,0,0);}else W(target,0,0);
 }
 u8 acquire[56],submit[72],present_info[64];u32 index=0,stage=0x400;
 st(acquire,56,1000060010);Q(acquire,16,c->swapchain);Q(acquire,24,~0ull);Q(acquire,32,c->acquired);W(acquire,48,1);
 u32 rc=VK(c,F_ACQUIRE,c->device,acquire,&index,0,0,0,0,0);if(rc)return rc;if(index>=c->image_count)return 0xe301;
 u64 semaphore=c->semaphores[index],command=commands[index];
 st(submit,72,4);W(submit,16,1);Q(submit,24,P(&c->acquired));Q(submit,32,P(&stage));W(submit,40,1);Q(submit,48,P(&command));W(submit,56,1);Q(submit,64,P(&semaphore));
 rc=VK(c,F_SUBMIT,c->queue,1,submit,0,0,0,0,0);if(rc)return rc;
 st(present_info,64,1000001001);W(present_info,16,1);Q(present_info,24,P(&semaphore));W(present_info,32,1);Q(present_info,40,P(&c->swapchain));Q(present_info,48,P(&index));
 rc=VK(c,F_PRESENT,c->queue,present_info,0,0,0,0,0,0);if(rc)return rc;return VK(c,F_QUEUE_IDLE,c->queue,0,0,0,0,0,0,0);
}
static u32 present(IzContext*c,const u64*commands){return present_at(c,commands,c->controls);}
static u32 input_at(IzContext*c,u8*controls,u64*previous,u64*first_release){
 u8 raw[33][64];u32 handheld=32;const unsigned kinds[]={F_PAD_FULLKEY,F_PAD_DUAL,F_PAD_LEFT,F_PAD_RIGHT};
 memset(raw,0,sizeof raw);CALL(c,F_PAD_HANDHELD,raw[0],&handheld,0,0,0,0,0,0);
 for(u32 player=0;player<8;player++)for(unsigned kind=0;kind<4;kind++)CALL(c,kinds[kind],raw[1+player*4+kind],&player,0,0,0,0,0,0);
 // Menus can merge pads, but launcher exit belongs to handheld / player 1.
 // Player 2 must not complete player 1's double-R3 sequence.
 u64 buttons=0,shortcut_buttons=0;u32 attributes=0;int selected=-1;int32_t axes[4]={0};for(unsigned i=0;i<33;i++)if(GET(raw[i],32)&1){buttons|=WG(raw[i],8);if(i<=4)shortcut_buttons|=WG(raw[i],8);attributes|=GET(raw[i],32);if(selected<0)selected=(int)i;
  for(unsigned j=0;j<4;j++){int32_t value=(int32_t)GET(raw[i],16+j*4);if(value>32767)value=32767;if(value<-32767)value=-32767;if((value<0?-value:value)>(axes[j]<0?-axes[j]:axes[j]))axes[j]=value;}}
 if(selected<0)selected=0;memcpy(controls,raw[selected],64);Q(controls,8,buttons);W(controls,32,attributes);for(unsigned j=0;j<4;j++)W(controls,16+j*4,axes[j]);
 CALL(c,F_TOUCH,controls+128,0,0,0,0,0,0,0);
 u64 now=CALL(c,F_TICK,0,0,0,0,0,0,0,0);Q(controls,0x700d70,now-(c->active?c->checkpoint_time_bias:0));W(controls,0x700d78,c->tick_hz);
 if(c->active&&(c->owner.native.app.flags&IZ_NATIVE_MANAGED_INPUT))shortcut_buttons=0;
 u64 r3=1u<<5;u32 exit=0;if((shortcut_buttons&r3)&&!(*previous&r3)){if(*first_release&&now-*first_release<c->tick_hz*3/5){exit=1;*first_release=0;}else *first_release=now;}
 *previous=shortcut_buttons;return exit;
}
static u32 input(IzContext*c,u64*previous,u64*first_release){return input_at(c,(void*)(uintptr_t)c->controls,previous,first_release);}
static void host_frames_menu(IzContext*c){
 u8*controls=(void*)(uintptr_t)c->controls;memcpy(controls,(void*)(uintptr_t)c->snapshot,c->controls_bytes);
 W(controls,1012,0);W(controls,1016,0);W(controls,940,0);W(controls,944,0);W(controls,960,0);W(controls,964,0);iz_menu_changed(c);
}
u64 iz_run(IzContext*c){
 if(!c||c->magic!=0x44535a49||c->version!=1)return 0xe900;
 IzFs*fs=&c->owner.fs;IzNative*native=&c->owner.native;
 if(c->state||fs->c||native->fs||native->source||native->mapped||native->data||native->image_state||native->worker_state)return 0xe930;
 memset(&c->owner,0,sizeof c->owner);fs->c=c;fs->buffer=(void*)(uintptr_t)c->scratch;c->state=1;
 if(c->probe==1){c->probe_result=iz_fs_probe(fs);c->state=c->probe_result?4:3;return c->probe_result;}
 if(c->probe==4){
  if(!c->controls_buffer||!c->menu_layout){c->state=4;return c->probe_result=0xe901;}
  char path[128];file_path(path,c->last_id);u32 file=0;u64 size=0;u32 rc=iz_fs_open(fs,path,&file,&size);IzAppHeader h;Gpu gpu;memset(&gpu,0,sizeof gpu);
  if(!rc)rc=iz_fs_read(fs,file,0,&h,sizeof h);if(!rc)rc=iz_validate_header(&h,size);if(!rc&&memcmp(h.id,c->last_id,48))rc=0xe112;
  if(!rc){c->load_stage=3;rc=private_buffer(c,&h,&gpu);}
  if(!rc){c->load_stage=5;memset((void*)(uintptr_t)gpu.mapped,0,h.private_bytes);rc=read_section(fs,file,&h.section[SEC_PRIVATE],(void*)(uintptr_t)gpu.mapped);if(!rc)c->probe_crc=h.section[SEC_PRIVATE].crc32;}
  if(file&&!c->poisoned){u32 close=iz_fs_close(fs,file);if(!rc)rc=close;}if(!c->poisoned)destroy(c,&gpu);c->probe_result=rc;c->state=rc?4:3;return rc;
 }
 if(c->probe==3){
  char path[128];file_path(path,c->last_id);u32 file=0;u64 size=0;u32 rc=iz_fs_open(fs,path,&file,&size);IzAppHeader h;
  if(!rc)rc=iz_fs_read(fs,file,0,&h,sizeof h);if(!rc)rc=iz_validate_header(&h,size);
  if(!rc&&memcmp(h.id,c->last_id,48))rc=0xe112;
  u32 crc=0;if(!rc)for(u32 done=0;done<h.section[SEC_PRIVATE].bytes&&!stopping(c);){u32 bytes=h.section[SEC_PRIVATE].bytes-done;if(bytes>65536)bytes=65536;
   rc=iz_fs_read(fs,file,h.section[SEC_PRIVATE].offset+done,(void*)(uintptr_t)c->arena,bytes);if(rc)break;
   crc=iz_crc_update(crc,(void*)(uintptr_t)c->arena,bytes);done+=bytes;
  }
  c->probe_crc=crc;if(!rc&&(stopping(c)||c->probe_crc!=h.section[SEC_PRIVATE].crc32))rc=0xe110;
  if(file&&!c->poisoned){u32 close=iz_fs_close(fs,file);if(!rc)rc=close;}c->probe_result=rc;c->state=rc?4:3;return rc;
 }
 if(c->probe==2){
  u32 file=0;u64 size=0;u32 rc=iz_fs_open(fs,IZ_SD_ROOT "/sd-proof-native/launcher.izapp",&file,&size);u8*image=(void*)(uintptr_t)c->arena;
  if(!rc&&(size<512||size>c->arena_bytes))rc=0xe115;if(!rc)rc=iz_fs_read(fs,file,0,image,(u32)size);
  if(file&&!c->poisoned){u32 close=iz_fs_close(fs,file);if(!rc)rc=close;}
  IzAppHeader*h=(void*)image;if(!rc)rc=iz_validate_header(h,size);if(!rc&&!(h->flags&IZ_APP_NATIVE))rc=0xe407;
  for(unsigned i=0;!rc&&i<SEC_COUNT;i++)if(h->section[i].bytes&&iz_crc(image+h->section[i].offset,h->section[i].bytes)!=h->section[i].crc32)rc=0xe110;
  if(!rc)rc=iz_native_load(fs,h,image,native,0,0);c->active=!rc;
  while(!rc&&!stopping(c)&&!GET((void*)(uintptr_t)c->controls,1016)&&c->frames<600){native->app.frames++;rc=iz_native_event(native,IZ_NATIVE_FRAME);c->frames++;CALL(c,F_SLEEP,16666667,0,0,0,0,0,0,0);}
  if(!c->poisoned){u32 close=native_drain(c,native);if(!rc)rc=close;}c->active=0;c->probe_result=rc;c->state=rc?4:3;return rc;
 }
 if(!c->controls||c->controls_bytes!=IZ_SHARED_BYTES||!c->image_count||c->image_count>IZ_MAX_IMAGES){c->state=4;return c->error=0xe901;}
 Entry apps[IZ_MAX_APPS];Gpu gpu;IzCheckpointSession checkpoint;IzHostFrameOwner host_frames;memset(&host_frames,0,sizeof host_frames);memset(&checkpoint,0,sizeof checkpoint);memset(&gpu,0,sizeof gpu);
 if(c->parent_snapshot){u8*parent=(void*)(uintptr_t)c->parent_snapshot;memcpy(parent,(void*)(uintptr_t)c->controls,c->controls_bytes);W(parent,1008,0);W(parent,1012,0);W(parent,1016,0);}
 memcpy((void*)(uintptr_t)c->snapshot,(void*)(uintptr_t)c->controls,c->controls_bytes);
 u32 rc=scan(fs,apps);if(rc){c->state=4;return c->error=rc;}memcpy((void*)(uintptr_t)c->controls,(void*)(uintptr_t)c->snapshot,c->controls_bytes);
 c->state=2;u64 previous=0,first_release=0,next_scan=0,offer_frame=0;u32 offer_token=0,completed_release=0;InventoryWatch watch;memset(&watch,0,sizeof watch);
 while(!stopping(c)){
  if(c->host_mode){
   u32 request=__atomic_load_n(&c->host_display_request,__ATOMIC_ACQUIRE);
   u32 released=__atomic_load_n(&c->host_display_release,__ATOMIC_ACQUIRE),claim=__atomic_load_n(&c->host_display_claim,__ATOMIC_ACQUIRE);
   /* A delayed release from an older network request cannot reopen a lease
    * whose return was already consumed by this native owner. */
   if(released<completed_release||released>request){released=completed_release;__atomic_store_n(&c->host_display_release,released,__ATOMIC_RELEASE);}
   if(released==request&&released>completed_release)completed_release=released;
   u8*menu=(void*)(uintptr_t)c->controls;u32 offered=c->host_launch?c->host_launch:c->host_remove;
   if(offered&&request!=released&&claim!=request){
    if(offer_token!=request){offer_token=request;offer_frame=c->frames;}
    else if(c->frames-offer_frame>=300){host_cancel(c);released=request;c->last_app_error=0xe905;W(menu,92,2);iz_menu_changed(c);}
   }
   /* A newer controller action wins over an unclaimed, older PC offer,
    * including a network reply arriving during an SD launch animation. */
   if(!c->active&&offered&&request!=released&&(GET(menu,1012)||GET(menu,944)||GET(menu,964)||GET(menu,80)+1u!=offered)){
    host_cancel(c);released=request;
   }
   u32 observed=__atomic_load_n(&c->host_display_observed,__ATOMIC_ACQUIRE);
   if(claim&&!(claim&1u)&&claim!=observed){
    if(claim<observed||c->active||request!=released||GET(menu,1012)||GET(menu,944)||GET(menu,964))__atomic_store_n(&c->host_display_rejected,claim,__ATOMIC_RELEASE);
    else{request=claim;__atomic_store_n(&c->host_display_request,request,__ATOMIC_RELEASE);}
    if(claim>observed)__atomic_store_n(&c->host_display_observed,claim,__ATOMIC_RELEASE);
   }
   if(!c->active&&request&&request!=released&&claim==request){
    /* Every previous presentation has reached QueueWaitIdle. No controller,
     * filesystem or graphics call may run until the host returns ownership. */
    hf_pause(c,&host_frames);destroy(c,&gpu);
    W((void*)(uintptr_t)c->controls,1012,c->host_launch);W((void*)(uintptr_t)c->controls,944,c->host_remove);
    memcpy((void*)(uintptr_t)c->snapshot,(void*)(uintptr_t)c->controls,1024);
    __atomic_store_n(&c->host_display_ready,request,__ATOMIC_RELEASE);
    while(!stopping(c)&&request!=__atomic_load_n(&c->host_display_release,__ATOMIC_ACQUIRE)){
     hf_offer(c,&host_frames,request);CALL(c,F_SLEEP,1000000,0,0,0,0,0,0,0);
    }
    __atomic_store_n(&c->host_display_ready,0,__ATOMIC_RELEASE);if(stopping(c))break;
    completed_release=request;
    c->host_launch=c->host_remove=0;previous=first_release=0;
    if(c->host_applied!=c->host_revision){
     u32 selected=GET((void*)(uintptr_t)c->snapshot,80);memcpy((void*)(uintptr_t)c->snapshot,(void*)(uintptr_t)c->parent_snapshot,c->controls_bytes);W((void*)(uintptr_t)c->snapshot,80,selected);
     memcpy((void*)(uintptr_t)c->controls,(void*)(uintptr_t)c->snapshot,c->controls_bytes);c->rescan=1;
    }
    hf_resume(c,&host_frames,request);
    if(host_frames.ticket){W((void*)(uintptr_t)c->controls,1012,0);W((void*)(uintptr_t)c->snapshot,1012,0);}
   }
  }
  if(host_frames.ticket){
   u8*app_controls=(void*)(uintptr_t)host_frames.live.controls;
   u32 exit=input_at(c,app_controls,&previous,&first_release),requested=GET(app_controls,1016);
   if(exit||requested){
    hf_return(c,&host_frames,exit?IZ_HOST_RETURN_USER:IZ_HOST_RETURN_APP);host_frames_menu(c);previous=first_release=0;c->transitions++;
   }else{
    rc=present_at(c,host_frames.live.commands,host_frames.live.controls);if(rc){c->poisoned=1;break;}c->frames++;continue;
   }
  }
  u8*controls=(void*)(uintptr_t)c->controls;u64 old_buttons=previous;u32 exit=input(c,&previous,&first_release);
  if(c->host_mode&&!c->active&&(c->host_launch||c->host_remove)&&(WG(controls,8)&~old_buttons&2u)){
   host_cancel(c);Q(controls,8,WG(controls,8)&~2ull);iz_menu_changed(c);
  }
  if(c->active&&GET(controls,1008)==IZ_NATIVE_CONNECTION_REQUEST){W(controls,1008,0);if(c->recovery_wake)__atomic_add_fetch((u32*)(uintptr_t)c->recovery_wake,1,__ATOMIC_RELEASE);}
  if(c->active&&(exit||GET(controls,1016)||native->stopping)){
   if(!checkpoint.ending){checkpoint.ending=1;if(!c->last_app_error){u32 saved=cp_save(fs,&apps[c->active-1].h,(void*)(uintptr_t)gpu.mapped,native,&checkpoint);if(c->poisoned){rc=saved?saved:0xe001;break;}}}
   rc=iz_native_unload(native);if(rc==IZ_NATIVE_STOP_PENDING)rc=0;else{if(rc)break;memcpy(controls,(void*)(uintptr_t)c->snapshot,c->controls_bytes);iz_menu_changed(c);c->active=0;c->checkpoint_time_bias=0;c->transitions++;first_release=0;}}
  u32 hold=__atomic_load_n(&c->host_hold_request,__ATOMIC_ACQUIRE);int held=hold&&hold!=__atomic_load_n(&c->host_hold_release,__ATOMIC_ACQUIRE);
  if(!c->active&&held)__atomic_store_n(&c->host_hold_ready,hold,__ATOMIC_RELEASE);
  if(!held)__atomic_store_n(&c->host_hold_ready,0,__ATOMIC_RELEASE);
  if(!c->active&&!held){
   u32 removing=GET(controls,944);
   if(removing){
    if(c->host_mode&&removing<=c->count&&apps[removing-1].host==1){host_request(c,0,removing);}
    else{
    if(c->host_mode)host_cancel(c);
    c->remove_sequence=GET(controls,948);c->remove_error=0;memset(c->removed_id,0,sizeof c->removed_id);
    if(removing>c->count)c->remove_error=0xe600;
    else{memcpy(c->removed_id,apps[removing-1].folder,48);destroy(c,&gpu);c->remove_error=iz_fs_remove_app(fs,c->removed_id);}
    if(c->poisoned){rc=c->remove_error?c->remove_error:0xe001;break;}
    W(controls,940,0);W(controls,944,0);W(controls,952,c->remove_error);W(controls,1012,0);c->rescan=1;
    }
   }
   iz_library_tick(c);
   u64 now=WG(controls,0x700d70);if(!next_scan)next_scan=now+c->tick_hz*5;
   if(now>=next_scan&&!GET(controls,0x700004)&&!GET(controls,800)&&!GET(controls,940)&&!GET(controls,944)){int done=0;u32 check=inventory_step(fs,&watch,&done);c->auto_scan_error=check;if(c->poisoned){rc=check?check:0xe001;break;}if(check||done){watch.active=0;next_scan=now+c->tick_hz*5;}if(!check&&done&&watch.value!=c->auto_scan_signature)c->rescan=1;}
   if(__atomic_exchange_n(&c->rescan,0,__ATOMIC_ACQ_REL)||GET(controls,104)!=GET((void*)(uintptr_t)c->snapshot,104)){
    watch.active=0;next_scan=now+c->tick_hz*5;destroy(c,&gpu);memcpy((void*)(uintptr_t)c->snapshot,controls,1024);rc=scan(fs,apps);if(rc)break;memcpy(controls,(void*)(uintptr_t)c->snapshot,c->controls_bytes);
   }
   u32 request=GET(controls,1012);if(request){if(c->host_mode&&request<=c->count&&apps[request-1].host){host_request(c,request,0);}else{if(c->host_mode)host_cancel(c);W(controls,1012,0);if(request<=c->count){memcpy((void*)(uintptr_t)c->snapshot,controls,1024);W((void*)(uintptr_t)c->snapshot,960,0);W((void*)(uintptr_t)c->snapshot,964,0);W((void*)(uintptr_t)c->snapshot,1016,0);rc=apps[request-1].host==2?0xe904:launch(fs,&apps[request-1],request-1,&gpu,native,&checkpoint);if(rc){c->last_app_error=rc;if(c->poisoned)break;memcpy(controls,(void*)(uintptr_t)c->snapshot,c->controls_bytes);iz_menu_changed(c);W(controls,1012,0);W(controls,92,1);rc=0;}}}}
  }
  if(c->active&&native->initialized&&!native->stopping){native->app.frames++;rc=iz_native_event(native,IZ_NATIVE_FRAME);if(rc){c->last_app_error=rc;if(c->poisoned)break;W(controls,1016,1);rc=0;}}
  if(c->active&&native->presentation_owned)CALL(c,F_SLEEP,16666667,0,0,0,0,0,0,0);
  else{rc=present(c,c->active?gpu.commands:c->menu_commands);if(rc){c->poisoned=1;break;}}c->frames++;
 }
 if(rc){c->error=rc;c->state=4;return rc;}
 if(host_frames.ticket){hf_return(c,&host_frames,IZ_HOST_RETURN_STOP);host_frames_menu(c);}
 if(c->active&&!checkpoint.ending&&!c->last_app_error){u32 saved=cp_save(fs,&apps[c->active-1].h,(void*)(uintptr_t)gpu.mapped,native,&checkpoint);if(c->poisoned){c->error=saved?saved:0xe001;c->state=4;return c->error;}}
 rc=native_drain(c,native);if(rc){c->error=rc;c->state=4;return rc;}destroy(c,&gpu);memcpy((void*)(uintptr_t)c->controls,(void*)(uintptr_t)(c->parent_snapshot?c->parent_snapshot:c->snapshot),c->controls_bytes);c->active=0;c->state=3;return 0;
}
