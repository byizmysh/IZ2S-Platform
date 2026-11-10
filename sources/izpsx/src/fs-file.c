#include "fs-file.h"
#include <string.h>

static int valid_handle(uint32_t h){return h&&h<0x80000000u;}
static int valid_context(const Ps1SdDirectory *s){return s&&s->send&&s->close_handle&&valid_handle(s->filesystem)&&s->buffer&&!((uintptr_t)s->buffer&4095)&&s->capacity>=0x310&&s->capacity<=65536&&(uint64_t)(uintptr_t)s->buffer<(1ull<<39)&&s->capacity<=(1ull<<39)-(uint64_t)(uintptr_t)s->buffer;}
uint32_t ps1_sd_closefile(Ps1SdDirectory *s,uint32_t handle){
 if(!s||!s->close_handle||!valid_handle(handle)||handle==s->filesystem)return PS1_SD_INVALID;
 uint32_t rc=s->close_handle(handle);
 if(rc){s->leaked_handle=handle;if(!s->error)s->error=rc;return rc;}
 if(s->leaked_handle==handle)s->leaked_handle=0;
 return 0;
}

/* Decode the complete HIPC header and dispose of unexpected transferred
 * capabilities. Only successful OpenFile may return one moved IFile handle. */
static uint32_t file_reply(Ps1SdDirectory *s,uint8_t *m,int open,uint32_t *handle,uint64_t *size){
 uint32_t *p=(void*)m,received[30];unsigned at=8,copies=0,moves=0;
 if(p[1]&0x80000000u){uint32_t special=p[2];copies=(special>>1)&15;moves=(special>>5)&15;at+=4;if(special&1)at+=8;for(unsigned i=0;i<copies+moves;i++){received[i]=*(uint32_t*)(m+at);at+=4;}}
 at+=((p[0]>>16)&15)*8+(((p[0]>>20)&15)+((p[0]>>24)&15)+((p[0]>>28)&15))*12;at=(at+15)&~15u;
 uint32_t rc=at>512||*(uint32_t*)(m+at)!=0x4f434653?PS1_SD_PACKET:*(uint32_t*)(m+at+8);
 int valid=open?copies==0&&moves==1&&valid_handle(received[0])&&received[0]!=s->filesystem:copies==0&&moves==0;
 if(!rc&&!valid)rc=PS1_SD_PACKET;
 if(rc){for(unsigned i=0;i<copies+moves;i++)if(valid_handle(received[i])&&received[i]!=s->filesystem&&(open||received[i]!=*handle))ps1_sd_closefile(s,received[i]);return rc;}
 if(open)*handle=received[0];else if(size){uint64_t n=*(uint64_t*)(m+at+16);if(n>INT64_MAX)return PS1_SD_PACKET;*size=n;}
 return 0;
}
uint32_t ps1_sd_openfile_mode(Ps1SdDirectory *s,const char *root,const char *path,uint32_t mode,uint32_t *handle,uint64_t *size){
 if(handle)*handle=0;
 if(size)*size=0;
 if(!handle||!size||!valid_context(s)||(mode!=1&&mode!=3)||!ps1_sd_path_valid(root,path))return PS1_SD_INVALID;
 if(s->leaked_handle)return PS1_SD_CLOSE;
 s->error=s->transport=0;
 /* Separate path scratch from IFile read-ahead memory, or invalidate the
  * caller's read cache before invoking this operation. */
 size_t path_bytes=strlen(path)+1;memmove(s->buffer,path,path_bytes);memset((uint8_t*)s->buffer+path_bytes,0,PS1_SD_PATH_MAX-path_bytes);
 _Alignas(4096) uint8_t message[4096];memset(message,0,sizeof message);uint32_t *p=(void*)message;uintptr_t a=(uintptr_t)s->buffer;
 p[0]=4u|(1u<<16);p[1]=9;p[2]=(0x301u<<16)|((uint32_t)(a>>36)&7u)<<6|((uint32_t)(a>>32)&15u)<<12;p[3]=(uint32_t)a;p[4]=0x49434653;p[6]=8;p[8]=mode;
 s->calls++;uint32_t rc=s->send(message,sizeof message,s->filesystem),opened=0;uint64_t length=0;
 if(rc){s->transport=rc;return s->error=rc;}
 rc=file_reply(s,message,1,&opened,&length);if(rc)return s->error=rc;
 memset(message,0,sizeof message);p[0]=4;p[1]=8;p[4]=0x49434653;p[6]=4;s->calls++;rc=s->send(message,sizeof message,opened);
 if(rc)s->transport=rc;else rc=file_reply(s,message,0,&opened,&length);
 if(rc){s->error=rc;ps1_sd_closefile(s,opened);return rc;}
 *handle=opened;*size=length;return 0;
}
uint32_t ps1_sd_openfile(Ps1SdDirectory*s,const char*root,const char*path,uint32_t*h,uint64_t*n){return ps1_sd_openfile_mode(s,root,path,1,h,n);}
/* Mutations are restricted to the caller's validated subtree. No mount,
 * implicit root creation, host pump or borrowed-handle closure occurs. */
