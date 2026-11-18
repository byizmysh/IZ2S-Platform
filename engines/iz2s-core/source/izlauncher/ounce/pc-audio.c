#include <stdbool.h>
#include <string.h>
#include <sb4/stream.h>
#include "pc-audio.h"
#include "pc-video.h"

enum { PcmPage=4096 };

static uint64_t call(IzPcPcm *pcm,unsigned action,const void *arg){
    return pcm->audio->functions[action]((uintptr_t)pcm,(uintptr_t)arg,0,0);
}

static uint32_t fail(IzPcPcm *pcm,uint64_t rc){
    return pcm->failure=rc>1&&rc<=UINT32_MAX?(uint32_t)rc:SB4_STREAM_INVALID;
}

static uint32_t open_pcm(IzPcPcm *pcm,IzNativeApp *app,const Sb4Compat *compat){
    const IzNativeAudio *audio=sb4ModuleAudio(app);
    if(!audio)return SB4_STREAM_UNAVAILABLE;
    void *memory=NULL;
    uint32_t rc=compat->allocate_aligned(&memory,PcmPage,IZ_PC_PCM_SLOTS*PcmPage);
    if(rc)return rc;
    if(!memory)return SB4_STREAM_INVALID;
    pcm->audio=audio;
    uint64_t result=audio->functions[IZ_AUDIO_INITIALIZE]((uintptr_t)&pcm->parameters,0,0,0);
    if(!result)result=call(pcm,IZ_AUDIO_OPEN,&pcm->parameters);
    if(result){compat->release(memory);pcm->audio=NULL;return (uint32_t)result;}
    pcm->samples=memory;
    if(call(pcm,IZ_AUDIO_RATE,NULL)==48000&&call(pcm,IZ_AUDIO_CHANNELS,NULL)==2&&
       call(pcm,IZ_AUDIO_FORMAT,NULL)==2)return 0;
    rc=iz_pc_pcm_close(pcm,app);
    return rc?rc:SB4_STREAM_UNAVAILABLE;
}

static uint32_t released(IzPcPcm *pcm){
    while(pcm->pending){
        const uint64_t receipt=call(pcm,IZ_AUDIO_RELEASED,NULL);
        if(!receipt)break;
        unsigned slot=0;
        while(slot<IZ_PC_PCM_SLOTS&&receipt!=(uintptr_t)pcm->descriptors[slot])++slot;
        if(slot==IZ_PC_PCM_SLOTS||!(pcm->pending&(1u<<slot)))return fail(pcm,0);
        pcm->pending&=~(1u<<slot);
    }
    return 0;
}

static uint32_t submit_pcm(IzPcPcm *pcm,IzNativeApp *app,const uint8_t *samples,unsigned prefill){
    const Sb4Compat *compat=sb4ModuleCompat(app);
    if(!compat||!samples||pcm->stopping)return SB4_STREAM_INVALID;
    if(pcm->failure)return pcm->failure;
    uint32_t rc=pcm->audio?0:open_pcm(pcm,app,compat);
    if(rc)return rc;
    rc=released(pcm);
    if(rc)return rc;
    if(prefill>1&&pcm->started&&!pcm->pending){
        uint64_t result=call(pcm,IZ_AUDIO_STOP,NULL);
        if(result)return fail(pcm,result);
        pcm->started=false;
    }
    const unsigned slots=prefill>1?IZ_PC_PCM_SLOTS:4;
    unsigned slot=0;
    while(slot<slots&&(pcm->pending&(1u<<slot)))++slot;
    if(slot==slots)return SB4_STREAM_PENDING;
    unsigned char *target=pcm->samples+slot*PcmPage;
    void *descriptor=pcm->descriptors[slot];
    memcpy(target,samples,IZ_PC_PCM_BYTES);
    uint64_t result=pcm->audio->functions[IZ_AUDIO_BUFFER]((uintptr_t)descriptor,(uintptr_t)target,PcmPage,IZ_PC_PCM_BYTES);
    if(result)return (uint32_t)result;
    pcm->pending|=1u<<slot;
    result=call(pcm,IZ_AUDIO_APPEND,descriptor);
    if(result!=1)return fail(pcm,result);
    if(pcm->started||__builtin_popcount(pcm->pending)<(int)prefill)return 0;
    result=call(pcm,IZ_AUDIO_START,NULL);
    if(result)return fail(pcm,result);
    pcm->started=true;
    return 0;
}

uint32_t iz_pc_pcm_submit(IzPcPcm *pcm,IzNativeApp *app,const uint8_t *samples){
    return submit_pcm(pcm,app,samples,1);
}

uint32_t iz_pc_pcm_submit_buffered(IzPcPcm *pcm,IzNativeApp *app,const uint8_t *samples){
    return submit_pcm(pcm,app,samples,3);
}

uint32_t iz_pc_pcm_close(IzPcPcm *pcm,IzNativeApp *app){
    if(pcm->failure)return pcm->failure;
    if(!pcm->audio)return 0;
    const Sb4Compat *compat=sb4ModuleCompat(app);
    if(!compat)return fail(pcm,0);
    if(!pcm->stopping){
        pcm->stop_tick=compat->ticks();
        pcm->stopping=true;
        uint64_t rc=pcm->started?call(pcm,IZ_AUDIO_STOP,NULL):0;
        if(rc)return fail(pcm,rc);
        if(pcm->pending){
            rc=call(pcm,IZ_AUDIO_FLUSH,NULL);
            if(rc>1)return fail(pcm,rc);
        }
    }
    const uint32_t rc=released(pcm);
    if(rc)return rc;
    if(pcm->pending){
        const uint64_t frequency=compat->tick_hz();
        if(!frequency||(compat->ticks()-pcm->stop_tick)/frequency>=2)return fail(pcm,0);
        return IZ_NATIVE_STOP_PENDING;
    }
    uint64_t result=call(pcm,IZ_AUDIO_COUNT,NULL);
    if(result)return fail(pcm,result);
    result=call(pcm,IZ_AUDIO_CLOSE,NULL);
    if(result)return fail(pcm,result);
    compat->release(pcm->samples);
    memset(pcm,0,sizeof(*pcm));
    return 0;
}
