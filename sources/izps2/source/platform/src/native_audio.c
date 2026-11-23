#include "native_audio.h"
#include <float.h>
#include <string.h>
#define NO_SLOT UINT32_MAX
#define SLOT_BYTES (PS2_AUDIO_SLOT_FRAMES*4u)
static int overlap(uintptr_t a,size_t n,uintptr_t b,size_t m){
    if(n>UINTPTR_MAX-a||m>UINTPTR_MAX-b)return 1;
    return a<b+m&&b<a+n;
}
static int fail(Ps2NativeAudio*a,int error,uint32_t sdk,int uncertain){
    if(a){a->last_error=error;a->sdk_result=sdk;if(uncertain)a->uncertain=1;}return error;
}
static void*object(Ps2NativeAudio*a){return a->config.arena;}
static void*descriptor(Ps2NativeAudio*a,unsigned slot){return (uint8_t*)a->config.arena+512+slot*64;}
static int16_t*pcm(Ps2NativeAudio*a,unsigned slot){return (int16_t*)((uint8_t*)a->config.arena+4096+slot*SLOT_BYTES);}
typedef uint64_t (*Sb4AudioCall)(uint64_t,uint64_t,uint64_t,uint64_t);
static uint64_t sb4_call(const Ps2NativeAudio*a,unsigned fn,uint64_t x,uint64_t y,uint64_t z,uint64_t w){
    return ((Sb4AudioCall)a->config.functions[fn])(x,y,z,w);
}
static uint32_t scalar(Ps2NativeAudio*a,unsigned fn){
    if(a->config.service_abi==PS2_AUDIO_SB4_ABI)
        return (uint32_t)sb4_call(a,fn,(uintptr_t)object(a),0,0,0);
    return ((uint32_t(*)(void*))a->config.functions[fn])(object(a));
}
static int valid(Ps2NativeAudio*a){return a&&a->opened&&!a->uncertain&&a->format_verified;}
static int start(Ps2NativeAudio*a){
    uint32_t r=scalar(a,PS2_AUDIO_START);if(r)return fail(a,PS2_AUDIO_SDK_ERROR,r,1);a->started=1;return 0;
}
int ps2_native_audio_open(Ps2NativeAudio*a,const Ps2NativeAudioConfig*c){
    if(!a||!c||a->opened||a->uncertain||c->service_abi>PS2_AUDIO_SB4_ABI||!c->arena||((uintptr_t)c->arena&4095)||
       c->arena_bytes<PS2_AUDIO_ARENA_BYTES||!c->sleep_ns||!c->startup_slots||c->startup_slots>PS2_AUDIO_SLOTS||
       overlap((uintptr_t)a,sizeof(*a),(uintptr_t)c,sizeof(*c))||
       overlap((uintptr_t)a,sizeof(*a),(uintptr_t)c->arena,c->arena_bytes)||
       overlap((uintptr_t)c,sizeof(*c),(uintptr_t)c->arena,c->arena_bytes))return PS2_AUDIO_INVALID;
    for(unsigned i=0;i<PS2_AUDIO_FUNCTIONS;i++)if(!c->functions[i])return PS2_AUDIO_INVALID;
    const Ps2NativeAudioConfig copy=*c;memset(a,0,sizeof(*a));a->config=copy;a->write_slot=NO_SLOT;
    memset(c->arena,0,PS2_AUDIO_ARENA_BYTES);void*param=(uint8_t*)c->arena+256;
    uint32_t r;
    if(copy.service_abi==PS2_AUDIO_SB4_ABI){
        r=(uint32_t)sb4_call(a,PS2_AUDIO_INIT,(uintptr_t)param,0,0,0);
        if(r)return fail(a,PS2_AUDIO_SDK_ERROR,r,0);
        r=(uint32_t)sb4_call(a,PS2_AUDIO_OPEN,(uintptr_t)object(a),(uintptr_t)param,0,0);
    }else{
        ((void(*)(void*))copy.functions[PS2_AUDIO_INIT])(param);
        *(uint32_t*)param=48000;*(uint16_t*)((uint8_t*)param+4)=2;
        r=((uint32_t(*)(void*,void*))copy.functions[PS2_AUDIO_OPEN])(object(a),param);
    }
    if(r)return fail(a,PS2_AUDIO_SDK_ERROR,r,1);
    a->opened=1;
    if(scalar(a,PS2_AUDIO_RATE)!=48000||scalar(a,PS2_AUDIO_CHANNELS)!=2||scalar(a,PS2_AUDIO_FORMAT)!=2)
        return fail(a,PS2_AUDIO_UNSUPPORTED_FORMAT,0,0);
    a->format_verified=1;return 0;
}
int ps2_native_audio_poll(Ps2NativeAudio*a){
    if(!valid(a))return PS2_AUDIO_INVALID;
    for(unsigned i=0;i<=PS2_AUDIO_SLOTS;i++){
        void*p=a->config.service_abi==PS2_AUDIO_SB4_ABI
            ?(void*)(uintptr_t)sb4_call(a,PS2_AUDIO_RELEASE,(uintptr_t)object(a),0,0,0)
            :((void*(*)(void*))a->config.functions[PS2_AUDIO_RELEASE])(object(a));
        if(!p)return 0;
        const uintptr_t begin=(uintptr_t)a->config.arena+512,address=(uintptr_t)p;
        if(address<begin||address-begin>=PS2_AUDIO_SLOTS*64||(address-begin)%64)
            return fail(a,PS2_AUDIO_OWNERSHIP,0,1);
        unsigned slot=(unsigned)((address-begin)/64);
        if(!a->busy[slot]||!a->pending)return fail(a,PS2_AUDIO_OWNERSHIP,0,1);
        a->busy[slot]=0;a->pending--;a->completed_frames+=a->slot_frames[slot];a->slot_frames[slot]=0;a->releases++;
    }
    return fail(a,PS2_AUDIO_OWNERSHIP,0,1);
}
static int get_slot(Ps2NativeAudio*a){
    if(a->write_slot!=NO_SLOT)return 0;
    for(unsigned i=0;i<PS2_AUDIO_SLOTS;i++)if(!a->busy[i]){a->write_slot=i;return 0;}
    a->backpressure++;return PS2_AUDIO_BACKPRESSURE;
}
static int submit(Ps2NativeAudio*a){
    if(!a->staged_frames)return 0;
    const unsigned slot=a->write_slot;
    if(slot>=PS2_AUDIO_SLOTS||a->busy[slot]||a->staged_frames>PS2_AUDIO_SLOT_FRAMES)
        return fail(a,PS2_AUDIO_OWNERSHIP,0,1);
    void *buffer=descriptor(a,slot);
    uint64_t bytes=a->staged_frames*4u;
    uint32_t accepted;
    if(a->config.service_abi==PS2_AUDIO_SB4_ABI){
        uint32_t rc=(uint32_t)sb4_call(a,PS2_AUDIO_SET_BUFFER,(uintptr_t)buffer,(uintptr_t)pcm(a,slot),SLOT_BYTES,bytes);
        if(rc)return fail(a,PS2_AUDIO_SDK_ERROR,rc,0);
        accepted=(uint32_t)sb4_call(a,PS2_AUDIO_APPEND,(uintptr_t)object(a),(uintptr_t)buffer,0,0);
    }else{
        ((void(*)(void*,void*,uint64_t,uint64_t))a->config.functions[PS2_AUDIO_SET_BUFFER])
            (buffer,pcm(a,slot),SLOT_BYTES,bytes);
        accepted=((uint32_t(*)(void*,void*))a->config.functions[PS2_AUDIO_APPEND])(object(a),buffer);
    }
    if(!accepted)return fail(a,PS2_AUDIO_REJECTED,0,0);
    a->busy[slot]=1;a->slot_frames[slot]=a->staged_frames;a->pending++;
    if(a->pending>a->peak_pending)a->peak_pending=a->pending;
    if(accepted!=1)return fail(a,PS2_AUDIO_OWNERSHIP,accepted,1);
    a->submitted_frames+=a->staged_frames;a->appends++;a->staged_frames=0;a->write_slot=NO_SLOT;
    if(!a->started&&a->pending>=a->config.startup_slots)return start(a);
    return 0;
}
int ps2_native_audio_feed(Ps2NativeAudio*a,const float*input,size_t frames,size_t*consumed){
    if(!valid(a)||!consumed||((uintptr_t)consumed%_Alignof(size_t))||(!input&&frames)||
       (frames&&((uintptr_t)input%_Alignof(float)))||frames>SIZE_MAX/(2*sizeof(float))||
       overlap((uintptr_t)consumed,sizeof(*consumed),(uintptr_t)a,sizeof(*a))||
       overlap((uintptr_t)consumed,sizeof(*consumed),(uintptr_t)a->config.arena,a->config.arena_bytes)||
       (frames&&(overlap((uintptr_t)input,frames*2*sizeof(float),(uintptr_t)a,sizeof(*a))||
                 overlap((uintptr_t)input,frames*2*sizeof(float),(uintptr_t)a->config.arena,a->config.arena_bytes)||
                 overlap((uintptr_t)input,frames*2*sizeof(float),(uintptr_t)consumed,sizeof(*consumed)))))return PS2_AUDIO_INVALID;
    *consumed=0;
    for(size_t i=0;i<frames*2;i++)if(!(input[i]<=FLT_MAX&&input[i]>=-FLT_MAX))return PS2_AUDIO_INVALID;
    int r=ps2_native_audio_poll(a);if(r)return r;
    for(size_t i=0;i<frames;i++){
        if(a->staged_frames==PS2_AUDIO_SLOT_FRAMES){r=submit(a);if(r)return r;}
        r=get_slot(a);if(r)return r;
        int16_t*out=pcm(a,a->write_slot)+a->staged_frames*2;
        for(unsigned ch=0;ch<2;ch++){
            float x=input[i*2+ch];if(x>1){x=1;a->clipped_samples++;}else if(x<-1){x=-1;a->clipped_samples++;}
            int32_t value=(int32_t)(x*32768.0f);if(value>32767)value=32767;out[ch]=(int16_t)value;
        }
        a->staged_frames++;a->accepted_frames++;*consumed=i+1;
        if(a->staged_frames==PS2_AUDIO_SLOT_FRAMES){r=submit(a);if(r)return r;}
    }
    return 0;
}
int ps2_native_audio_flush_partial(Ps2NativeAudio*a){
    if(!valid(a))return PS2_AUDIO_INVALID;int r=ps2_native_audio_poll(a);return r?r:submit(a);
}
int ps2_native_audio_drain(Ps2NativeAudio*a,unsigned wait_ms){
    if(!a||!a->opened||a->uncertain||wait_ms>60000)return PS2_AUDIO_INVALID;
    if(!a->format_verified&&(a->pending||a->staged_frames||a->started))return PS2_AUDIO_INVALID;
    int r=a->format_verified?ps2_native_audio_flush_partial(a):0;if(r)return r;
    if(a->pending&&!a->started){r=start(a);if(r)return r;}
    unsigned waited=0;
    while(a->pending){
        r=ps2_native_audio_poll(a);if(r)return r;if(!a->pending)break;
        if(waited==wait_ms){a->timeouts++;return fail(a,PS2_AUDIO_TIMEOUT,0,0);}
        a->config.sleep_ns(1000000);waited++;
    }
    if(a->started){uint32_t result=scalar(a,PS2_AUDIO_STOP);if(result)return fail(a,PS2_AUDIO_SDK_ERROR,result,1);a->started=0;}
    uint32_t flushed=scalar(a,PS2_AUDIO_FLUSH);
    if(flushed>1)return fail(a,PS2_AUDIO_OWNERSHIP,flushed,1);
    uint32_t count=scalar(a,PS2_AUDIO_COUNT);if(count)return fail(a,PS2_AUDIO_OWNERSHIP,count,1);
    return 0;
}
int ps2_native_audio_close(Ps2NativeAudio*a,unsigned wait_ms){
    if(!a||a->uncertain)return PS2_AUDIO_INVALID;if(!a->opened)return 0;
    int r=ps2_native_audio_drain(a,wait_ms);if(r)return r;
    if(a->config.service_abi==PS2_AUDIO_SB4_ABI){
        uint32_t rc=scalar(a,PS2_AUDIO_CLOSE);
        if(rc)return fail(a,PS2_AUDIO_SDK_ERROR,rc,1);
    }else{
        ((void(*)(void*))a->config.functions[PS2_AUDIO_CLOSE])(object(a));
    }
    a->opened=0;
    return 0;
}
