#include "ps2_native_log_adapter.h"
#include <errno.h>
#include <limits.h>
#include <string.h>

#define TRACE_MAGIC 0x31424c50u
#define TRACE_FULL 0xffff1001u
#define CAPTURE_FAILED 0xffff1002u
static Ps2NativeLogAdapter *active;
static uint32_t activation_claim;
/* This is ordinary private musl TLS, not a new SDK/thread ABI. Reentrant logs
 * from futex fallback, sink or controller join use the independent ledger. */
static _Thread_local unsigned backend_depth;
static _Thread_local Ps2NativeLogAdapter *backend_owner;
static void backend_enter(Ps2NativeLogAdapter*a){if(!backend_depth)backend_owner=a;++backend_depth;}
static void backend_leave(void){if(!--backend_depth)backend_owner=0;}
static int area(const void*p,size_t n){return p&&n&&(uintptr_t)p<=UINTPTR_MAX-n;}
static int overlap(const void*a,size_t n,const void*b,size_t m){return (uintptr_t)a<(uintptr_t)b+m&&(uintptr_t)b<(uintptr_t)a+n;}

static Ps2LogIoResult result(unsigned state,unsigned code,size_t bytes) {
    Ps2LogIoResult r={state,code,bytes};return r;
}
static void sticky(Ps2NativeLogAdapter *a,uint32_t code) {
    uint32_t old=0;__atomic_compare_exchange_n(&a->sticky_error,&old,code,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED);
}
static int backend(Ps2NativeLogAdapter *a,unsigned kind,int fd,const void *data,size_t n) {
    if(n>UINT32_MAX-sizeof(Ps2NativeLogBackendHeader))goto full;
    const size_t bytes=(sizeof(Ps2NativeLogBackendHeader)+n+7u)&~(size_t)7;
    size_t at=__atomic_load_n(&a->backend_used,__ATOMIC_RELAXED);
    for(;;){
        if(at>a->config.backend_bytes||bytes>a->config.backend_bytes-at)goto full;
        if(__atomic_compare_exchange_n(&a->backend_used,&at,at+bytes,1,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED))break;
    }
    unsigned char *p=(unsigned char*)a->config.backend+at;
    Ps2NativeLogBackendHeader h={TRACE_MAGIC,kind,(uint32_t)n,0,
        __atomic_add_fetch(&a->backend_sequence,1,__ATOMIC_RELAXED),
        a->config.tick?a->config.tick(a->config.tick_user):0,fd,0};
    memcpy(p,&h,sizeof h);if(n)memcpy(p+sizeof h,data,n);
    if(bytes>sizeof h+n)memset(p+sizeof h+n,0,bytes-sizeof h-n);
    __atomic_store_n(&((Ps2NativeLogBackendHeader*)p)->committed,1,__ATOMIC_RELEASE);
    return 0;
full:
    __atomic_add_fetch(&a->backend_rejected,1,__ATOMIC_RELAXED);sticky(a,TRACE_FULL);return -1;
}
static int receipt(Ps2NativeLogAdapter*a,unsigned op,unsigned phase,uint64_t x,uint64_t y,Ps2LogIoResult r) {
    Ps2NativeLogBackendReceipt p={op,phase,x,y,r.state,r.code,r.bytes,a->config.tick_hz};
    return backend(a,2,0,&p,sizeof p);
}
static void lock(void*p){(void)pthread_mutex_lock(&((Ps2NativeLogAdapter*)p)->mutex);}
static void unlock(void*p){(void)pthread_mutex_unlock(&((Ps2NativeLogAdapter*)p)->mutex);}
static int wait(void*p){Ps2NativeLogAdapter*a=p;return pthread_cond_wait(&a->condition,&a->mutex);}
static void notify(void*p){(void)pthread_cond_broadcast(&((Ps2NativeLogAdapter*)p)->condition);}
static void *entry(void*p){Ps2NativeLogAdapter*a=p;backend_enter(a);a->worker_entry(a->worker_argument);backend_leave();return 0;}
static uint32_t start(void*p,void(*fn)(void*),void*argument){
    Ps2NativeLogAdapter*a=p;a->worker_entry=fn;a->worker_argument=argument;
    pthread_attr_t attr;int rc=pthread_attr_init(&attr);if(rc)return PS2_LOG_THREAD_REJECTED;
    rc=pthread_attr_setstacksize(&attr,a->config.worker_stack_bytes);
    if(!rc)rc=pthread_create(&a->worker,&attr,entry,a);
    const int cleanup=pthread_attr_destroy(&attr);
    if(!rc){if(cleanup)sticky(a,(uint32_t)cleanup);return PS2_LOG_THREAD_OK;}
    return PS2_LOG_THREAD_REJECTED; /* Existing facade returns errors before creation. */
}
static uint32_t joined(void*p){Ps2NativeLogAdapter*a=p;
    if(!__atomic_load_n(&a->controller_join_available,__ATOMIC_ACQUIRE))return PS2_LOG_THREAD_BUSY;
    return a->controller_join_result?PS2_LOG_THREAD_UNCERTAIN:PS2_LOG_THREAD_OK;
}
static Ps2LogIoResult sdk_result(Ps2NativeLogAdapter*a,uint32_t rc,size_t bytes){
    if(!rc)return result(PS2_LOG_IO_OK,0,bytes);
    return result(ps2_sd_write_healthy(&a->sd)?PS2_LOG_IO_ERROR:PS2_LOG_IO_UNCERTAIN,rc,bytes);
}
static Ps2LogIoResult traced_after(Ps2NativeLogAdapter*a,unsigned op,uint64_t x,uint64_t y,Ps2LogIoResult r){
    if(receipt(a,op,1,x,y,r))return result(PS2_LOG_IO_UNCERTAIN,TRACE_FULL,0);
    return r;
}
static Ps2LogIoResult open_sink(void*p){Ps2NativeLogAdapter*a=p;
    if(receipt(a,1,0,0,0,result(0,0,0)))return result(PS2_LOG_IO_ERROR,TRACE_FULL,0);
    uint32_t rc=ps2_sd_write_create(&a->sd,a->path,0,0);
    uint64_t file=0;if(!rc)rc=ps2_sd_write_open(&a->sd,&file,a->path,3);
    __atomic_store_n(&a->file,file,__ATOMIC_RELEASE);
    return traced_after(a,1,file,0,sdk_result(a,rc,0));
}
static Ps2LogIoResult write_sink(void*p,const void*data,size_t n){Ps2NativeLogAdapter*a=p;
    if(receipt(a,2,0,a->offset,n,result(0,0,0)))return result(PS2_LOG_IO_ERROR,TRACE_FULL,0);
    /* Existing facade requires explicit SetSize before a write can extend a
     * file. The same offset is never replayed after an ambiguous operation. */
    uint32_t rc=n>INT64_MAX-a->offset?PS2_SD_WRITE_INVALID:
        ps2_sd_write_resize(&a->sd,a->file,(int64_t)(a->offset+n));
    const uint64_t before=a->sd.written_bytes;const uint32_t option=1;
    if(!rc)rc=ps2_sd_write_write(&a->sd,a->file,(int64_t)a->offset,data,n,&option);
    const size_t actual=(size_t)(a->sd.written_bytes-before);const uint64_t at=a->offset;
    __atomic_store_n(&a->offset,at+actual,__ATOMIC_RELEASE);
    return traced_after(a,2,at,n,sdk_result(a,rc,actual));
}
static Ps2LogIoResult flush_sink(void*p){Ps2NativeLogAdapter*a=p;
    if(receipt(a,3,0,a->file,0,result(0,0,0)))return result(PS2_LOG_IO_ERROR,TRACE_FULL,0);
    return traced_after(a,3,a->file,0,sdk_result(a,ps2_sd_write_flush(&a->sd,a->file),0));
}
static Ps2LogIoResult close_sink(void*p){Ps2NativeLogAdapter*a=p;
    if(receipt(a,4,0,a->file,0,result(0,0,0)))return result(PS2_LOG_IO_ERROR,TRACE_FULL,0);
    const uint64_t before=a->sd.closed;ps2_sd_write_close(&a->sd,a->file);
    Ps2LogIoResult r= a->sd.closed==before+1&&ps2_sd_write_healthy(&a->sd)?result(0,0,0):
        result(PS2_LOG_IO_UNCERTAIN,a->sd.error?a->sd.error:PS2_SD_WRITE_RETAINED,0);
    return traced_after(a,4,a->file,0,r);
}

