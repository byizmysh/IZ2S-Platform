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
 assert(handle>=100&&handle<100+count);File*f=files+handle-100;assert(f->opened);
 if(cmd==4){response(m,0,f->size,0);return 0;}
 if(cmd==3){uint64_t n;memcpy(&n,(uint8_t*)m+32,8);f->data=realloc(f->data,n);assert(f->data);f->size=n;response(m,0,0,0);return 0;}
 if(cmd==2){flushes++;response(m,fail_flush?0xdead:0,0,0);return 0;}
 assert(cmd<=1&&raw==32&&p[1]==14);uint64_t offset,size;memcpy(&offset,(uint8_t*)m+56,8);memcpy(&size,(uint8_t*)m+64,8);
 uintptr_t at=p[3]|(uint64_t)((p[4]>>28)&15)<<32|(uint64_t)((p[4]>>2)&7)<<36;assert(at==(uintptr_t)scratch&&size==p[2]&&size<=65536&&offset+size<=f->size&&(p[4]&3)==1);
 if(cmd==1){assert(p[0]==(4u|(1u<<20)));writes++;if(fail_write&&writes==(unsigned)fail_write){response(m,0xbeef,0,0);return 0;}memcpy(f->data+offset,scratch,size);response(m,0,0,0);}
 else{assert(p[0]==(4u|(1u<<24)));reads++;memcpy(scratch,f->data+offset,size);if(bad_readback&&offset==0)scratch[40]^=1;response(m,0,short_read?size-1:size,0);}return 0;
}
static uint32_t close_handle(uint32_t h){assert(h>=100&&h<100+count&&files[h-100].opened);if(close_fault)return 0xfafa;files[h-100].opened--;open_count--;return 0;}
static uint8_t card1[IZPSX_CARD_BYTES],card2[IZPSX_CARD_BYTES];
static void blank(void){memset(card1,0,sizeof card1);memset(card2,0,sizeof card2);card1[0]=card2[0]='M';card1[1]=card2[1]='C';}
static void check(int ok){if(!ok)fprintf(stderr,"failed check %u; files=%u opened=%u writes=%u flushes=%u\n",checks+1,count,open_count,writes,flushes);assert(ok);checks++;}
#ifdef IZPSX_CARD_MODEL_LIBRARY
unsigned izpsx_model_card_writes(void){return writes;}
Ps1SdDirectory* izpsx_model_directory(void){
 static Ps1SdDirectory sd;
 scratch=mmap((void*)0x60020000,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);assert(scratch==(void*)0x60020000);
 sd=(Ps1SdDirectory){.send=send,.close_handle=close_handle,.filesystem=42,.buffer=scratch,.capacity=65536};return &sd;
}
#else
int main(void){
 scratch=mmap(0,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);assert(scratch!=MAP_FAILED);
 Ps1SdDirectory sd={.send=send,.close_handle=close_handle,.filesystem=42,.buffer=scratch,.capacity=65536};
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
