#include "memory-card.h"
#include "fs-file.h"
#include "platform.h"
#include "sd-layout.h"
#include <stdlib.h>
#include <string.h>

#define HEADER 64u
#define IMAGE_BYTES (HEADER+IZPSX_CARD_BYTES)
#define BAD_CARD 0xf101u
typedef struct {uint8_t*data;uint64_t generation,hash,last_write;unsigned active,dirty,pending;} Card;
static Card cards[2];
static Ps1SdDirectory *directory;
static uint64_t saves;
static unsigned save_delay=500;
void ps1_cards_set_delay(unsigned ms){save_delay=ms<=2000?ms:500;}
static const char *paths[2][2]={
 {IZPSX_SD_SAVES "/card1.a.izmc",IZPSX_SD_SAVES "/card1.b.izmc"},
 {IZPSX_SD_SAVES "/card2.a.izmc",IZPSX_SD_SAVES "/card2.b.izmc"}
};
static uint64_t hash(const void*data,size_t bytes){const uint8_t*p=data;uint64_t h=1469598103934665603ull;while(bytes--){h^=*p++;h*=1099511628211ull;}return h;}
static uint64_t get64(const uint8_t*p){uint64_t n;memcpy(&n,p,8);return n;}
static void put64(uint8_t*p,uint64_t n){memcpy(p,&n,8);}
static int missing(uint32_t rc){return (rc&0x3fffffu)==0x202u;}
static Ps1FileIO reader(void){Ps1FileIO io={.send=(uintptr_t)directory->send,.buffer=(uintptr_t)directory->buffer,.capacity=directory->capacity};return io;}
static uint32_t read_image(uint32_t handle,uint8_t*out){Ps1FileIO io=reader();ps1_ifile_reset();return ps1_ifile_read(&io,handle,0,out,IMAGE_BYTES,IMAGE_BYTES);}
static int valid(const uint8_t*p,unsigned slot){return !memcmp(p,"IZPSXMC1",8)&&get64(p+8)==slot+1&&get64(p+16)>0&&get64(p+24)==IZPSX_CARD_BYTES&&get64(p+32)==hash(p+HEADER,IZPSX_CARD_BYTES)&&get64(p+40)==hash(p,40)&&p[HEADER]=='M'&&p[HEADER+1]=='C';}
static uint32_t close_file(uint32_t h,uint32_t prior){uint32_t rc=ps1_sd_closefile(directory,h);ps1_ifile_reset();return prior?prior:rc;}
static uint32_t commit(unsigned slot,uint8_t*image){
 Card*c=cards+slot;if(c->generation==UINT64_MAX)return BAD_CARD;
 unsigned target=c->generation?c->active^1u:0;uint32_t h=0;uint64_t size=0;
 uint32_t rc=ps1_sd_openfile_mode(directory,IZPSX_SD_SAVES,paths[slot][target],3,&h,&size);
 if(missing(rc)){
  rc=ps1_sd_createfile(directory,IZPSX_SD_SAVES,paths[slot][target],IMAGE_BYTES);if(rc)return rc;
  rc=ps1_sd_openfile_mode(directory,IZPSX_SD_SAVES,paths[slot][target],3,&h,&size);
 }
 if(rc)return rc;
 if(size!=IMAGE_BYTES){rc=ps1_sd_resizefile(directory,h,IMAGE_BYTES);if(rc)return close_file(h,rc);}
 /* Persist an invalid header before replacing the inactive payload. A torn
  * write cannot gain priority over the untouched previous generation. */
 memset(image,0,HEADER);rc=ps1_sd_writefile(directory,h,0,image,HEADER);
 if(!rc)rc=ps1_sd_flushfile(directory,h);
 if(!rc)rc=ps1_sd_writefile(directory,h,HEADER,c->data,IZPSX_CARD_BYTES);
 if(!rc)rc=ps1_sd_flushfile(directory,h);
 uint64_t digest=hash(c->data,IZPSX_CARD_BYTES);
 memcpy(image,"IZPSXMC1",8);put64(image+8,slot+1);put64(image+16,c->generation+1);put64(image+24,IZPSX_CARD_BYTES);put64(image+32,digest);put64(image+40,hash(image,40));
 if(!rc)rc=ps1_sd_writefile(directory,h,0,image,HEADER);
 if(!rc)rc=ps1_sd_flushfile(directory,h);
 if(!rc)rc=read_image(h,image);
 if(!rc&&(!valid(image,slot)||get64(image+16)!=c->generation+1||get64(image+32)!=digest||memcmp(image+HEADER,c->data,IZPSX_CARD_BYTES)))rc=BAD_CARD;
 rc=close_file(h,rc);if(rc)return rc;
 rc=ps1_sd_commit(directory);if(rc)return rc;
 c->generation++;c->hash=digest;c->active=target;c->dirty=c->pending=0;saves++;return 0;
}
uint32_t ps1_cards_open(Ps1SdDirectory*sd,void*card1,void*card2){
 directory=sd;saves=0;memset(cards,0,sizeof cards);
 if(!sd||!card1||!card2||card1==card2)return PS1_SD_INVALID;
 uint32_t rc=ps1_sd_mkdir(sd,IZPSX_SD_ROOT,IZPSX_SD_SAVES);
 if(rc&&(rc&0x3fffffu)!=0x402u)return rc; /* PathAlreadyExists */
 rc=0;
 uint8_t*image=malloc(IMAGE_BYTES);if(!image)return PS1_SD_LIMIT;
 cards[0].data=card1;cards[1].data=card2;
 for(unsigned slot=0;slot<2&&!rc;slot++){
  unsigned damaged=0;Card*c=cards+slot;
  for(unsigned copy=0;copy<2&&!rc;copy++){
   uint32_t h=0;uint64_t size=0;rc=ps1_sd_openfile(sd,IZPSX_SD_SAVES,paths[slot][copy],&h,&size);
   if(missing(rc)){rc=0;continue;}if(rc)break;
   if(size!=IMAGE_BYTES){damaged++;rc=close_file(h,0);continue;}
   rc=read_image(h,image);rc=close_file(h,rc);if(rc)break;
   if(!valid(image,slot)){damaged++;continue;}
   uint64_t generation=get64(image+16);
   if(generation==c->generation&&c->generation&&get64(image+32)!=c->hash){rc=BAD_CARD;break;}
   if(generation>c->generation){memcpy(c->data,image+HEADER,IZPSX_CARD_BYTES);c->generation=generation;c->hash=get64(image+32);c->active=copy;}
  }
  if(!rc&&!c->generation){if(damaged)rc=BAD_CARD;else rc=commit(slot,image);}
 }
 free(image);if(rc)directory=NULL;else sd->error=sd->transport=0;return rc;
}
void ps1_card_mark(const void*data){if(!directory)return;for(unsigned i=0;i<2;i++)if(data==cards[i].data)cards[i].dirty=1;}
uint32_t ps1_cards_poll(uint64_t now,uint64_t hz,int force){
 if(!directory)return 0;if(!hz)return PS1_SD_INVALID;
 uint8_t*image=NULL;uint32_t rc=0;
 for(unsigned i=0;i<2&&!rc;i++){
  Card*c=cards+i;if(c->dirty){c->dirty=0;c->pending=1;c->last_write=now;}
  if(!c->pending||(!force&&now-c->last_write<(hz/1000)*save_delay+(hz%1000)*save_delay/1000))continue;
  if(hash(c->data,IZPSX_CARD_BYTES)==c->hash){c->pending=0;continue;}
  if(!image){image=malloc(IMAGE_BYTES);if(!image){rc=PS1_SD_LIMIT;break;}}
  rc=commit(i,image);
 }free(image);return rc;
}
uint64_t ps1_cards_saved(void){return saves;}