int ps2_native_log_adapter_start(Ps2NativeLogAdapter*a,const Ps2NativeLogAdapterConfig*c){
    if(!a||!c||a->initialized||__atomic_load_n(&active,__ATOMIC_ACQUIRE)||
       !c->backend||((uintptr_t)c->backend&7)||c->backend_bytes<512||
       c->worker_stack_bytes<PTHREAD_STACK_MIN||(c->worker_stack_bytes&4095)||
       !c->unique_path||strnlen(c->unique_path,0x301)>=0x301)return PS2_LOG_INVALID;
    const void*areas[]={a,c->queue,c->batch,c->backend,(const void*)(uintptr_t)c->sd.buffer};
    const size_t lengths[]={sizeof(*a),c->queue_bytes,c->batch_bytes,c->backend_bytes,(size_t)c->sd.capacity};
    for(unsigned i=0;i<5;i++){if(!area(areas[i],lengths[i]))return PS2_LOG_INVALID;
        for(unsigned j=0;j<i;j++)if(overlap(areas[i],lengths[i],areas[j],lengths[j]))return PS2_LOG_INVALID;}
    if(overlap(a,sizeof(*a),c,sizeof(*c))||overlap(a,sizeof(*a),c->unique_path,strlen(c->unique_path)+1))return PS2_LOG_INVALID;
    uint32_t expected=0;
    if(!__atomic_compare_exchange_n(&activation_claim,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED))return PS2_LOG_INVALID;
    memset(a,0,sizeof(*a));a->config=*c;memcpy(a->path,c->unique_path,strlen(c->unique_path)+1);
    if(ps2_sd_write_initialize(&a->sd,&c->sd,c->send,c->close_handle))return PS2_LOG_INVALID;
    int rc=pthread_mutex_init(&a->mutex,0);if(rc)return PS2_LOG_FAILED;
    rc=pthread_cond_init(&a->condition,0);if(rc){pthread_mutex_destroy(&a->mutex);return PS2_LOG_FAILED;}
    a->initialized=1;
    Ps2LogSpoolConfig config={{a,lock,unlock,wait,notify,a,start,joined,a,open_sink,write_sink,flush_sink,close_sink},
        c->queue,c->queue_bytes,c->batch,c->batch_bytes,c->max_record_bytes};
    backend_enter(a);rc=ps2_log_spool_start(&a->spool,&config);backend_leave();
    if(rc!=PS2_LOG_OK)return rc;
    a->active=1;__atomic_store_n(&active,a,__ATOMIC_RELEASE);return PS2_LOG_OK;
}
int ps2_native_log_capture(int fd,const void*data,size_t n){
    Ps2NativeLogAdapter*a=backend_depth?backend_owner:__atomic_load_n(&active,__ATOMIC_ACQUIRE);if(!a)return 0;
    if(backend_depth)return backend(a,1,fd,data,n)?-1:1;
    backend_enter(a);
    const int rc=ps2_log_spool_append(&a->spool,data,n,0);
    backend_leave();
    if(rc!=PS2_LOG_OK){__atomic_add_fetch(&a->capture_failures,1,__ATOMIC_RELAXED);sticky(a,CAPTURE_FAILED);return -1;}
    return 1;
}
int ps2_native_log_adapter_seal(Ps2NativeLogAdapter*a){if(!a||!a->initialized)return PS2_LOG_INVALID;
    backend_enter(a);const int rc=ps2_log_spool_seal(&a->spool);backend_leave();return rc;}
