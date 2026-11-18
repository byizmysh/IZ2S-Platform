#include "directory.h"
#include "checkpoint.h"
#include "host-frames.h"
#include "host-input.h"
#include "host-rpc.h"
#include "host-mapper.h"
#define P(v) ((u64)(uintptr_t)(v))
#define W(p,o,v) (*(u32*)((u8*)(p)+(o))=(u32)(v))
#define Q(p,o,v) (*(u64*)((u8*)(p)+(o))=(u64)(v))
#define GET(p,o) (*(u32*)((u8*)(p)+(o)))
#define WG(p,o) (*(u64*)((u8*)(p)+(o)))
#define VK(c,f,a,b,d,e,g,h,i,j) ((u32)CALL(c,f,a,b,d,e,g,h,i,j))
#include "sdk-mailbox.h"
static void st(void*p,size_t n,u32 type){memset(p,0,n);W(p,0,type);}
static int stopping(IzContext*c){return __atomic_load_n(&c->stop,__ATOMIC_ACQUIRE)!=0;}
typedef struct {IzAppHeader h;char folder[48];u32 host;} Entry;
typedef struct {char id[48];u32 source;} MenuSelection;
enum { MENU_SD=1,MENU_MIXED=2,MENU_SD_PAGE=3,MENU_MIXED_WITH_SD_PAGE=4 };
#define MENU_SOURCE_AT 0x700d7cu
#define MENU_TILES_AT (1024+IZ_ATLAS_BYTES)
#define MENU_TILES_BYTES (16*IZ_TILE_BYTES)
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
 const u32 observed[]={c->host_display_release,c->host_display_claim,c->host_display_observed,c->host_rpc_claim_ack};for(u32 i=0;i<sizeof observed/sizeof *observed;i++)if(observed[i]>previous)previous=observed[i];
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
  const char*id=w->dirs[w->index++];char path[128];iz_fs_app_path(path,id);u32 file=0;u64 size=0;u32 rc=iz_fs_open(fs,path,&file,&size);
  int backup=0;
  if((rc&0x3fffffu)==0x202&&!fs->c->poisoned&&!fs->c->ram_only){
   rc=iz_fs_open_app_backup(fs,id,&file,&size);
   backup=!rc;
  }
  if(rc){if(fs->c->poisoned||(rc&0x3fffffu)!=0x202)return rc;}
  else{
   IzAppHeader h;int header=size>=sizeof h;
   if(header)rc=iz_fs_read(fs,file,0,&h,sizeof h);
   if(fs->c->poisoned)return rc;
   u32 closed=iz_fs_close(fs,file);if(rc)return rc;if(closed)return closed;
   if(!backup)w->value=signature(w->value,id,size,header?&h:0);
   else if(header&&!iz_validate_header(&h,size)&&!memcmp(h.id,id,48)){
    // Distinguish a recoverable file from a published file with the same
    // bytes. Only the next idle catalog scan may restore its name.
    w->value=signature(w->value,id,size,&h);
    w->value=iz_crc_update(w->value,"backup",6);
   }
  }
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
static u32 read_private_image(IzFs*fs,u32 file,const IzAppHeader*h,u8*mapped){
 const IzSection*initial=&h->section[SEC_PRIVATE];
 fs->c->load_stage=5;
 u32 rc=read_section(fs,file,initial,mapped);
 if(!rc&&initial->bytes<h->private_bytes)
  memset(mapped+initial->bytes,0,h->private_bytes-initial->bytes);
 return rc;
}
static int staged_section(const IzAppHeader*h,unsigned i){
 return i!=SEC_PRIVATE&&i!=SEC_TILE&&h->section[i].bytes;
}
static u32 read_staged(IzFs*fs,u32 file,const IzAppHeader*h,const IzAppHeader*loaded,u8*image){
 for(unsigned i=0;i<SEC_COUNT;){
  if(!staged_section(h,i)){i++;continue;}
  const IzSection*first=&h->section[i];
  u8*destination=image+loaded->section[i].offset;
  unsigned end=i+1;
  u32 length=first->bytes;
  // Read contiguous sections as one span. The filesystem splits it into
  // bounded transfers. Every section still has an independent checksum.
  for(;end<SEC_COUNT;end++){
   if(!staged_section(h,end))continue;
   const IzSection*next=&h->section[end];
   u32 at=next->offset-first->offset;
   if(at!=((length+3u)&~3u)||at!=loaded->section[end].offset-loaded->section[i].offset)break;
   length=at+next->bytes;
  }
  u32 rc;
  if(length==first->bytes)rc=read_section(fs,file,first,destination);
  else{
   rc=iz_fs_read(fs,file,first->offset,destination,length);
   for(unsigned k=i;!rc&&k<end;k++)if(staged_section(h,k)){
    const IzSection*section=&h->section[k];
    if(iz_crc(image+loaded->section[k].offset,section->bytes)!=section->crc32)rc=0xe110;
   }
  }
  if(rc)return rc;
  i=end;
 }
 return 0;
}
/* Both menus use the same 24x32 ASCII cells in the packed focus atlas.
 * Build a neutral tile from the checked header and the local font only. */
