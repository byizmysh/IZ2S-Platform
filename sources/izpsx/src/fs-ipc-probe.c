/* Read an already-open, read-only IFile capability in the current process.
 * The host owns the capability until this thread has joined. No SDK mount is
 * needed and all data transfers are console-local kernel IPC. */
#include "abi.h"
typedef struct {HeavyContext base;uint64_t send,buffer,capacity,size,count,hash;uint32_t handle,transport,result,reserved;} Probe;
_Static_assert(sizeof(Probe)==320,"IFile probe ABI");
static unsigned request(Probe*c,uint32_t command,uint64_t offset,uint64_t size,uint8_t*message){
 for(unsigned i=0;i<4096;i++)message[i]=0;
 uint32_t*p=(void*)message;unsigned raw=size?32:16;p[0]=4u|(size?1u<<24:0);p[1]=size?14:8;
 if(size){uint64_t a=c->buffer;p[2]=(uint32_t)size;p[3]=(uint32_t)a;p[4]=1u|(uint32_t)(a>>36)<<2|(uint32_t)((a>>32)&15)<<28;}
 p[raw/4]=0x49434653;p[raw/4+2]=command;
 if(size){uint64_t*input=(void*)(message+raw+16);input[0]=0;input[1]=offset;input[2]=size;}
 c->transport=((uint32_t(*)(void*,uint64_t,uint32_t))(uintptr_t)c->send)(message,4096,c->handle);if(c->transport)return 1;
 unsigned at=8;if(p[1]&0x80000000u){uint32_t special=p[2];at+=4;if(special&1)at+=8;at+=4*(((special>>1)&15)+((special>>5)&15));}
 at+=((p[0]>>16)&15)*8+(((p[0]>>20)&15)+((p[0]>>24)&15)+((p[0]>>28)&15))*12;at=(at+15)&~15u;
 if(at>256||*(uint32_t*)(message+at)!=0x4f434653)return 2;
 c->result=*(uint32_t*)(message+at+8);if(c->result)return 3;
 uint64_t value=*(uint64_t*)(message+at+16);if(command==4)c->size=value;else c->count=value;return 0;
}
uint32_t heavy_main(HeavyContext*b){Probe*c=(void*)b;if(!c||b->magic!=HEAVY_MAGIC||!c->send||!c->handle||!c->buffer||(c->buffer&4095)||!c->capacity||c->capacity>65536)return 1;
 _Alignas(4096) uint8_t message[4096];b->status=1;b->phase=1;unsigned error=request(c,4,0,0,message);
 if(!error){b->phase=2;uint64_t count=c->size<c->capacity?c->size:c->capacity;if(count)error=request(c,0,0,count,message);if(!error&&c->count!=count)error=4;}
 if(!error){c->hash=1469598103934665603ull;const uint8_t*p=(void*)(uintptr_t)c->buffer;for(uint64_t i=0;i<c->count;i++){c->hash^=p[i];c->hash*=1099511628211ull;}}
 b->error=error;b->status=error?3:2;b->phase=3;return error;
}