int ps2_native_log_adapter_join(Ps2NativeLogAdapter*a){if(!a||!a->initialized)return PS2_LOG_INVALID;
    Ps2LogSpoolSnapshot v;backend_enter(a);int rc=ps2_log_spool_snapshot(&a->spool,&v);backend_leave();
    if(rc!=PS2_LOG_OK)return rc;
    if(!v.sealed||!v.worker_done)return PS2_LOG_BUSY;
    uint32_t old=0;
    if(__atomic_compare_exchange_n(&a->controller_join_attempted,&old,1,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)){
        if(receipt(a,5,0,0,0,result(0,0,0))){sticky(a,TRACE_FULL);return PS2_LOG_FAILED;}
        backend_enter(a);const int r=pthread_join(a->worker,0);backend_leave();
        a->controller_join_result=(uint32_t)r;
        if(receipt(a,5,1,0,0,result(r?PS2_LOG_IO_UNCERTAIN:PS2_LOG_IO_OK,(uint32_t)r,0)))a->controller_join_result=TRACE_FULL;
        __atomic_store_n(&a->controller_join_available,1,__ATOMIC_RELEASE);
    }
    backend_enter(a);rc=ps2_log_spool_try_join(&a->spool);backend_leave();return rc;
}
int ps2_native_log_adapter_snapshot(Ps2NativeLogAdapter*a,Ps2NativeLogAdapterSnapshot*out){
    if(!a||!a->initialized||!out)return PS2_LOG_INVALID;
    backend_enter(a);
    const int rc=ps2_log_spool_snapshot(&a->spool,&out->spool);backend_leave();
    out->file=__atomic_load_n(&a->file,__ATOMIC_ACQUIRE);out->offset=__atomic_load_n(&a->offset,__ATOMIC_ACQUIRE);out->backend_sequence=__atomic_load_n(&a->backend_sequence,__ATOMIC_ACQUIRE);
    out->backend_rejected=__atomic_load_n(&a->backend_rejected,__ATOMIC_ACQUIRE);
    out->backend_used=__atomic_load_n(&a->backend_used,__ATOMIC_ACQUIRE);
    out->capture_failures=__atomic_load_n(&a->capture_failures,__ATOMIC_ACQUIRE);
    out->active=a->active;out->sticky_error=__atomic_load_n(&a->sticky_error,__ATOMIC_ACQUIRE);
    out->controller_join_attempted=__atomic_load_n(&a->controller_join_attempted,__ATOMIC_ACQUIRE);
    out->backendRequiresPreservation=out->backend_used!=0;return rc;
}
