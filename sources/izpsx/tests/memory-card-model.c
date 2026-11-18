#define _GNU_SOURCE
#include "memory-card.h"
#include "fs-file.h"
#include "sd-layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
typedef struct {char path[769];uint8_t*data;size_t size;unsigned opened;} File;
static File files[4];static unsigned count,open_count,writes,flushes,reads;
static uint8_t*scratch;static int fail_write,fail_flush,short_read,bad_readback,close_fault;
static unsigned checks;
#ifdef IZPSX_SB4_STORAGE
#include <sb4/native-app.h>
static IzNativeApp7Storage model_app;
static File *model_file(IzNativeApp *app,uint32_t token){
 assert(app==(void*)&model_app&&token>=100&&token<100+count&&files[token-100].opened);
 return files+(token-100);
}
static void model_path(char physical[769],const char*name){
 int n=snprintf(physical,769,"%s/%s",IZPSX_SD_ROOT,name);assert(n>0&&n<769);
 assert(ps1_sd_path_valid(IZPSX_SD_ROOT,physical));
}
static uint32_t model_open(IzNativeApp *app,const char*name,uint32_t mode,uint32_t*token,uint64_t*size){
 assert(app==(void*)&model_app&&(mode==1||mode==3));
 char physical[769];model_path(physical,name);
 for(unsigned i=0;i<count;i++)if(!strcmp(physical,files[i].path)){
  ++files[i].opened;++open_count;*token=100+i;*size=files[i].size;return 0;
 }
 return 0x202;
}
static uint32_t model_read(IzNativeApp*app,uint32_t token,uint64_t at,void*out,uint32_t bytes,uint32_t*actual){
 File*f=model_file(app,token);assert(out==scratch&&bytes<=65536&&at+bytes<=f->size);
 ++reads;
 memcpy(out,f->data+at,bytes);
 if(bad_readback&&at==0)((uint8_t*)out)[40]^=1;
 *actual=short_read?bytes-1:bytes;
 return 0;
}
static uint32_t model_write(IzNativeApp*app,uint32_t token,uint64_t at,const void*data,uint32_t bytes){
 File*f=model_file(app,token);assert(bytes<=65536&&at+bytes<=f->size);
 if(++writes==(unsigned)fail_write)return 0xbeef;
 memcpy(f->data+at,data,bytes);return 0;
}
static uint32_t model_size(IzNativeApp*app,uint32_t token,uint64_t*size){*size=model_file(app,token)->size;return 0;}
static uint32_t model_resize(IzNativeApp*app,uint32_t token,uint64_t size){
 File*f=model_file(app,token);
 f->data=realloc(f->data,size);assert(f->data);f->size=size;return 0;
}
static uint32_t model_flush(IzNativeApp*app,uint32_t token){model_file(app,token);++flushes;return fail_flush?0xdead:0;}
static uint32_t model_close(IzNativeApp*app,uint32_t token){
 File*f=model_file(app,token);if(close_fault)return 0xfafa;--f->opened;--open_count;return 0;
}
static uint32_t model_create(IzNativeApp*app,const char*name,uint64_t size){
 assert(app==(void*)&model_app&&count<4);
 File*f=files+count++;model_path(f->path,name);f->data=calloc(1,size);assert(f->data);f->size=size;return 0;
}
static uint32_t model_mkdir(IzNativeApp*app,const char*name){assert(app==(void*)&model_app&&!strcmp(name,"saves"));return 0x402;}
static uint32_t model_commit(IzNativeApp*app){assert(app==(void*)&model_app);return 0;}
static uint32_t list_unused(IzNativeApp*a,const char*p,Sb4StorageEntry*e,uint32_t c,uint32_t*n){assert(a&&p&&e&&c&&n);return 1;}
static uint32_t stat_unused(IzNativeApp*a,const char*p,uint64_t*n){assert(a&&p&&n);return 1;}
static Sb4CFunction resolve_c(const char *name){(void)name;return NULL;}
static IzServiceFunction resolve_service(uint32_t mod,uint32_t id){(void)mod;(void)id;return NULL;}
static uint32_t allocate(void **out,size_t alignment,size_t bytes){(void)out;(void)alignment;(void)bytes;return 1;}
static uint64_t ticks(void){return 1;}
static void sleep_ns(uint64_t ns){(void)ns;}
static Ps1SdDirectory setup_sd(void){
 static const Sb4Compat api={SB4_COMPAT_VERSION,sizeof(Sb4Compat),SB4_COMPAT_LIBOUNCE,0,
  resolve_c,resolve_service,allocate,free,ticks,ticks,sleep_ns};
 static const Sb4StorageFiles ops={SB4_STORAGE_VERSION,sizeof(Sb4StorageFiles),
  model_open,model_read,model_write,model_size,model_resize,model_flush,model_close,model_create,model_mkdir,model_commit};
 static const Sb4Storage storage={SB4_STORAGE_VERSION,sizeof(Sb4Storage),list_unused,stat_unused,&ops};
 IzNativeApp*app=(void*)&model_app;
 app->magic=0x414e5a49;app->version=SB4_NATIVE_ABI;app->bytes=sizeof model_app;
 model_app.base.sb4=&api;model_app.storage=&storage;
 return (Ps1SdDirectory){.app=app,.buffer=scratch,.capacity=65536};
}
#else
static void response(void*m,uint32_t rc,uint64_t value,uint32_t handle){
 memset(m,0,4096);uint32_t*p=m;if(handle){p[1]=0x80000008;p[2]=1u<<5;p[3]=handle;}p[4]=0x4f434653;p[6]=rc;memcpy((uint8_t*)m+32,&value,8);
}
static uint32_t send(void*m,uint64_t bytes,uint32_t handle){
 assert(bytes==4096&&!((uintptr_t)m&4095));uint32_t*p=m;unsigned raw=((p[0]>>20)&255)?32:16;uint32_t cmd=p[raw/4+2];assert(p[raw/4]==0x49434653);
 if(handle==42){
  if(cmd==10){assert(p[0]==4&&p[1]==8);response(m,0,0,0);return 0;}
  assert(p[0]==(4u|(1u<<16)));uintptr_t at=p[3]|(uint64_t)((p[2]>>12)&15)<<32|(uint64_t)((p[2]>>6)&7)<<36;assert(at==(uintptr_t)scratch&&(p[2]>>16)==769);const char*path=(void*)at;
  if(!ps1_sd_path_valid(
#ifdef IZPSX_SETTINGS_MODEL
  IZPSX_SD_ROOT,
#else
  IZPSX_SD_SAVES,
#endif
  path)){response(m,0x202,0,0);return 0;}
  if(cmd==2){assert(!strcmp(path,IZPSX_SD_SAVES));response(m,0x402,0,0);return 0;}
  unsigned i;for(i=0;i<count;i++)if(!strcmp(path,files[i].path))break;
  if(cmd==0){assert(i==count&&count<4&&p[1]==12);uint64_t size;memcpy(&size,(uint8_t*)m+40,8);File*f=files+count++;strcpy(f->path,path);f->data=calloc(1,size);assert(f->data);f->size=size;response(m,0,0,0);return 0;}
  assert(cmd==8&&(p[8]==1||p[8]==3));if(i==count){response(m,0x202,0,0);return 0;}files[i].opened++;open_count++;response(m,0,0,100+i);return 0;
 }
 assert(handle>=100&&handle<100+count);File*f=files+(handle-100);assert(f->opened);
 if(cmd==4){response(m,0,f->size,0);return 0;}
 if(cmd==3){uint64_t n;memcpy(&n,(uint8_t*)m+32,8);f->data=realloc(f->data,n);assert(f->data);f->size=n;response(m,0,0,0);return 0;}
 if(cmd==2){flushes++;response(m,fail_flush?0xdead:0,0,0);return 0;}
 assert(cmd<=1&&raw==32&&p[1]==14);uint64_t offset,size;memcpy(&offset,(uint8_t*)m+56,8);memcpy(&size,(uint8_t*)m+64,8);
 uintptr_t at=p[3]|(uint64_t)((p[4]>>28)&15)<<32|(uint64_t)((p[4]>>2)&7)<<36;assert(at==(uintptr_t)scratch&&size==p[2]&&size<=65536&&offset+size<=f->size&&(p[4]&3)==1);
 if(cmd==1){assert(p[0]==(4u|(1u<<20)));writes++;if(fail_write&&writes==(unsigned)fail_write){response(m,0xbeef,0,0);return 0;}memcpy(f->data+offset,scratch,size);response(m,0,0,0);}
 else{assert(p[0]==(4u|(1u<<24)));reads++;memcpy(scratch,f->data+offset,size);if(bad_readback&&offset==0)scratch[40]^=1;response(m,0,short_read?size-1:size,0);}return 0;
}
static uint32_t close_handle(uint32_t h){assert(h>=100&&h<100+count&&files[h-100].opened);if(close_fault)return 0xfafa;files[h-100].opened--;open_count--;return 0;}
static Ps1SdDirectory setup_sd(void){return (Ps1SdDirectory){.send=send,.close_handle=close_handle,.filesystem=42,.buffer=scratch,.capacity=65536};}
#endif
static uint8_t card1[IZPSX_CARD_BYTES],card2[IZPSX_CARD_BYTES];
static void blank(void){memset(card1,0,sizeof card1);memset(card2,0,sizeof card2);card1[0]=card2[0]='M';card1[1]=card2[1]='C';}
static void check(int ok){if(!ok)fprintf(stderr,"failed check %u; files=%u opened=%u writes=%u flushes=%u\n",checks+1,count,open_count,writes,flushes);assert(ok);checks++;}
#ifdef IZPSX_CARD_MODEL_LIBRARY
unsigned izpsx_model_card_writes(void){return writes;}
Ps1SdDirectory* izpsx_model_directory(void){
 static Ps1SdDirectory sd;
 scratch=mmap((void*)0x60020000,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);assert(scratch==(void*)0x60020000);
 sd=setup_sd();return &sd;
}
#else
int main(void){
 scratch=mmap(0,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);assert(scratch!=MAP_FAILED);
 Ps1SdDirectory sd=setup_sd();
 blank();check(!ps1_cards_open(&sd,card1,card2)&&count==2&&!open_count&&ps1_cards_saved()==2);
 unsigned before=writes;card1[8123]=17;ps1_card_mark(card1);check(!ps1_cards_poll(1000,1000,0)&&writes==before);check(!ps1_cards_poll(1499,1000,0)&&writes==before);check(!ps1_cards_poll(1500,1000,0)&&writes>before&&count==3&&!open_count);
 blank();check(!ps1_cards_open(&sd,card1,card2)&&card1[8123]==17&&!ps1_cards_saved());
 card2[1234]=89;ps1_card_mark(card2);check(!ps1_cards_poll(2000,1000,1)&&count==4&&!open_count);
 blank();check(!ps1_cards_open(&sd,card1,card2)&&card1[8123]==17&&card2[1234]==89);
 before=writes;ps1_card_mark(card1);check(!ps1_cards_poll(3000,1000,1)&&writes==before);
 /* Tear each individual write of an update. Reload must select exactly the
  * previous verified card. Header invalidation, payload and commit covered. */
 for(unsigned phase=1;phase<=4;phase++){
  card1[8123]=42;ps1_card_mark(card1);fail_write=writes+phase;check(ps1_cards_poll(4000,1000,1)!=0&&!open_count);fail_write=0;
  blank();check(!ps1_cards_open(&sd,card1,card2)&&card1[8123]==17&&card2[1234]==89);
 }
 card1[8123]=43;ps1_card_mark(card1);fail_flush=1;check(ps1_cards_poll(5000,1000,1)!=0&&!open_count);fail_flush=0;blank();check(!ps1_cards_open(&sd,card1,card2)&&card1[8123]==17);
 /* Commit a new generation then damage its payload. The older one survives. */
 card1[8123]=44;ps1_card_mark(card1);check(!ps1_cards_poll(6000,1000,1));files[0].data[64+8123]^=1;blank();check(!ps1_cards_open(&sd,card1,card2)&&card1[8123]==17);
 card1[8123]=45;ps1_card_mark(card1);bad_readback=1;check(ps1_cards_poll(7000,1000,1)!=0&&!open_count);bad_readback=0;
 blank();check(!ps1_cards_open(&sd,card1,card2)&&card1[8123]==45);
 short_read=1;check(ps1_cards_open(&sd,card1,card2)!=0&&!open_count);short_read=0;
 blank();check(!ps1_cards_open(&sd,card1,card2));
 close_fault=1;check(ps1_cards_open(&sd,card1,card2)!=0&&sd.leaked_handle&&open_count==1);close_fault=0;check(!ps1_sd_closefile(&sd,sd.leaked_handle)&&!open_count);
 /* Both corrupt is an explicit failure, never a silent card format. */
 files[0].data[0]^=1;files[2].data[0]^=1;before=writes;check(ps1_cards_open(&sd,card1,card2)!=0&&writes==before&&!open_count);
 check(ps1_sd_createfile(&sd,IZPSX_SD_SAVES,IZPSX_SD_SAVES "/../escape",64)==PS1_SD_INVALID);
 for(unsigned i=0;i<count;i++)free(files[i].data);munmap(scratch,65536);
 printf("{\"passed\":true,\"checks\":%u,\"writes\":%u,\"flushes\":%u,\"reads\":%u,\"consoleExecuted\":false}\n",checks,writes,flushes,reads);return 0;
}
#endif
