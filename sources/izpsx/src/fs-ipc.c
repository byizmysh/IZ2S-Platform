/* Console-local IFile reads with one bounded read-ahead buffer. The file
 * capability is borrowed from the initial loader, never closed by this code. */
#include "platform.h"
#include <string.h>
static struct {uint32_t handle;uint64_t offset,bytes;} cache;
void ps1_ifile_reset(void){cache.handle=0;cache.offset=cache.bytes=0;}
static uint32_t request(Ps1FileIO*io,uint32_t handle,uint32_t command,uint64_t offset,uint64_t bytes,uint64_t*value){
 if(!io||!io->send||!io->buffer||io->buffer>=1ull<<39||(io->buffer&4095)||!io->capacity||io->capacity>65536||!handle||handle>=0x80000000u||bytes>io->capacity)return 1;
 _Alignas(4096) uint8_t message[4096];memset(message,0,sizeof message);uint32_t*p=(void*)message;unsigned raw=bytes?32:16;
 p[0]=4u|(bytes?1u<<24:0);p[1]=bytes?14:8;
 if(bytes){uint64_t a=io->buffer;p[2]=(uint32_t)bytes;p[3]=(uint32_t)a;p[4]=1u|(uint32_t)(a>>36)<<2|(uint32_t)((a>>32)&15)<<28;}
 p[raw/4]=0x49434653;p[raw/4+2]=command;if(bytes){uint64_t*input=(void*)(message+raw+16);input[0]=0;input[1]=offset;input[2]=bytes;}
 uint32_t rc=((uint32_t(*)(void*,uint64_t,uint32_t))(uintptr_t)io->send)(message,4096,handle);if(rc)return rc;
 unsigned at=8;if(p[1]&0x80000000u){uint32_t special=p[2];if(((special>>1)&15)||((special>>5)&15))return 2;at+=4;if(special&1)at+=8;}
 at+=((p[0]>>16)&15)*8+(((p[0]>>20)&15)+((p[0]>>24)&15)+((p[0]>>28)&15))*12;at=(at+15)&~15u;
 if(at>256||*(uint32_t*)(message+at)!=0x4f434653)return 3;
 rc=*(uint32_t*)(message+at+8);if(rc)return rc;*value=*(uint64_t*)(message+at+16);return 0;
}
uint32_t ps1_ifile_size(Ps1FileIO*io,uint32_t handle,int64_t*out){uint64_t size=0;uint32_t rc=request(io,handle,4,0,0,&size);if(!rc&&size>INT64_MAX)return 4;*out=(int64_t)size;return rc;}
uint32_t ps1_ifile_read(Ps1FileIO*io,uint32_t handle,uint64_t offset,void*out,uint64_t bytes,uint64_t file_size){
 if(offset>file_size||bytes>file_size-offset)return 5;
 uint8_t*dest=out;
 while(bytes){
  if(cache.handle!=handle||offset<cache.offset||offset-cache.offset>=cache.bytes){
   uint64_t wanted=file_size-offset;if(wanted>io->capacity)wanted=io->capacity;uint64_t got=0;
   uint32_t rc=request(io,handle,0,offset,wanted,&got);if(rc)return rc;if(got!=wanted)return 6;
   cache.handle=handle;cache.offset=offset;cache.bytes=got;io->reads++;io->bytes+=got;
  }
  uint64_t at=offset-cache.offset,n=cache.bytes-at;if(n>bytes)n=bytes;
  memcpy(dest,(void*)(uintptr_t)(io->buffer+at),n);dest+=n;offset+=n;bytes-=n;
 }return 0;
}
