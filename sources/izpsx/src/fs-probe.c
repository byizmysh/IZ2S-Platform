/* Read-only SDK filesystem qualification. An optional private SD mount is
 * unmounted before returning; no files or directories are created or changed. */
#include "abi.h"
typedef struct {
 HeavyContext base;uint64_t fn[11],path,mount_name,buffer,capacity;
 uint32_t operation,mounted;uint64_t size,count,hash;
 uint32_t result,opened;uint64_t before,after;
} Probe;
_Static_assert(sizeof(Probe)==432,"FS probe ABI");
_Static_assert(offsetof(Probe,path)==344,"FS path ABI");
enum{ENTER,RESTORE,MOUNT,UNMOUNT,OPEN_DIR,READ_DIR,CLOSE_DIR,OPEN_FILE,READ_FILE,FILE_SIZE,CLOSE_FILE};
uint32_t heavy_main(HeavyContext*b){
 Probe*c=(void*)b;if(!b||b->magic!=HEAVY_MAGIC||!c->path||!c->buffer||c->capacity<784||c->capacity>65536||c->operation>1)return 1;
 for(unsigned i=0;i<11;i++)if(!c->fn[i])return 2;
 b->status=1;b->phase=1;c->before=0;
 ((void(*)(uint64_t*))(uintptr_t)c->fn[ENTER])(&c->before);
 if(c->mount_name){c->result=((uint32_t(*)(const char*))(uintptr_t)c->fn[MOUNT])((const char*)(uintptr_t)c->mount_name);if(c->result)goto done;c->mounted=1;}
 uint64_t handle=0;b->phase=2;
 if(!c->operation){
  c->result=((uint32_t(*)(uint64_t*,const char*,int))(uintptr_t)c->fn[OPEN_DIR])(&handle,(const char*)(uintptr_t)c->path,3);
  if(!c->result){int64_t count=0;c->opened=1;c->result=((uint32_t(*)(int64_t*,void*,uint64_t,int64_t))(uintptr_t)c->fn[READ_DIR])(&count,(void*)(uintptr_t)c->buffer,handle,c->capacity/784);
   if(count<0||(uint64_t)count>c->capacity/784)c->result=0xffffffffu;else{c->count=count;c->size=count*784;}
   ((void(*)(uint64_t))(uintptr_t)c->fn[CLOSE_DIR])(handle);c->opened=0;}
 }else{
  c->result=((uint32_t(*)(uint64_t*,const char*,int))(uintptr_t)c->fn[OPEN_FILE])(&handle,(const char*)(uintptr_t)c->path,1);
  if(!c->result){int64_t size=0;c->opened=1;c->result=((uint32_t(*)(int64_t*,uint64_t))(uintptr_t)c->fn[FILE_SIZE])(&size,handle);
   if(size<0)c->result=0xffffffffu;
   if(!c->result){c->size=size;c->count=c->size<c->capacity?c->size:c->capacity;
    if(c->count)c->result=((uint32_t(*)(uint64_t,int64_t,void*,uint64_t))(uintptr_t)c->fn[READ_FILE])(handle,0,(void*)(uintptr_t)c->buffer,c->count);
    if(!c->result){const uint8_t*p=(void*)(uintptr_t)c->buffer;c->hash=1469598103934665603ull;for(uint64_t i=0;i<c->count;i++){c->hash^=p[i];c->hash*=1099511628211ull;}}}
   ((void(*)(uint64_t))(uintptr_t)c->fn[CLOSE_FILE])(handle);c->opened=0;}
 }
done:
 b->phase=3;if(c->mounted){((void(*)(const char*))(uintptr_t)c->fn[UNMOUNT])((const char*)(uintptr_t)c->mount_name);c->mounted=0;}
 ((void(*)(uint64_t))(uintptr_t)c->fn[RESTORE])(c->before);
 ((void(*)(uint64_t*))(uintptr_t)c->fn[ENTER])(&c->after);
 ((void(*)(uint64_t))(uintptr_t)c->fn[RESTORE])(c->after);
 if(c->before!=c->after)c->result=0xfffffffeu;
 b->error=c->result;b->status=c->result?3:2;b->phase=4;return c->result;
}
