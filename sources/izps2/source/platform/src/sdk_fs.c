#include "sdk_fs.h"
#include <string.h>
#include <limits.h>
_Static_assert(sizeof(size_t)==8,"SDK size_t ABI");
static void add(uint64_t *value,uint64_t amount){__atomic_fetch_add(value,amount,__ATOMIC_RELAXED);}
static uint64_t enter(Ps2SdkFs *s){uint64_t previous=0;s->api.enter(&previous);add(&s->calls,1);return previous;}
static void leave(Ps2SdkFs *s,uint64_t previous){s->api.restore(previous);}
static int valid(const Ps2SdkFile *f){return f && f->opened && f->owner && f->owner->ready && f->handle;}
int ps2_sdk_fs_initialize(Ps2SdkFs *s,const Ps2SdkFsApi *a){
    if(!s || !a || !a->enter || !a->restore || !a->open || !a->close || !a->get_size || !a->read)return 1;
    memset(s,0,sizeof(*s));s->api=*a;s->ready=1;return 0;
}
static uint32_t open_handle(Ps2SdkFs *s,const char *path,int mode,uint64_t *handle){
    uint64_t previous=enter(s);uint32_t rc=s->api.open(handle,path,mode);leave(s,previous);return rc;
}
static void close_handle(Ps2SdkFs *s,uint64_t handle){uint64_t previous=enter(s);s->api.close(handle);leave(s,previous);add(&s->closed,1);}
uint32_t ps2_sdk_fs_open(Ps2SdkFs *s,const char *path,unsigned access,Ps2SdkFile *out){
    if(!s || !s->ready || !path || !*path || !out || out->opened || (access&~7u) || !(access&3u))return PS2_FS_INVALID;
    memset(out,0,sizeof(*out));int writing=(access&PS2_FS_WRITE)!=0;
    if(writing && (!s->api.write || !s->api.flush || !s->api.set_size))return PS2_FS_UNSUPPORTED;
    uint64_t handle=0;uint32_t rc=open_handle(s,path,access&3u,&handle);
    if(rc && writing && !(access&PS2_FS_UPDATE_EXISTING) && s->api.create && s->api.is_not_found && s->api.is_not_found(rc)){
        uint64_t previous=enter(s);rc=s->api.create(path,0,0);leave(s,previous);
        if(!rc){handle=0;rc=open_handle(s,path,access&3u,&handle);}
    }
    if(rc)return rc;
    if(!handle)return PS2_FS_BAD_REPLY;
    add(&s->opened,1);
    int64_t size=-1;uint64_t previous=enter(s);rc=s->api.get_size(&size,handle);leave(s,previous);
    if(!rc && size<0)rc=PS2_FS_BAD_REPLY;
    if(!rc && writing && !(access&PS2_FS_UPDATE_EXISTING)){
        previous=enter(s);rc=s->api.set_size(handle,0);leave(s,previous);if(!rc)size=0;
    }
    if(rc){close_handle(s,handle);return rc;}
    *out=(Ps2SdkFile){.owner=s,.handle=handle,.size=(uint64_t)size,.opened=1,.writable=writing,.dirty=writing && !(access&PS2_FS_UPDATE_EXISTING)};
    return 0;
}
uint32_t ps2_sdk_fs_read(Ps2SdkFile *f,uint64_t offset,void *buffer,size_t length,size_t *actual){
    if(actual)*actual=0;
    if(!valid(f) || !actual || (!buffer && length) || offset>INT64_MAX || length>(uint64_t)INT64_MAX-offset)return PS2_FS_INVALID;
    if(offset>=f->size || !length)return 0;
    if(length>f->size-offset)length=(size_t)(f->size-offset);
    Ps2SdkFs *s=f->owner;size_t received=0;uint64_t previous=enter(s);
    uint32_t rc=s->api.read(&received,f->handle,(int64_t)offset,buffer,length);leave(s,previous);
    if(rc)return rc;
    if(received>length)return PS2_FS_BAD_REPLY;
    *actual=received;add(&s->read_bytes,received);return 0;
}
uint32_t ps2_sdk_fs_write(Ps2SdkFile *f,uint64_t offset,const void *buffer,size_t length){
    if(!valid(f) || (!buffer && length) || offset>INT64_MAX || length>(uint64_t)INT64_MAX-offset)return PS2_FS_INVALID;
    if(!f->writable)return PS2_FS_READ_ONLY;
    if(!length)return 0;
    if(offset>f->size || length>f->size-offset){uint32_t rc=ps2_sdk_fs_resize(f,offset+length);if(rc)return rc;}
    /* Cookie stdio has no flush callback. WriteOption::Flush makes a completed
     * cookie write durable even when the core keeps its FILE open after fflush. */
    Ps2SdkFs *s=f->owner;uint32_t option=1;uint64_t previous=enter(s);
    uint32_t rc=s->api.write(f->handle,(int64_t)offset,buffer,length,&option);leave(s,previous);
    if(!rc){f->dirty=1;add(&s->written_bytes,length);}return rc;
}
uint32_t ps2_sdk_fs_size(Ps2SdkFile *f,uint64_t *out){
    if(out)*out=0;if(!valid(f)||!out)return PS2_FS_INVALID;
    Ps2SdkFs *s=f->owner;int64_t size=-1;uint64_t previous=enter(s);uint32_t rc=s->api.get_size(&size,f->handle);leave(s,previous);
    if(rc)return rc;if(size<0)return PS2_FS_BAD_REPLY;*out=f->size=(uint64_t)size;return 0;
}
uint32_t ps2_sdk_fs_resize(Ps2SdkFile *f,uint64_t size){
    if(!valid(f) || size>INT64_MAX)return PS2_FS_INVALID;if(!f->writable)return PS2_FS_READ_ONLY;
    Ps2SdkFs *s=f->owner;uint64_t previous=enter(s);uint32_t rc=s->api.set_size(f->handle,(int64_t)size);leave(s,previous);
    if(!rc){f->size=size;f->dirty=1;}return rc;
}
uint32_t ps2_sdk_fs_flush(Ps2SdkFile *f){
    if(!valid(f))return PS2_FS_INVALID;if(!f->dirty)return 0;
    Ps2SdkFs *s=f->owner;uint64_t previous=enter(s);uint32_t rc=s->api.flush(f->handle);leave(s,previous);
    if(!rc)f->dirty=0;return rc;
}
uint32_t ps2_sdk_fs_close(Ps2SdkFile *f){
    if(!valid(f))return PS2_FS_INVALID;
    uint32_t rc=ps2_sdk_fs_flush(f);Ps2SdkFs *s=f->owner;uint64_t handle=f->handle;
    memset(f,0,sizeof(*f));close_handle(s,handle);return rc;
}