static void fallback_tile(u8*tile,const u8*snapshot,const IzAppHeader*h){
 const u8*font=snapshot+1024+65536;
 memset(tile,0,IZ_TILE_BYTES);
 unsigned count=0;
 for(unsigned i=0;i<sizeof h->name&&h->name[i];i++){
  u8 ch=(u8)h->name[i];
  tile[102432+i]=ch>='A'&&ch<='Z'?ch+32:ch<128?ch:'?';
  if(i>=21)continue;
  unsigned glyph=ch>=32&&ch<=126?ch-32:'?'-32;
  unsigned x=glyph%32*24,y=1117+glyph/32*32;
  for(unsigned row=0;row<32;row++)for(unsigned col=0;col<24;col++){
   unsigned pixel=(y+row)*768+x+col;
   tile[row*512+i*24+col]=((font[pixel/4]>>(pixel%4*2))&3)*85;
  }
  count++;
 }
 W(tile,102400,count*24);W(tile,102404,32);
 for(unsigned y=12;y<84;y++)for(unsigned x=19;x<77;x++){
  if(x+y<43)continue;
  u32 color=0xff345372;
  if(x==19||x==76||y==12||y==83)color=0xff6387a7;
  if(y>=68&&y<78&&x>=28&&x<70&&(x-28)%9<5)color=0xffbee0f4;
  W(tile,65536+(y*96+x)*4,color);
 }
}
static MenuSelection menu_selection(IzContext*c,const u8*menu){
 MenuSelection selected={{0},0};u32 index=GET(menu,80);
 if(index<c->catalog_count){memcpy(selected.id,c->catalog[index].id,48);selected.source=c->catalog_source[index];}
 return selected;
}
static u32 publish_catalog(IzFs*fs,Entry*apps,int sd_page,char dirs[][48],u32 count,u32 io_error,int reuse_full_host,const MenuSelection*remembered){
 IzContext*c=fs->c;if(!host_valid(c))return 0xe902;
 MenuSelection selected=remembered?*remembered:menu_selection(c,(void*)(uintptr_t)c->snapshot);
 __atomic_add_fetch(&c->catalog_revision,1,__ATOMIC_RELEASE);c->catalog_count=0;memset(c->catalog,0,sizeof c->catalog);memset(c->catalog_source,0,sizeof c->catalog_source);c->count=0;c->rejected=0;
 u32 rc=0;sort_dirs(dirs,count);u32 fingerprint=0;int overflow=reuse_full_host;
 u8*snapshot=(void*)(uintptr_t)c->snapshot;
 memset(snapshot+MENU_TILES_AT,0,MENU_TILES_BYTES);
 if(c->host_mode&&!sd_page)for(unsigned i=0;i<c->host_count;i++){
  memset(&apps[i],0,sizeof apps[i]);apps[i].host=1;memcpy(apps[i].folder,c->host_catalog[i].id,48);memcpy(&c->catalog[i],&c->host_catalog[i],sizeof c->catalog[i]);c->catalog_source[i]=1;
  u8*tile=snapshot+1024+IZ_ATLAS_BYTES+i*IZ_TILE_BYTES;memcpy(tile,(u8*)(uintptr_t)c->parent_snapshot+1024+IZ_ATLAS_BYTES+i*IZ_TILE_BYTES,IZ_TILE_BYTES);W(tile,102512,0);c->count++;
 }
 for(unsigned i=0;i<count&&!stopping(c);i++){
  char path[128];iz_fs_app_path(path,dirs[i]);u32 file=0;u64 size=0;rc=iz_fs_open(fs,path,&file,&size);
  if((rc&0x3fffffu)==0x202&&!c->poisoned){
   rc=iz_fs_restore_app(fs,dirs[i]);
   if(!rc)rc=iz_fs_open(fs,path,&file,&size);
  }
  if(rc){
   if(c->poisoned)return rc;
   if(!io_error&&(rc&0x3fffffu)!=0x202)io_error=rc;
   continue;
  }
  IzAppHeader h;
  if(size<sizeof h){
   fingerprint=signature(fingerprint,dirs[i],size,0);
   rc=0xe111;
  }else{
   rc=iz_fs_read(fs,file,0,&h,sizeof h);
   if(rc){if(!io_error)io_error=rc;}
   else{fingerprint=signature(fingerprint,dirs[i],size,&h);rc=iz_validate_header(&h,size);}
  }
  if(!rc&&memcmp(h.id,dirs[i],48))rc=0xe112;
  if(!rc&&c->count==IZ_MAX_APPS){
   if(c->host_mode&&!sd_page)overflow=1;
   else rc=0xe113;
  }else if(!rc){u8*tile=snapshot+MENU_TILES_AT+c->count*IZ_TILE_BYTES;rc=read_section(fs,file,&h.section[SEC_TILE],tile);
   if(rc&&rc!=0xe110&&!io_error)io_error=rc;
   // A known tile read failure does not invalidate the executable. Keep
   // its slot usable and let the normal scan retry restore the artwork.
   if(rc&&!c->poisoned&&!(h.flags&IZ_APP_MANAGED)){
    fallback_tile(tile,snapshot,&h);rc=0;
   }
  if(!rc){W(tile,102512,0x44534453u);W(tile,102516,(h.flags&IZ_APP_MANAGED)?2:1);apps[c->count].h=h;apps[c->count].host=(h.flags&IZ_APP_MANAGED)?2:0;c->catalog_source[c->count]=apps[c->count].host;c->catalog_header_crc[c->count]=iz_crc(&h,sizeof h);memcpy(apps[c->count].folder,dirs[i],48);memcpy(c->catalog[c->count].id,h.id,48);memcpy(c->catalog[c->count].name,h.name,64);c->count++;}}
  if(c->poisoned)return rc?rc:0xe001;
  u32 close=iz_fs_close(fs,file);if(close)return close;if(rc){c->rejected++;c->last_app_error=rc;}
 }
 u32 index=0;for(unsigned i=0;i<c->count;i++)if(c->catalog_source[i]==selected.source&&!memcmp(c->catalog[i].id,selected.id,48)){index=i;break;}
 u32 ordinal=0;if(index){u32 exponent=0,value=index;while(value>>1){exponent++;value>>=1;}ordinal=((127+exponent)<<23)|((index-(1u<<exponent))<<(23-exponent));}
 W(snapshot,80,index);W(snapshot,68,ordinal);W(snapshot,112,c->count);W(snapshot,1020,c->count);W(snapshot,116,0);
 W(snapshot,1008,0);W(snapshot,1012,0);W(snapshot,1016,0);W(snapshot,784,0);W(snapshot,800,0);W(snapshot,804,0);W(snapshot,812,0);W(snapshot,836,0);W(snapshot,940,0);W(snapshot,944,0);W(snapshot,948,0);
 if(!c->host_mode){W(snapshot,808,0);W(snapshot,832,0);W(snapshot,840,0);W(snapshot,844,0);}
 W(snapshot,960,0);W(snapshot,964,0);W(snapshot,0x700004,0);
 int has_sd_page=overflow||(c->host_mode&&c->host_count&&c->count>c->host_count);
 W(snapshot,MENU_SOURCE_AT,sd_page?MENU_SD_PAGE:has_sd_page?MENU_MIXED_WITH_SD_PAGE:c->host_mode?MENU_MIXED:MENU_SD);
 if(!reuse_full_host){c->auto_scan_signature=fingerprint;c->auto_scan_error=io_error;c->scan_generation++;}
 c->catalog_count=c->count;c->host_applied=c->host_revision;iz_library_menu(c);iz_menu_changed(c);__atomic_add_fetch(&c->catalog_revision,1,__ATOMIC_RELEASE);
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
static int package_changed(u32 error){return error==0xe111||error==0xe114||error==0xe002||(error&0x3fffffu)==0x202;}
static void record_launch_failure(IzContext*c,const Entry*entry,u32 error){
 u32 sequence=c->sd_launch_failure_sequence+1;
 // The status reader observes both ends of this record in one span.
 __atomic_store_n(&c->sd_launch_failure_committed,0,__ATOMIC_SEQ_CST);
 __atomic_store_n(&c->sd_launch_failure_sequence,0,__ATOMIC_SEQ_CST);
 c->sd_launch_failure_stage=c->load_stage;c->sd_launch_failure_error=error;
 memcpy(c->sd_launch_failure_id,entry->folder,48);
 __atomic_store_n(&c->sd_launch_failure_sequence,sequence?sequence:1,__ATOMIC_RELEASE);
 __atomic_store_n(&c->sd_launch_failure_committed,sequence?sequence:1,__ATOMIC_RELEASE);
}
static void restore_menu(IzContext*c,u8*controls,u32 app_error){
 memcpy(controls,(void*)(uintptr_t)c->snapshot,c->controls_bytes);
 if(app_error){
  u32 message=1;
  if(app_error==0xe904)message=2;
  else if(c->load_stage==1&&package_changed(app_error))message=3;
  else if(app_error==0xe110||app_error==0xe002||(app_error&0x3fffffu)==0x202)message=4;
  W(controls,92,message);
 }
 iz_menu_changed(c);
}
static void record_sd_return(IzContext*c,const Entry*entry){
 u32 sequence=c->sd_return_sequence+1;
 if(!sequence)sequence=1;
 u32 slot=c->sd_return_next;
 IzSdReturn*row=&c->sd_returns[slot];
 __atomic_store_n(&row->committed,0,__ATOMIC_SEQ_CST);
 __atomic_store_n(&row->sequence,0,__ATOMIC_SEQ_CST);
 row->error=c->last_app_error;
 memcpy(row->id,entry->folder,sizeof row->id);
 __atomic_store_n(&row->sequence,sequence,__ATOMIC_RELEASE);
 __atomic_store_n(&row->committed,sequence,__ATOMIC_RELEASE);
 c->sd_return_next=(slot+1)%IZ_SD_RETURN_HISTORY;
 __atomic_store_n(&c->sd_return_sequence,sequence,__ATOMIC_RELEASE);
}
static void initialize_app_controls(IzContext*c,const IzAppHeader*h,const u8*image){
 u8*controls=(void*)(uintptr_t)c->controls;
 u32 initial_bytes=1024+h->section[SEC_DATA].bytes;
 memcpy(controls,image+h->section[SEC_STATE].offset,1024);
 memcpy(controls+1024,image+h->section[SEC_DATA].offset,h->section[SEC_DATA].bytes);
 memset(controls+initial_bytes,0,c->controls_bytes-initial_bytes);
 W(controls,1008,0);W(controls,1012,0);W(controls,1016,0);W(controls,960,0);
}
static u32 launch(IzFs*fs,Entry*entry,unsigned slot,Gpu*g,IzNative*n,IzCheckpointSession*checkpoint){
 IzContext*c=fs->c;c->last_app_error=0;c->load_stage=1;char path[128];iz_fs_app_path(path,entry->folder);u32 file=0;u64 size=0;u32 rc=iz_fs_open(fs,path,&file,&size);
 if(rc){if(!c->poisoned&&package_changed(rc))c->rescan=1;return rc;}
 IzAppHeader h,loaded;memset(&h,0,sizeof h);
 rc=size<sizeof h?0xe111:iz_fs_read(fs,file,0,&h,sizeof h);
 if(!rc&&memcmp(&h,&entry->h,sizeof h))rc=0xe114;
 if(!rc&&h.total_bytes!=size)rc=0xe114;
 if(package_changed(rc))c->rescan=1;
 if(!rc)rc=iz_validate_header(&h,size);
 /* Keep only the small executable/shader/control sections in CPU staging.
  * Large private images go straight from bounded SD reads to their final
  * coherent GPU allocation. Never duplicate a complete game in SDK heap. */
 u64 staging=512,temporary=0;loaded=h;
 loaded.section[SEC_TILE]=(IzSection){0};
 if(!rc)for(unsigned i=0;i<SEC_COUNT;i++)if(staged_section(&h,i)){staging=(staging+3)&~3ull;loaded.section[i].offset=(u32)staging;staging+=h.section[i].bytes;}
 c->staging_bytes=(u32)staging;if(!rc)c->load_stage=2;u8*image=(void*)(uintptr_t)c->arena;
 if(!rc&&staging>c->arena_bytes){u64 capacity=(staging+4095)&~4095ull;rc=(u32)CALL(c,F_ALLOCATE,&temporary,4096,capacity,0,0,0,0,0);if(!rc){if(!temporary||temporary%4096||temporary>=(1ull<<39)||capacity>(1ull<<39)-temporary){c->poisoned=1;rc=0xe115;}else image=(void*)(uintptr_t)temporary;}}
 if(!rc)rc=read_staged(fs,file,&h,&loaded,image);
 if(!rc&&(!g->ready||g->slot!=slot)){destroy(c,g);g->layout=c->menu_layout;g->descriptor=c->menu_descriptor;if(h.private_bytes){c->load_stage=3;rc=private_buffer(c,&h,g);}if(!rc){c->load_stage=4;rc=pipelines(c,&loaded,image,g);}if(!rc)rc=record(c,&h,g);if(rc){if(!c->poisoned)destroy(c,g);}else{g->slot=slot;g->ready=1;}}
 if(!rc){
  initialize_app_controls(c,&loaded,image);
  u8*private_memory=(void*)(uintptr_t)g->mapped;
  c->load_stage=7;
  u32 kind=c->checkpoint_enabled?cp_kind(&h,&loaded,image):0,rules=32;
  const IzSection*program=&loaded.section[SEC_NATIVE_DATA];
  // The exact instruction runtime uses word six for the number of rules.
  // Reject invalid completion bits before applying the candidate's memory.
  if(kind==2&&program->bytes>=64)memcpy(&rules,image+program->offset+6*sizeof(u32),sizeof rules);
  rc=cp_restore(fs,&h,kind,rules,private_memory,checkpoint);
  if(rc&&!c->poisoned){
   // Applying a snapshot can fail after overwriting a prefix. Reinitialize
   // both regions before INIT. Read failures keep the existing banks.
   initialize_app_controls(c,&loaded,image);
   rc=0;
  }
  // A verified snapshot already supplies every byte of private memory.
  if(!rc&&!checkpoint->generation&&private_memory)
   rc=read_private_image(fs,file,&h,private_memory);
 }
 if(!c->poisoned){u32 close=iz_fs_close(fs,file);if(!rc)rc=close;}
 if(!rc&&(h.flags&IZ_APP_NATIVE)){
  c->load_stage=6;u8*private_memory=(void*)(uintptr_t)g->mapped;
  const IzInstructionSaved*saved=checkpoint->kind==2&&checkpoint->generation?&checkpoint->portable:0;
  rc=iz_native_load_saved(fs,&loaded,image,n,private_memory,h.private_bytes,saved);
  if(!rc&&saved)n->app.frames=checkpoint->frames;
  // A verified snapshot can still be rejected by the instruction runtime.
  // Only retry after STOP confirms cleanup. Preserve both checkpoint banks.
  if(rc&&saved&&n->initialized&&!c->poisoned){
   u32 restore_error=rc;
   rc=native_drain(c,n);
   if(!rc){
    checkpoint->save_blocked=1;checkpoint->generation=0;
    c->checkpoint_state=CP_COLD;c->checkpoint_error=restore_error;c->checkpoint_bank=0;
    c->checkpoint_generation=c->checkpoint_stored=c->checkpoint_time_bias=0;
    initialize_app_controls(c,&loaded,image);
    c->load_stage=1;file=0;
    rc=iz_fs_open(fs,path,&file,&size);
    if(!rc){
     IzAppHeader current;
     rc=size!=h.total_bytes?0xe114:iz_fs_read(fs,file,0,&current,sizeof current);
     if(!rc&&memcmp(&current,&h,sizeof h))rc=0xe114;
     if(!rc&&private_memory)rc=read_private_image(fs,file,&h,private_memory);
     if(!c->poisoned){u32 close=iz_fs_close(fs,file);if(!rc)rc=close;}
    }
    if(!c->poisoned&&c->load_stage==1&&package_changed(rc))c->rescan=1;
    if(!rc){c->load_stage=6;rc=iz_native_load(fs,&loaded,image,n,private_memory,h.private_bytes);}
   }
  }
 }
 if(!rc)rc=sdkm_begin(c,&h);
 if(!rc){c->load_stage=8;memcpy(c->last_id,h.id,48);c->active=slot+1;c->transitions++;}
 else if(!c->poisoned){
  // INIT may have succeeded even if SDK setup failed. Stop the app while
  // its private graphics memory is still valid.
  u32 closed=native_drain(c,n);
  if(closed)rc=closed;
  if(!c->poisoned)destroy(c,g);
  c->checkpoint_time_bias=0;
 }
 if(temporary&&!c->poisoned)CALL(c,F_FREE,temporary,0,0,0,0,0,0,0);return rc;
}
static void sdk_input(IzContext*c,u8*controls,const u8 raw[33][64],u32 epoch,unsigned selected,u64 now){
 u8*ext=controls+IZ_SDK_INPUT_AT;
 int compatible=GET(ext,0)==IZ_SDK_INPUT_MAGIC&&GET(ext,4)==IZ_SDK_INPUT_VERSION&&GET(ext,IZ_SDK_EPOCH_AT)==epoch;
 W(ext,0,0);if(!epoch)return;
 /* The same 33 samples already used by native input provide SDK pad data.
  * Auxiliary PC snapshots expire independently; they never replace live pad. */
 u32 flags=compatible?(GET(ext,12)&IZ_SDK_AUX_FLAGS):0;
 u64 sampled=compatible?WG(ext,IZ_SDK_AUX_TICK_AT):0;if(!sampled){sampled=now;Q(ext,IZ_SDK_AUX_TICK_AT,now);}
 if(!compatible||!c->tick_hz||sampled>now||now-sampled>(u64)c->tick_hz*2||(u32)((u32)c->frames-GET(ext,IZ_SDK_HOST_FRAME_AT))>IZ_SDK_AUX_MAX_FRAMES){flags=0;memset(ext+IZ_SDK_AUX_AT,0,IZ_SDK_AUX_BYTES);W(ext,28,0);}
 if(!compatible)W(ext,IZ_SDK_HOST_FRAME_AT,(u32)c->frames-IZ_SDK_AUX_MAX_FRAMES-1u);
 memcpy(ext+IZ_SDK_PAD_AT,controls,64);memcpy(ext+IZ_SDK_SOURCES_AT,raw,5*64);
 memcpy(ext+IZ_SDK_SWEEP_AT,raw+1,IZ_SDK_SWEEP_COUNT*64);
 W(ext,IZ_SDK_SWEEP_HEADER_AT,IZ_SDK_SWEEP_MAGIC);W(ext,IZ_SDK_SWEEP_HEADER_AT+4,1);W(ext,IZ_SDK_SWEEP_HEADER_AT+8,IZ_SDK_SWEEP_COUNT);W(ext,IZ_SDK_SWEEP_HEADER_AT+12,0);
 const u32 styles[]={1,4,8,16};W(ext,20,selected?((selected-1)/4):32);W(ext,24,selected?styles[(selected-1)%4]:2);
 if(c->active&&c->owner.sdk.enabled&&c->owner.sdk.epoch==epoch){c->owner.sdk.pad_id=GET(ext,20);c->owner.sdk.pad_style=GET(ext,24);c->owner.sdk.pad_connected=GET(controls,32)&1u;}
 W(ext,12,flags|IZ_SDK_FLAG_SOURCES|((GET(controls,32)&1)?IZ_SDK_FLAG_PAD:0));
 W(ext,IZ_SDK_EPOCH_AT,epoch);W(ext,IZ_SDK_NATIVE_FRAME_AT,(u32)c->frames);W(ext,4,IZ_SDK_INPUT_VERSION);W(ext,0,IZ_SDK_INPUT_MAGIC);
}
static u32 present_at(IzContext*c,const u64*commands,u64 controls_address,u32 sdk_epoch){
 // Transitions can restore an old snapshot after input. Preserve only data
 // stamped by this native frame for the actual coordinated generation.
 u8*ext=(void*)(uintptr_t)(controls_address+IZ_SDK_INPUT_AT);
 if(!sdk_epoch||GET(ext,0)!=IZ_SDK_INPUT_MAGIC||GET(ext,4)!=IZ_SDK_INPUT_VERSION||GET(ext,IZ_SDK_EPOCH_AT)!=sdk_epoch||GET(ext,IZ_SDK_NATIVE_FRAME_AT)!=(u32)c->frames)W(ext,0,0);
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
static u32 present(IzContext*c,const u64*commands){return present_at(c,commands,c->controls,sdkm_epoch(c));}
static u32 input_at(IzContext*c,u8*controls,u64*previous,u64*first_release,u32 sdk_epoch){
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
 /* The platform shader normalizes all players for game input. Its exit latch
  * must still use the primary player, including when the PC header is stale. */
 W(controls,IZ_HOST_GESTURE_AT+24,IZ_HOST_PRIMARY_MAGIC);W(controls,IZ_HOST_GESTURE_AT+28,(u32)shortcut_buttons);
 sdk_input(c,controls,raw,sdk_epoch,(unsigned)selected,now);
 u64 r3=1u<<5;u32 exit=0;if((shortcut_buttons&r3)&&!(*previous&r3)){if(*first_release&&now-*first_release<c->tick_hz*3/5){exit=1;*first_release=0;}else *first_release=now;}
 *previous=shortcut_buttons;return exit;
}
static u32 input(IzContext*c,u64*previous,u64*first_release){return input_at(c,(void*)(uintptr_t)c->controls,previous,first_release,sdkm_epoch(c));}
static void host_frames_menu(IzContext*c){
 u8*controls=(void*)(uintptr_t)c->controls;memcpy(controls,(void*)(uintptr_t)c->snapshot,c->controls_bytes);
 W(controls,1012,0);W(controls,1016,0);W(controls,940,0);W(controls,944,0);W(controls,960,0);W(controls,964,0);iz_menu_changed(c);
}
typedef struct {u64 layout,descriptor,commands[IZ_MAX_IMAGES];u32 count,revision,applied;u8 catalog[sizeof(((IzContext*)0)->host_catalog)];} HostRpcMenu;
static void host_rpc_backup(IzContext*c,HostRpcMenu*b){b->layout=c->menu_layout;b->descriptor=c->menu_descriptor;memcpy(b->commands,c->menu_commands,sizeof b->commands);b->count=c->host_count;b->revision=c->host_revision;b->applied=c->host_applied;memcpy(b->catalog,c->host_catalog,sizeof b->catalog);}
static void host_rpc_restore(IzContext*c,const HostRpcMenu*b){c->menu_layout=b->layout;c->menu_descriptor=b->descriptor;memcpy(c->menu_commands,b->commands,sizeof b->commands);c->host_count=b->count;c->host_revision=b->revision;c->host_applied=b->applied;memcpy(c->host_catalog,b->catalog,sizeof b->catalog);}
static u32 host_rpc_input(IzContext*c,u64*previous,u64*first){
 u8 raw[5][64];u32 handheld=32,player=0;const unsigned kinds[]={F_PAD_FULLKEY,F_PAD_DUAL,F_PAD_LEFT,F_PAD_RIGHT};memset(raw,0,sizeof raw);CALL(c,F_PAD_HANDHELD,raw[0],&handheld,0,0,0,0,0,0);for(unsigned i=0;i<4;i++)CALL(c,kinds[i],raw[i+1],&player,0,0,0,0,0,0);
 u64 buttons=0;for(unsigned i=0;i<5;i++)if(GET(raw[i],32)&1)buttons|=WG(raw[i],8);u64 now=CALL(c,F_TICK,0,0,0,0,0,0,0,0),r3=1u<<5;u32 exit=0;
 if((buttons&r3)&&!(*previous&r3)){if(*first&&now-*first<c->tick_hz*3/5){exit=1;*first=0;}else *first=now;}*previous=buttons;return exit;
}
u64 iz_run(IzContext*c){
 if(!c||c->magic!=0x44535a49||c->version!=1)return 0xe900;
 IzFs*fs=&c->owner.fs;IzNative*native=&c->owner.native;
 if(c->state||fs->c||native->fs||native->source||native->mapped||native->data||native->image_state||native->worker_state)return 0xe930;
 memset(&c->owner,0,sizeof c->owner);fs->c=c;fs->buffer=(void*)(uintptr_t)c->scratch;c->state=1;
 if(c->probe==1){c->probe_result=iz_fs_probe(fs);c->state=c->probe_result?4:3;return c->probe_result;}
 if(c->probe==4){
  if(!c->controls_buffer||!c->menu_layout){c->state=4;return c->probe_result=0xe901;}
  char path[128];iz_fs_app_path(path,c->last_id);u32 file=0;u64 size=0;u32 rc=iz_fs_open(fs,path,&file,&size);IzAppHeader h;Gpu gpu;memset(&gpu,0,sizeof gpu);
  if(!rc)rc=iz_fs_read(fs,file,0,&h,sizeof h);if(!rc)rc=iz_validate_header(&h,size);if(!rc&&memcmp(h.id,c->last_id,48))rc=0xe112;
  if(!rc){c->load_stage=3;rc=private_buffer(c,&h,&gpu);}
  if(!rc){rc=read_private_image(fs,file,&h,(void*)(uintptr_t)gpu.mapped);if(!rc)c->probe_crc=h.section[SEC_PRIVATE].crc32;}
  if(file&&!c->poisoned){u32 close=iz_fs_close(fs,file);if(!rc)rc=close;}if(!c->poisoned)destroy(c,&gpu);c->probe_result=rc;c->state=rc?4:3;return rc;
 }
 if(c->probe==3){
  char path[128];iz_fs_app_path(path,c->last_id);u32 file=0;u64 size=0;u32 rc=iz_fs_open(fs,path,&file,&size);IzAppHeader h;
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
  u32 file=0;u64 size=0;u32 rc=iz_fs_open(fs,IZ_SD_ROOT "/sd-proof-native/launcher.iza",&file,&size);u8*image=(void*)(uintptr_t)c->arena;
  if(!rc&&(size<512||size>c->arena_bytes))rc=0xe115;if(!rc)rc=iz_fs_read(fs,file,0,image,(u32)size);
  if(file&&!c->poisoned){u32 close=iz_fs_close(fs,file);if(!rc)rc=close;}
  IzAppHeader*h=(void*)image;if(!rc)rc=iz_validate_header(h,size);if(!rc&&!(h->flags&IZ_APP_NATIVE))rc=0xe407;
  for(unsigned i=0;!rc&&i<SEC_COUNT;i++)if(h->section[i].bytes&&iz_crc(image+h->section[i].offset,h->section[i].bytes)!=h->section[i].crc32)rc=0xe110;
  if(!rc)rc=iz_native_load(fs,h,image,native,0,0);c->active=!rc;
  while(!rc&&!stopping(c)&&!GET((void*)(uintptr_t)c->controls,1016)&&c->frames<600){native->app.frames++;rc=iz_native_event(native,IZ_NATIVE_FRAME);c->frames++;CALL(c,F_SLEEP,16666667,0,0,0,0,0,0,0);}
  if(!c->poisoned){u32 close=native_drain(c,native);if(!rc)rc=close;}c->active=0;c->probe_result=rc;c->state=rc?4:3;return rc;
 }
 if(!c->controls||c->controls_bytes!=IZ_SHARED_BYTES||!c->image_count||c->image_count>IZ_MAX_IMAGES){c->state=4;return c->error=0xe901;}
 Entry apps[IZ_MAX_APPS];Gpu gpu;IzCheckpointSession checkpoint;IzHostFrameOwner host_frames;IzHostRpcMapper rpc_mapper;memset(&rpc_mapper,0,sizeof rpc_mapper);memset(&host_frames,0,sizeof host_frames);memset(&checkpoint,0,sizeof checkpoint);memset(&gpu,0,sizeof gpu);
 IzHostRpcOwner*rpc_owner=0;if(c->host_rpc_owner){
  if(!c->host_mode||!hr_span(c,c->host_rpc_owner,4096)||!hr_span(c,c->host_rpc_staging,4096)||c->host_rpc_owner!=c->supervisor_base+IZ_HOST_RPC_OWNER_OFFSET||c->host_rpc_staging!=c->supervisor_base+IZ_HOST_RPC_STAGING_OFFSET){c->state=4;return c->error=0xe965;}
  if(c->host_rpc_scratch&&(c->host_rpc_scratch<4096||c->host_rpc_scratch%16||c->host_rpc_scratch>(1ull<<39)-0x80000)){c->state=4;return c->error=0xe965;}
  rpc_owner=(void*)(uintptr_t)c->host_rpc_owner;hr_initialize(rpc_owner);rpc_owner->sdk_scratch=c->host_rpc_scratch;rpc_owner->protected_base=c->supervisor_base;rpc_owner->protected_bytes=c->supervisor_bytes;
 }
 if(c->parent_snapshot){u8*parent=(void*)(uintptr_t)c->parent_snapshot;memcpy(parent,(void*)(uintptr_t)c->controls,c->controls_bytes);W(parent,1008,0);W(parent,1012,0);W(parent,1016,0);}
 memcpy((void*)(uintptr_t)c->snapshot,(void*)(uintptr_t)c->controls,c->controls_bytes);
 int sd_page=0,requested_page=0;
 MenuSelection remembered[2]={0};
 char dirs[IZ_MAX_DIRS][48];u32 dir_count=0;
 u32 listing=iz_fs_dirs(fs,dirs,&dir_count);
 int directories_valid=!listing;
 if(listing&&(c->poisoned||c->ram_only)){c->state=4;return c->error=listing;}
 u32 rc=publish_catalog(fs,apps,sd_page,dirs,listing?0:dir_count,listing,0,0);
 if(rc){c->state=4;return c->error=rc;}
 u64 retry_scan_at=0;
 if(c->auto_scan_error&&!c->ram_only){
  c->rescan=1;
  retry_scan_at=CALL(c,F_TICK,0,0,0,0,0,0,0,0)+c->tick_hz*5;
 }
 memcpy((void*)(uintptr_t)c->controls,(void*)(uintptr_t)c->snapshot,c->controls_bytes);
 c->state=2;u64 previous=0,first_release=0,next_scan=0,offer_frame=0;u32 offer_token=0,completed_release=0;InventoryWatch watch;memset(&watch,0,sizeof watch);
 while(!stopping(c)){
  if(c->host_mode){
   if(rpc_owner)hr_guarded_claim(c,rpc_owner);
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
    HostRpcMenu rpc_menu;u64 rpc_previous=0,rpc_first=0;u32 rpc_polls=0,rpc_local_return=0,rpc_return_reason=IZ_HOST_RETURN_USER,rpc_cancel_error=0;if(rpc_owner)host_rpc_backup(c,&rpc_menu);
    hf_pause(c,&host_frames);destroy(c,&gpu);
    W((void*)(uintptr_t)c->controls,1012,c->host_launch);W((void*)(uintptr_t)c->controls,944,c->host_remove);
    memcpy((void*)(uintptr_t)c->snapshot,(void*)(uintptr_t)c->controls,1024);
    if(sd_page)memcpy(menu+MENU_TILES_AT,(const u8*)(uintptr_t)c->parent_snapshot+MENU_TILES_AT,MENU_TILES_BYTES);
    __atomic_store_n(&c->host_display_ready,request,__ATOMIC_RELEASE);
    while(!stopping(c)){
     if(rpc_owner&&rpc_owner->claim_epoch==request&&__atomic_load_n(&c->host_frames_cancel,__ATOMIC_ACQUIRE)==request){rpc_cancel_error=c->host_frames_error;if(hr_epoch(rpc_owner)<request)iz_host_rpc_mapper_cancel(c,rpc_owner,&rpc_mapper,request);else hr_revoke(rpc_owner,request);c->host_rpc_revoked=request;rpc_return_reason=IZ_HOST_RETURN_PC;}
     if(rpc_owner){iz_host_rpc_mapper_step(c,rpc_owner,&rpc_mapper,request);if((rpc_mapper.phase||rpc_owner->claim_epoch==request)&&hr_epoch(rpc_owner)!=request&&((rpc_polls++&15u)==0)&&host_rpc_input(c,&rpc_previous,&rpc_first))iz_host_rpc_mapper_cancel(c,rpc_owner,&rpc_mapper,request);hr_offer(c,rpc_owner,request);}
     int guarded=rpc_owner&&hr_epoch(rpc_owner)==request,release=request==__atomic_load_n(&c->host_display_release,__ATOMIC_ACQUIRE),rendered=0;
     int coordinated=guarded&&rpc_owner->graphics_mode;
     if(!coordinated)hf_offer(c,&host_frames,request);
     if(guarded){
      u32 graphics_error=__atomic_load_n(&rpc_owner->graphics_fault,__ATOMIC_ACQUIRE);if(graphics_error){c->poisoned=1;rc=graphics_error;break;}
      if(release){hr_revoke(rpc_owner,request);c->host_rpc_revoked=request;}
      int input_done=0;
      if(coordinated&&!release&&!hr_revoked(rpc_owner)&&(host_frames.ticket||host_frames.pending_ticket||__atomic_load_n(&c->host_frames_offer,__ATOMIC_ACQUIRE)==request)&&hr_graphics_native_begin(rpc_owner,request)){
       /* Frame-plan reads and app input writes share the same private mutex
        * as SDK mutations. A public offer alone cannot race a live producer. */
       hf_offer(c,&host_frames,request);hf_resume(c,&host_frames,request);
       if(host_frames.ticket&&__atomic_load_n(&c->host_frames_cancel,__ATOMIC_ACQUIRE)==host_frames.ticket)hf_return(c,&host_frames,IZ_HOST_RETURN_PC);
       if(host_frames.ticket){
        u8*app_controls=(void*)(uintptr_t)host_frames.live.controls;u32 exit=input_at(c,app_controls,&rpc_previous,&rpc_first,request),requested=GET(app_controls,1016);input_done=1;rpc_polls=0;
        if(exit||requested){rpc_return_reason=exit?IZ_HOST_RETURN_USER:IZ_HOST_RETURN_APP;hr_revoke(rpc_owner,request);c->host_rpc_revoked=request;}
        else{
         rc=present_at(c,host_frames.live.commands,host_frames.live.controls,request);
         if(!rc){c->frames++;rendered=1;
          /* Observe an exit after QueueWaitIdle, before a waiting SDK call
           * receives graphics. The same edge history spans frames and waits. */
          exit=host_rpc_input(c,&rpc_previous,&rpc_first);requested=GET(app_controls,1016);
          if(exit||requested){rpc_return_reason=exit?IZ_HOST_RETURN_USER:IZ_HOST_RETURN_APP;hr_revoke(rpc_owner,request);c->host_rpc_revoked=request;}
         }
        }
       }
       rc=hr_graphics_native_end(rpc_owner,rc);if(rc){c->poisoned=1;break;}
      }
      if(!release&&!input_done&&((rpc_polls++&15u)==0)&&host_rpc_input(c,&rpc_previous,&rpc_first)){hr_revoke(rpc_owner,request);c->host_rpc_revoked=request;}
      hr_status(c,rpc_owner);
      if(release&&hr_idle(rpc_owner,request))break;
      if(!release&&hr_revoked(rpc_owner)&&hr_local_return(rpc_owner,request)){
       u32 idle=VK(c,F_QUEUE_IDLE,c->queue,0,0,0,0,0,0,0);if(idle){c->poisoned=1;rc=idle;break;}
       host_rpc_restore(c,&rpc_menu);if(!host_frames.ticket&&host_frames.pending_ticket){host_frames.ticket=host_frames.pending_ticket;host_frames.pending_ticket=0;}hf_return(c,&host_frames,rpc_return_reason);host_frames_menu(c);if(rpc_cancel_error)c->last_app_error=rpc_cancel_error;if(rpc_cancel_error||(rpc_owner->map_epoch==request&&rpc_owner->map_error&&rpc_owner->map_state!=IZ_RPC_MAP_CANCELLED)){W((void*)(uintptr_t)c->controls,92,1);iz_menu_changed(c);}c->transitions++;rpc_local_return=1;
       __atomic_store_n(&c->host_rpc_returned,request,__ATOMIC_RELEASE);__atomic_store_n(&c->host_display_release,request,__ATOMIC_RELEASE);break;
      }
     }else if(release)break;
     if(rendered)continue;
     CALL(c,F_SLEEP,1000000,0,0,0,0,0,0,0);
    }
    if(rpc_owner&&hr_epoch(rpc_owner)==request){hr_revoke(rpc_owner,request);hr_status(c,rpc_owner);if(!hr_idle(rpc_owner,request)){c->poisoned=1;if(!rc)rc=0xe966;}else if(__atomic_load_n(&rpc_owner->opaque,__ATOMIC_ACQUIRE)){c->poisoned=1;if(!rc)rc=0xe967;}}
    if(rc)break;
    __atomic_store_n(&c->host_display_ready,0,__ATOMIC_RELEASE);if(stopping(c))break;
    completed_release=request;
    c->host_launch=c->host_remove=0;previous=first_release=0;
    if(c->host_applied!=c->host_revision){
     u32 selected=GET((void*)(uintptr_t)c->snapshot,80);memcpy((void*)(uintptr_t)c->snapshot,(void*)(uintptr_t)c->parent_snapshot,c->controls_bytes);W((void*)(uintptr_t)c->snapshot,80,selected);
     memcpy((void*)(uintptr_t)c->controls,(void*)(uintptr_t)c->snapshot,c->controls_bytes);c->rescan=1;
    }else if(sd_page)memcpy(menu+MENU_TILES_AT,(const u8*)(uintptr_t)c->snapshot+MENU_TILES_AT,MENU_TILES_BYTES);
    if(!rpc_local_return)hf_resume(c,&host_frames,request);
    if(host_frames.ticket){W((void*)(uintptr_t)c->controls,1012,0);W((void*)(uintptr_t)c->snapshot,1012,0);}
   }
  }
  if(host_frames.ticket){
   u8*app_controls=(void*)(uintptr_t)host_frames.live.controls;
   u32 exit=input_at(c,app_controls,&previous,&first_release,0),requested=GET(app_controls,1016);
   if(exit||requested){
    hf_return(c,&host_frames,exit?IZ_HOST_RETURN_USER:IZ_HOST_RETURN_APP);host_frames_menu(c);previous=first_release=0;c->transitions++;
   }else{
    rc=present_at(c,host_frames.live.commands,host_frames.live.controls,0);if(rc){c->poisoned=1;break;}c->frames++;continue;
   }
  }
  u8*controls=(void*)(uintptr_t)c->controls;u64 old_buttons=previous;u32 exit=input(c,&previous,&first_release);
  if(c->host_mode&&!c->active&&(c->host_launch||c->host_remove)&&(WG(controls,8)&~old_buttons&2u)){
   host_cancel(c);Q(controls,8,WG(controls,8)&~2ull);iz_menu_changed(c);
  }
  if(c->active&&GET(controls,1008)==IZ_NATIVE_CONNECTION_REQUEST){W(controls,1008,0);if(c->recovery_wake)__atomic_add_fetch((u32*)(uintptr_t)c->recovery_wake,1,__ATOMIC_RELEASE);}
  if(c->active&&(exit||GET(controls,1016)||native->stopping||c->owner.sdk.ending)){
   u32 sdk_stopped=sdkm_stop(c);if(sdk_stopped){if(sdk_stopped!=SDKM_STOP_PENDING||c->owner.sdk.retained){c->last_app_error=sdk_stopped;c->owner.sdk.ending=0;W(controls,1016,0);}}else{
   if(!checkpoint.ending){checkpoint.ending=1;if(!c->last_app_error){u32 saved=cp_save(fs,&apps[c->active-1].h,(void*)(uintptr_t)gpu.mapped,native,&checkpoint);if(c->poisoned){rc=saved?saved:0xe001;break;}}}
   rc=iz_native_unload(native);
   if(rc==IZ_NATIVE_STOP_PENDING)rc=0;
   else{
    if(rc)break;
    if(c->last_app_error)destroy(c,&gpu);
    restore_menu(c,controls,c->last_app_error);
    record_sd_return(c,&apps[c->active-1]);
    c->active=0;c->checkpoint_time_bias=0;c->transitions++;first_release=0;
   }}}
  u32 hold=__atomic_load_n(&c->host_hold_request,__ATOMIC_ACQUIRE);int held=hold&&hold!=__atomic_load_n(&c->host_hold_release,__ATOMIC_ACQUIRE);
  if(!c->active&&held)__atomic_store_n(&c->host_hold_ready,hold,__ATOMIC_RELEASE);
  if(!held)__atomic_store_n(&c->host_hold_ready,0,__ATOMIC_RELEASE);
  if(!c->active&&!held){
   u32 menu_source=GET((void*)(uintptr_t)c->snapshot,MENU_SOURCE_AT);
   int page_changed=0;
   if(menu_source>=MENU_SD_PAGE&&(previous&~old_buttons&(1u<<9))&&
      !GET(controls,0x700004)&&!GET(controls,800)&&!GET(controls,92)&&!GET(controls,884)&&
      !GET(controls,940)&&!GET(controls,944)&&!GET(controls,964)&&!GET(controls,1012)){
    host_cancel(c);
    requested_page=!requested_page;
    page_changed=1;
   }
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
   u32 request=GET(controls,1012);
   // The fade and the request both refer to the currently published slots.
   // Keep them intact until launch completes or a PC offer is resolved.
   int launching=request||GET(controls,964)||c->host_launch;
   u64 now=WG(controls,0x700d70);if(!next_scan)next_scan=now+c->tick_hz*5;
   int refresh=GET(controls,104)!=GET((void*)(uintptr_t)c->snapshot,104);
   int idle_menu=!GET(controls,0x700004)&&!GET(controls,800)&&!GET(controls,940)&&!GET(controls,944);
   if(!launching&&idle_menu&&now>=next_scan&&!refresh&&!page_changed&&!__atomic_load_n(&c->rescan,__ATOMIC_ACQUIRE)){
    int done=0;u32 check=inventory_step(fs,&watch,&done);
    if(check||done)c->auto_scan_error=check;
    if(c->poisoned){rc=check?check:0xe001;break;}
    if(check||done){watch.active=0;next_scan=now+c->tick_hz*5;}
    if(!check&&done&&watch.value!=c->auto_scan_signature)c->rescan=1;
   }
   int host_changed=c->host_applied!=c->host_revision;
   int reuse_directories=0;
   if(page_changed){
    int current=!host_changed&&!refresh&&!c->auto_scan_error&&!launching&&idle_menu&&!__atomic_load_n(&c->rescan,__ATOMIC_ACQUIRE);
    // The full mixed page consists only of retained PC tiles. Its SD
    // overflow marker comes from the card page we already have.
    if(current&&sd_page&&!requested_page&&c->host_count==IZ_MAX_APPS&&c->count){
     remembered[sd_page]=menu_selection(c,controls);
     memcpy((void*)(uintptr_t)c->snapshot,controls,1024);
     sd_page=0;rc=publish_catalog(fs,apps,sd_page,dirs,0,0,1,&remembered[sd_page]);if(rc)break;
     destroy(c,&gpu);memcpy(controls,(void*)(uintptr_t)c->snapshot,c->controls_bytes);
    }else if(current&&directories_valid&&!sd_page&&requested_page)reuse_directories=1;
    else __atomic_store_n(&c->rescan,1,__ATOMIC_RELEASE);
   }
   int scan_allowed=host_changed||(idle_menu&&now>=retry_scan_at);
   int rescan=!launching&&scan_allowed&&__atomic_exchange_n(&c->rescan,0,__ATOMIC_ACQ_REL);
   if(!launching&&scan_allowed&&(rescan||refresh||reuse_directories)){
    if(c->host_mode&&(c->host_launch||c->host_remove))host_cancel(c);
    u32 listing=0;
    // Reopen the known packages for current headers and tiles, but leave the
    // automatic directory watch on its original schedule to find new folders.
    if(!reuse_directories||rescan){watch.active=0;next_scan=now+c->tick_hz*5;listing=iz_fs_dirs(fs,dirs,&dir_count);directories_valid=!listing;}
    if(c->poisoned){rc=listing?listing:0xe001;break;}
    // A known listing rejection leaves the previous slots usable. A new PC
    // catalog must still be acknowledged with its current, PC-only slots.
    if(!listing||host_changed){
     // Keep the idle Vulkan objects only if the same SD package is still
     // published. Launch will independently verify the file and its sections.
     IzAppHeader cached;
     if(gpu.ready)cached=apps[gpu.slot].h;
     memcpy((void*)(uintptr_t)c->snapshot,controls,1024);
     int next_page=listing?0:requested_page;
     int changing_page=next_page!=sd_page;
     if(changing_page)remembered[sd_page]=menu_selection(c,controls);
     sd_page=next_page;
     rc=publish_catalog(fs,apps,sd_page,dirs,listing?0:dir_count,listing,0,changing_page?&remembered[sd_page]:0);
     if(rc)break;
     if(gpu.ready){
      u32 slot=0;
      for(;slot<c->count;slot++)if(!apps[slot].host&&!memcmp(&apps[slot].h,&cached,sizeof cached))break;
      if(slot==c->count)destroy(c,&gpu);
      else gpu.slot=slot;
     }
     memcpy(controls,(void*)(uintptr_t)c->snapshot,c->controls_bytes);
    }else c->auto_scan_error=listing;
    // A matching header cannot prove that artwork with a previous I/O
    // error has recovered. Retry the tile read even if the header matches.
    int retry=c->auto_scan_error&&!c->ram_only;
    retry_scan_at=retry?now+c->tick_hz*5:0;
    if(retry)__atomic_store_n(&c->rescan,1,__ATOMIC_RELEASE);
   }
   if(request){
    if(c->host_mode&&request<=c->count&&apps[request-1].host)host_request(c,request,0);
    else{
     if(c->host_mode)host_cancel(c);
     W(controls,1012,0);
     if(request<=c->count){
      memcpy((void*)(uintptr_t)c->snapshot,controls,1024);
      W((void*)(uintptr_t)c->snapshot,92,0);W((void*)(uintptr_t)c->snapshot,960,0);W((void*)(uintptr_t)c->snapshot,964,0);W((void*)(uintptr_t)c->snapshot,1016,0);
      Entry*entry=&apps[request-1];
      if(entry->host==2){
       c->load_stage=0;
       rc=0xe904;
      }else rc=launch(fs,entry,request-1,&gpu,native,&checkpoint);
      if(rc){
       c->last_app_error=rc;record_launch_failure(c,entry,rc);if(c->poisoned)break;
       restore_menu(c,controls,rc);
       rc=0;
      }else if(GET(controls,1016)){
       // INIT requested exit. Let the shutdown path run before any app
       // frame, and keep the previous checkpoint.
       checkpoint.save_blocked=1;
       continue;
      }
     }
    }
   }
  }
  if(c->active&&native->initialized&&!native->stopping&&!c->owner.sdk.ending){native->app.frames++;rc=iz_native_event(native,IZ_NATIVE_FRAME);if(rc){c->last_app_error=rc;if(c->poisoned)break;W(controls,1016,1);rc=0;}}
  if(c->active&&native->presentation_owned)CALL(c,F_SLEEP,16666667,0,0,0,0,0,0,0);
  else{rc=present(c,c->active?gpu.commands:c->menu_commands);if(rc){c->poisoned=1;break;}if(c->active)sdkm_frame(c);}c->frames++;
 }
 {u32 sdk_stopped=sdkm_shutdown(c);if(sdk_stopped){c->poisoned=1;if(!rc)rc=sdk_stopped;}}
 if(rc){c->error=rc;c->state=4;return rc;}
 if(host_frames.ticket){hf_return(c,&host_frames,IZ_HOST_RETURN_STOP);host_frames_menu(c);}
 if(c->active&&!checkpoint.ending&&!c->last_app_error){u32 saved=cp_save(fs,&apps[c->active-1].h,(void*)(uintptr_t)gpu.mapped,native,&checkpoint);if(c->poisoned){c->error=saved?saved:0xe001;c->state=4;return c->error;}}
 rc=native_drain(c,native);if(rc){c->error=rc;c->state=4;return rc;}destroy(c,&gpu);memcpy((void*)(uintptr_t)c->controls,(void*)(uintptr_t)(c->parent_snapshot?c->parent_snapshot:c->snapshot),c->controls_bytes);c->active=0;c->state=3;return 0;
}
