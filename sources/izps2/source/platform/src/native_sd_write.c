#include "native_sd_write.h"
#include <string.h>
#include <limits.h>
_Static_assert(sizeof(Ps2SdWriteConfig)==64,"SD write ABI");
#define PAGE 4096u
#define CACHE 65536u
enum { RetryRequestBytes = 72, FsResultModule = 2, FsResultModuleMask = 0x1ff,
       FsPathNotFound = 0x202, FsPathAlreadyExists = 0x402 };
static int handle(uint32_t h){return h&&h<0x80000000u;}
static int known(Ps2SdWrite*s,uint64_t h){if(h>UINT32_MAX||!handle((uint32_t)h)||h==s->filesystem)return 0;for(unsigned i=0;i<32;i++)if(s->files[i]==h)return 1;return 0;}
static uint32_t bad(Ps2SdWrite*s,uint32_t rc){s->error=rc;s->poisoned=1;return rc;}
static int valid(Ps2SdWrite*s){return s&&s->ready&&!s->poisoned;}
static int path(const char*p,const char**out,unsigned command){
 if(!p)return 0;if(!strncmp(p,"ps2sd:",6))p+=6;
#ifdef PS2_NATIVE_SPIRV_CACHE
 /* A separate optional cache root needs explicit admission; the filesystem
  * capability copy alone never widens the existing saves-only path policy.
  * No cache rename, arbitrary child, parent traversal or directory creation. */
 if((command==7&&!strcmp(p,"/izmysh/ps2/cache"))||
    ((command==0||command==8)&&(!strcmp(p,"/izmysh/ps2/cache/spirv.a.ps2cache")||
                              !strcmp(p,"/izmysh/ps2/cache/spirv.b.ps2cache")))){*out=p;return 1;}
#else
 (void)command;
#endif
 const char*root="/izmysh/ps2/saves";size_t n=strnlen(p,0x301),r=strlen(root);
 if(n<r||n>=0x301||strncmp(p,root,r)||(p[r]&&p[r]!='/')||p[n-1]=='/')return 0;
 for(size_t i=1;i<n;){size_t b=i;while(i<n&&p[i]!='/'){unsigned c=(unsigned char)p[i++];if(c<32||c==127||c=='\\'||c==':')return 0;}
  size_t k=i-b;if(!k||(k==1&&p[b]=='.')||(k==2&&p[b]=='.'&&p[b+1]=='.'))return 0;i++;}
 *out=p;return 1;
}
int ps2_sd_write_initialize(Ps2SdWrite*s,const Ps2SdWriteConfig*c,Ps2SdSend send,Ps2SdClose close){
 if(!s||!c||!send||!close||s->ready||c->magic!=0x32575350||c->version!=1||c->size!=64||c->flags!=1||c->reserved||
  !handle(c->filesystem)||!c->buffer||(c->buffer&4095)||c->buffer>=(1ull<<39)-CACHE||c->capacity!=CACHE||(!c->token[0]&&!c->token[1]))return PS2_SD_WRITE_INVALID;
 memset(s,0,sizeof(*s));s->send=send;s->close=close;s->buffer=(void*)(uintptr_t)c->buffer;s->filesystem=c->filesystem;s->ready=1;return 0;
}
static uint32_t response(Ps2SdWrite*s,uint8_t*m,int opening,uint64_t*out,int*retryable){
 uint32_t*p=(void*)m;unsigned at=8,copies=0,moves=0;uint32_t received[30];
 if(p[1]&0x80000000u){uint32_t special=p[2];copies=(special>>1)&15;moves=(special>>5)&15;at+=4;if(special&1)at+=8;
  for(unsigned i=0;i<copies+moves;i++){memcpy(&received[i],m+at,4);at+=4;}}
 at+=((p[0]>>16)&15)*8+(((p[0]>>20)&15)+((p[0]>>24)&15)+((p[0]>>28)&15))*12;at=(at+15)&~15u;
 uint32_t rc=at>512||p[1]%1024<4||*(uint32_t*)(m+at)!=0x4f434653?PS2_SD_WRITE_PACKET:*(uint32_t*)(m+at+8);
 const int shape=opening?copies==0&&moves==1&&handle(received[0])&&received[0]!=s->filesystem:copies==0&&moves==0;
 if(rc||!shape){
  for(unsigned i=0;i<copies+moves;i++)if(handle(received[i])&&received[i]!=s->filesystem&&!known(s,received[i])){
   if(s->close(received[i])){s->leaked=received[i];bad(s,PS2_SD_WRITE_RETAINED);}}
  if(!rc||rc==PS2_SD_WRITE_PACKET)return bad(s,PS2_SD_WRITE_PACKET);
  s->error=rc;
  *retryable=!copies&&!moves&&(rc&FsResultModuleMask)==FsResultModule&&
      rc!=FsPathNotFound&&rc!=FsPathAlreadyExists;
  return rc;
 }
 if(opening){
  if(known(s,received[0]))return bad(s,PS2_SD_WRITE_PACKET);
  for(unsigned i=0;i<32;i++)if(!s->files[i]){s->files[i]=received[0];s->opened++;*out=received[0];return 0;}
  if(s->close(received[0])){s->leaked=received[0];return bad(s,PS2_SD_WRITE_RETAINED);}return PS2_SD_WRITE_INVALID;
 }
 if(out)memcpy(out,m+at+16,8);return 0;
}
static uint32_t send(Ps2SdWrite*s,uint32_t h,uint8_t*m,int opening,uint64_t*out,int retry){
 // The transport replaces the request in place. Save the request header and
 // arguments for one replay after a well-formed FS error with no handles.
 uint8_t request[RetryRequestBytes];
 if(retry)memcpy(request,m,sizeof request);
 for(unsigned attempt=0;;++attempt){
  uint32_t rc=s->send(m,PAGE,h);
  if(rc){s->transport=rc;return bad(s,rc);}
  int retryable=0;
  rc=response(s,m,opening,out,&retryable);
  if(!retry||attempt||!retryable||s->poisoned)return rc;
  memset(m,0,PAGE);
  memcpy(m,request,sizeof request);
 }
}
static void pointer(uint32_t*p,uintptr_t a){p[0]=(0x301u<<16)|((uint32_t)(a>>36)&7u)<<6|((uint32_t)(a>>32)&15u)<<12;p[1]=(uint32_t)a;}
static uint32_t path_call(Ps2SdWrite*s,unsigned command,const char*first,const char*second,const void*input,size_t n,int opening,uint64_t*out,int retry){
 if(!valid(s))return PS2_SD_WRITE_RETAINED;const char*a,*b=0;
 if(!path(first,&a,command)||(second&&!path(second,&b,command))||n>16)return PS2_SD_WRITE_INVALID;
 memset(s->buffer,0,0x800);memcpy(s->buffer,a,strlen(a)+1);if(b)memcpy(s->buffer+0x400,b,strlen(b)+1);
 _Alignas(PAGE) uint8_t message[PAGE];memset(message,0,sizeof message);uint32_t*p=(void*)message;
 unsigned count=b?2:1,raw=(8+count*8+15)&~15u;p[0]=4u|(count<<16);p[1]=(32u+(unsigned)n+3u)/4u;
 pointer(p+2,(uintptr_t)s->buffer);if(b)pointer(p+4,(uintptr_t)s->buffer+0x400);
 *(uint32_t*)(message+raw)=0x49434653;*(uint32_t*)(message+raw+8)=command;if(n)memcpy(message+raw+16,input,n);
 return send(s,s->filesystem,message,opening,out,retry);
}
uint32_t ps2_sd_write_open(Ps2SdWrite*s,uint64_t*out,const char*p,int mode){
 if(!out)return PS2_SD_WRITE_INVALID;*out=0;if(mode<1||mode>3)return PS2_SD_WRITE_INVALID;uint32_t m=(uint32_t)mode;return path_call(s,8,p,0,&m,4,1,out,(mode&2)!=0);
}
void ps2_sd_write_close(Ps2SdWrite*s,uint64_t h){
 if(!s||!s->ready||!known(s,h)){if(s)bad(s,PS2_SD_WRITE_INVALID);return;}
 const uint32_t rc=s->close((uint32_t)h);if(rc){s->leaked=(uint32_t)h;bad(s,rc);return;}
 for(unsigned i=0;i<32;i++)if(s->files[i]==h){s->files[i]=0;s->closed++;break;}
}
static uint32_t file_call(Ps2SdWrite*s,uint64_t h,unsigned command,uint64_t value,uint64_t*out){
 if(!valid(s))return PS2_SD_WRITE_RETAINED;if(!known(s,h))return PS2_SD_WRITE_INVALID;
 _Alignas(PAGE) uint8_t message[PAGE];memset(message,0,sizeof message);uint32_t*p=(void*)message;
 p[0]=4;p[1]=command==3?10:8;p[4]=0x49434653;p[6]=command;if(command==3)memcpy(message+32,&value,8);
 return send(s,(uint32_t)h,message,0,out,command==2||command==3);
}
uint32_t ps2_sd_write_size(Ps2SdWrite*s,int64_t*out,uint64_t h){
 if(!out)return PS2_SD_WRITE_INVALID;uint64_t n=0;uint32_t rc=file_call(s,h,4,0,&n);if(!rc&&n>INT64_MAX)return bad(s,PS2_SD_WRITE_PACKET);if(!rc)*out=(int64_t)n;return rc;
}
uint32_t ps2_sd_write_flush(Ps2SdWrite*s,uint64_t h){return file_call(s,h,2,0,0);}
uint32_t ps2_sd_write_resize(Ps2SdWrite*s,uint64_t h,int64_t n){return n<0?PS2_SD_WRITE_INVALID:file_call(s,h,3,(uint64_t)n,0);}
static uint32_t transfer(Ps2SdWrite*s,uint64_t h,int64_t offset,void*data,size_t bytes,size_t*actual,int writing){
 if(actual)*actual=0;if(!valid(s))return PS2_SD_WRITE_RETAINED;
 if(!known(s,h)||offset<0||bytes>INT64_MAX-(uint64_t)offset||(!data&&bytes))return PS2_SD_WRITE_INVALID;
 for(size_t done=0;done<bytes;){size_t n=bytes-done;if(n>CACHE)n=CACHE;
  if(writing)memcpy(s->buffer,(uint8_t*)data+done,n);
  _Alignas(PAGE) uint8_t message[PAGE];memset(message,0,sizeof message);uint32_t*p=(void*)message;uintptr_t a=(uintptr_t)s->buffer;
  p[0]=4u|(writing?1u<<20:1u<<24);p[1]=14;p[2]=(uint32_t)n;p[3]=(uint32_t)a;
  p[4]=1u|((uint32_t)(a>>36)&7u)<<2|((uint32_t)(a>>32)&15u)<<28;p[8]=0x49434653;p[10]=writing?1:0;
  uint64_t in[]={writing?1u:0u,(uint64_t)offset+done,n};memcpy(message+48,in,sizeof in);uint64_t got=0;
  // Write replays the same bytes at the same offset with WriteOption::Flush.
  uint32_t rc=send(s,(uint32_t)h,message,0,&got,writing);if(rc)return rc;
  if(!writing){if(got>n)return bad(s,PS2_SD_WRITE_PACKET);memcpy((uint8_t*)data+done,s->buffer,(size_t)got);s->read_bytes+=got;done+=(size_t)got;
   if(actual)*actual=done;if(got<n)return 0;
  }else{s->written_bytes+=n;done+=n;}
 }return 0;
}
uint32_t ps2_sd_write_read(Ps2SdWrite*s,size_t*out,uint64_t h,int64_t off,void*p,size_t n){if(!out)return PS2_SD_WRITE_INVALID;return transfer(s,h,off,p,n,out,0);}
uint32_t ps2_sd_write_write(Ps2SdWrite*s,uint64_t h,int64_t off,const void*p,size_t n,const uint32_t*option){
 if(!option||*option!=1)return PS2_SD_WRITE_INVALID;return transfer(s,h,off,(void*)p,n,0,1);
}
uint32_t ps2_sd_write_create(Ps2SdWrite*s,const char*p,int64_t n,int option){
 if(n<0||option)return PS2_SD_WRITE_INVALID;uint64_t in[]={0,(uint64_t)n};return path_call(s,0,p,0,in,16,0,0,0);
}
uint32_t ps2_sd_write_type(Ps2SdWrite*s,uint32_t*out,const char*p){
 if(!out)return PS2_SD_WRITE_INVALID;uint64_t kind=0;uint32_t rc=path_call(s,7,p,0,0,0,0,&kind,0);if(!rc&&(uint32_t)kind>1)return bad(s,PS2_SD_WRITE_PACKET);if(!rc)*out=(uint32_t)kind;return rc;
}
uint32_t ps2_sd_write_rename(Ps2SdWrite*s,const char*a,const char*b){return path_call(s,5,a,b,0,0,0,0,0);}
int ps2_sd_write_healthy(const Ps2SdWrite*s){return s&&s->ready&&!s->poisoned&&!s->leaked;}
int ps2_sd_write_shutdown(Ps2SdWrite*s){
 if(!ps2_sd_write_healthy(s)||s->opened!=s->closed)return 0;for(unsigned i=0;i<32;i++)if(s->files[i])return 0;
 memset(s,0,sizeof(*s));return 1; /* The borrowed filesystem stays with the launcher. */
}