static uint32_t result_only(Ps1SdDirectory*s,uint8_t*m,uint32_t handle){
 s->calls++;uint32_t rc=s->send(m,4096,handle);if(rc){s->transport=rc;return s->error=rc;}
 rc=file_reply(s,m,0,&handle,NULL);return s->error=rc;
}
static uint32_t path_mutation(Ps1SdDirectory*s,const char*root,const char*path,uint32_t command,uint64_t size){
 if(!valid_context(s)||!ps1_sd_path_valid(root,path)||size>INT64_MAX)return PS1_SD_INVALID;
 if(s->leaked_handle)return PS1_SD_CLOSE;
 size_t n=strlen(path)+1;memmove(s->buffer,path,n);memset((uint8_t*)s->buffer+n,0,PS1_SD_PATH_MAX-n);
 _Alignas(4096) uint8_t m[4096];memset(m,0,sizeof m);uint32_t*p=(void*)m;uintptr_t a=(uintptr_t)s->buffer;
 p[0]=4u|(1u<<16);p[1]=command==0?12:8;p[2]=(0x301u<<16)|((uint32_t)(a>>36)&7u)<<6|((uint32_t)(a>>32)&15u)<<12;p[3]=(uint32_t)a;p[4]=0x49434653;p[6]=command;
 if(command==0)memcpy(m+40,&size,8);
 return result_only(s,m,s->filesystem);
}
uint32_t ps1_sd_createfile(Ps1SdDirectory*s,const char*r,const char*p,uint64_t n){return path_mutation(s,r,p,0,n);}
uint32_t ps1_sd_mkdir(Ps1SdDirectory*s,const char*r,const char*p){return path_mutation(s,r,p,2,0);}
uint32_t ps1_sd_writefile(Ps1SdDirectory*s,uint32_t h,uint64_t offset,const void*data,size_t bytes){
 if(!valid_context(s)||!valid_handle(h)||h==s->filesystem||!data||offset>INT64_MAX||bytes>INT64_MAX-offset)return PS1_SD_INVALID;
 if(s->leaked_handle)return PS1_SD_CLOSE;
 const uint8_t*src=data;
 while(bytes){size_t n=bytes<s->capacity?bytes:s->capacity;memmove(s->buffer,src,n);
  _Alignas(4096) uint8_t m[4096];memset(m,0,sizeof m);uint32_t*p=(void*)m;uint64_t a=(uintptr_t)s->buffer;
  p[0]=4u|(1u<<20);p[1]=14;p[2]=(uint32_t)n;p[3]=(uint32_t)a;p[4]=1u|(uint32_t)(a>>36)<<2|(uint32_t)((a>>32)&15)<<28;p[8]=0x49434653;p[10]=1;
  memcpy(m+56,&offset,8);uint64_t count=n;memcpy(m+64,&count,8);
  uint32_t rc=result_only(s,m,h);if(rc)return rc;src+=n;offset+=n;bytes-=n;
 }return 0;
}
uint32_t ps1_sd_flushfile(Ps1SdDirectory*s,uint32_t h){
 if(!valid_context(s)||!valid_handle(h)||h==s->filesystem)return PS1_SD_INVALID;
 if(s->leaked_handle)return PS1_SD_CLOSE;
 _Alignas(4096) uint8_t m[4096];memset(m,0,sizeof m);uint32_t*p=(void*)m;p[0]=4;p[1]=8;p[4]=0x49434653;p[6]=2;return result_only(s,m,h);
}
uint32_t ps1_sd_resizefile(Ps1SdDirectory*s,uint32_t h,uint64_t size){
 if(!valid_context(s)||!valid_handle(h)||h==s->filesystem||size>INT64_MAX)return PS1_SD_INVALID;
 if(s->leaked_handle)return PS1_SD_CLOSE;
 _Alignas(4096) uint8_t m[4096];memset(m,0,sizeof m);uint32_t*p=(void*)m;p[0]=4;p[1]=10;p[4]=0x49434653;p[6]=3;memcpy(m+32,&size,8);return result_only(s,m,h);
}
uint32_t ps1_sd_commit(Ps1SdDirectory*s){
 if(!valid_context(s))return PS1_SD_INVALID;if(s->leaked_handle)return PS1_SD_CLOSE;
 _Alignas(4096) uint8_t m[4096];memset(m,0,sizeof m);uint32_t*p=(void*)m;p[0]=4;p[1]=8;p[4]=0x49434653;p[6]=10;return result_only(s,m,s->filesystem);
}
uint32_t ps1_sd_statfile(Ps1SdDirectory *s,const char *root,const char *path,uint64_t *size){
 if(!size)return PS1_SD_INVALID;
 *size=0;uint32_t handle=0;uint64_t length=0;uint32_t rc=ps1_sd_openfile(s,root,path,&handle,&length);if(rc)return rc;
 rc=ps1_sd_closefile(s,handle);if(rc)return rc;
 *size=length;return 0;
}
