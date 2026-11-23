/* nn::audio's 13-call ABI, backed by the real libounce AudioOut session.
 * Descriptor layout comes from SetAudioOutBufferInfo and the catalog, not
 * from casting the SDK's private AudioOut object to a libounce Service. */
#include <switch.h>
#include <string.h>
#include <sb4/audio.h>
#define P(t,x) ((t*)(uintptr_t)(x))
#define AUDIO_FN(n) static u64 n(u64 a,u64 b,u64 c,u64 d)
#define BAD 0xe870u
static u64 owner;
static u32 failure;
static bool started;
static AudioOutBuffer *pending[32];
static u32 count;
_Static_assert(sizeof(AudioOutBuffer)==40,"Audio buffer fields");
_Static_assert(offsetof(AudioOutBuffer,buffer)==8&&offsetof(AudioOutBuffer,data_size)==24,"SDK buffer ABI");
static u32 error(u32 rc){if(rc)failure=rc;return rc;}
AUDIO_FN(initialize){if(!a)return BAD;memset(P(void,a),0,8);*P(u32,a)=48000;*P(u16,a+4)=2;return 0;}
AUDIO_FN(open_output){
 if(!a||!b||owner||failure||*P(u32,b)!=48000||*P(u16,b+4)!=2)return BAD;
 u32 rc=audoutInitialize();if(rc)return rc;
 owner=a;started=false;count=0;memset(pending,0,sizeof pending);return 0;
}
AUDIO_FN(rate){return a==owner&&owner?audoutGetSampleRate():0;}
AUDIO_FN(channels){return a==owner&&owner?audoutGetChannelCount():0;}
AUDIO_FN(format){return a==owner&&owner?audoutGetPcmFormat():0;}
AUDIO_FN(buffer_info){
 if(!a||!b||b%4096||!c||c%4096||d>c)return BAD;
 for(u32 i=0;i<count;i++)if(pending[i]==P(AudioOutBuffer,a))return BAD;
 *P(AudioOutBuffer,a)=(AudioOutBuffer){.buffer=P(void,b),.buffer_size=c,.data_size=d};return 0;
}
AUDIO_FN(append){
 if(!owner||a!=owner||failure||!b||count==32)return BAD;
 AudioOutBuffer *v=P(AudioOutBuffer,b);
 if(!v->buffer||(uintptr_t)v->buffer%4096||!v->buffer_size||v->buffer_size%4096||v->data_size>v->buffer_size||v->data_offset)return BAD;
 for(u32 i=0;i<count;i++)if(pending[i]==v)return BAD;
 /* A failed IPC may already have queued the buffer. Keep its owner. */
 pending[count++]=v;u32 rc=audoutAppendAudioOutBuffer(v);return rc?error(rc):1;
}
AUDIO_FN(start){if(!owner||a!=owner||failure)return BAD;u32 rc=audoutStartAudioOut();if(!rc)started=true;return error(rc);}
AUDIO_FN(stop){if(!owner||a!=owner||failure)return BAD;u32 rc=audoutStopAudioOut();if(!rc)started=false;return error(rc);}
AUDIO_FN(flush){if(!owner||a!=owner||failure)return BAD;bool flushed=false;u32 rc=audoutFlushAudioOutBuffers(&flushed);return rc?error(rc):(u64)flushed;}
AUDIO_FN(released){
 if(!owner||a!=owner||failure)return 0;
 AudioOutBuffer *v=NULL;u32 n=0,rc=audoutGetReleasedAudioOutBuffer(&v,&n);
 if(rc){error(rc);return 0;}if(!n)return 0;
 if(n!=1||!v){error(BAD);return 0;}
 for(u32 i=0;i<count;i++)if(pending[i]==v){pending[i]=pending[--count];return (u64)(uintptr_t)v;}
 error(BAD);return 0;
}
AUDIO_FN(buffer_count){if(!owner||a!=owner||failure)return UINT32_MAX;u32 n=0,rc=audoutGetAudioOutBufferCount(&n);return rc?(error(rc),UINT32_MAX):n;}
AUDIO_FN(close_output){
 if(!owner||a!=owner||started||count||failure)return BAD;
 u32 n=0,rc=audoutGetAudioOutBufferCount(&n);if(rc)return error(rc);if(n)return BAD;
 audoutExit();owner=0;return 0;
}
const Sb4AudioFunction *sb4GetAudioFunctions(void){
 static const Sb4AudioFunction functions[]={initialize,open_output,rate,channels,format,buffer_info,append,start,stop,flush,released,buffer_count,close_output};
 return functions;
}
uint32_t sb4CloseAudio(void){return failure?failure:owner||count?BAD:0;}
