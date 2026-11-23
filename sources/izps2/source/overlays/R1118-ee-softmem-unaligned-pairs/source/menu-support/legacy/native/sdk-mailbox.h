/* Services for an actual SD application. Request and response work runs only
 * after the preceding GPU frame is idle. Haptics has a fixed native worker
 * for its existing short timing slices; UDP remains cooperative. */
#include "sdk-services.h"
enum {SDKM_UNAVAILABLE=0xe9d0u,SDKM_BAD_SOCKET,SDKM_SEND_LENGTH,SDKM_RECV_LENGTH,SDKM_CLOCK,SDKM_CANCELLED,SDKM_STOP_PENDING};
static u64 sdkm_call(IzSdkOwner*s,unsigned function,u64 a,u64 b,u64 d,u64 e,u64 f,u64 g){
 u32 id=iz_sdk_udp_code[function].offset;
 IzServiceFunction target=s->bridge?iz_service_target(s->bridge,id):(IzServiceFunction)(uintptr_t)(s->sdk+id);
 return target?target(a,b,d,e,f,g,0,0):SDKM_UNAVAILABLE;
}
static int sdkm_verify_table(IzSdkOwner*s,const IzSdkCode*table,u32 count,u32*verified){
 if(*verified)return *verified==1;
 *verified=2;
 if(s->bridge){
  /* A linked provider has explicit adapters, not copied SDK instruction bytes. */
  for(unsigned i=0;i<count;i++)if(!iz_service_target(s->bridge,table[i].offset))return 0;
  *verified=1;return 1;
 }
 if(s->sdk<4096||s->sdk>=(1ull<<39)-0x5e1000ull)return 0;
 for(unsigned i=0;i<count;i++){const IzSdkCode*r=&table[i];if(r->offset>=0x5e1000u||r->bytes>0x5e1000u-r->offset||memcmp((void*)(uintptr_t)(s->sdk+r->offset),r->code,r->bytes))return 0;}
 *verified=1;return 1;
}
static void sdkm_reply(IzContext*c,u32 status,const char*message,u32 length){
 IzSdkOwner*s=&c->owner.sdk;u8*out=(void*)(uintptr_t)(c->controls+IZ_SDK_INPUT_AT+IZ_SDK_RESPONSE_AT);u32 sequence=s->pending?s->pending:s->sequence;
 if(message){length=(u32)strlen(message);if(length>1024)length=1024;memcpy(out+32,message,length);}memset(out+32+length,0,1024-length);memset(out+4,0,28);W(out,4,status);W(out,8,length);__atomic_store_n((u32*)out,sequence,__ATOMIC_RELEASE);s->pending=0;
}
static void sdkm_finish(IzContext*c,u32 status,u32 error,u32 length){
 IzSdkOwner*s=&c->owner.sdk;s->error=error;
 if(s->socket>=0&&!s->retained){s->close_error=(u32)sdkm_call(s,SDKM_CLOSE,(u32)s->socket,0,0,0,0,0);if(s->close_error){s->retained=1;if(!s->error)s->error=s->close_error;}else s->socket=-1;}
 if(s->retained){sdkm_reply(c,3,"Native UDP descriptor retained",0);return;}
 if(status==4)sdkm_reply(c,4,"UDP receive timed out",0);else if(status||error)sdkm_reply(c,status?status:3,error==SDKM_CANCELLED?"UDP request cancelled":"Native UDP request failed",0);else sdkm_reply(c,0,0,length);
}
#include "sdk-haptics.h"
static u32 sdkm_stop(IzContext*c){
 IzSdkOwner*s=&c->owner.sdk;if(s->retained)return s->close_error?s->close_error:SDKM_UNAVAILABLE;
 if(s->pending&&(s->operation==1||s->operation==4)){
  if(!s->ending){s->ending=1;s->start_frame=c->frames;__atomic_store_n(&s->haptic.cancel,1,__ATOMIC_RELEASE);}
  if(sdkh_poll(c)){if(c->frames-s->start_frame<120)return SDKM_STOP_PENDING;sdkh_retain(c,SDKH_DRAIN_UNKNOWN);}
 }else if(s->pending)sdkm_finish(c,3,SDKM_CANCELLED,0);
 if(s->retained)return s->close_error;s->enabled=s->ending=0;return 0;
}
static u32 sdkm_shutdown(IzContext*c){
 u32 rc=sdkm_stop(c);for(unsigned i=0;rc==SDKM_STOP_PENDING&&!c->owner.sdk.retained&&i<250;i++){CALL(c,F_SLEEP,1000000,0,0,0,0,0,0,0);rc=sdkm_stop(c);}
 if(rc==SDKM_STOP_PENDING&&!c->owner.sdk.retained){sdkh_retain(c,SDKH_DRAIN_UNKNOWN);return SDKH_DRAIN_UNKNOWN;}return rc;
}
static u32 sdkm_begin(IzContext*c,const IzAppHeader*h){
 IzSdkOwner*s=&c->owner.sdk;if(s->pending||s->retained||s->haptic.thread)return SDKM_UNAVAILABLE;
 u32 epoch=s->epoch;memset(s,0,sizeof *s);s->epoch=epoch;s->socket=-1;
 if(!(h->flags&IZ_APP_SDK))return 0;if(iz_validate_sdk_config(h)||epoch==0xffffffffu)return SDKM_UNAVAILABLE;
 s->enabled=1;s->epoch=epoch+1;s->sdk=c->sdk;s->bridge=c->service_bridge;const u32*p=(const void*)(h->padding+32);s->capabilities=p[2];s->count=p[3];for(unsigned i=0;i<s->count;i++){s->endpoints[i].ip=p[4+i*2];s->endpoints[i].port=p[5+i*2];}
 memset((void*)(uintptr_t)(c->controls+IZ_SDK_INPUT_AT+IZ_SDK_REQUEST_AT),0,6144);return 0;
}
static u32 sdkm_epoch(IzContext*c){return c->active&&c->owner.sdk.enabled?c->owner.sdk.epoch:0;}
static void sdkm_frame(IzContext*c){
 IzSdkOwner*s=&c->owner.sdk;if(!s->enabled||!c->active||s->ending)return;
 if(s->pending&&(s->operation==1||s->operation==4)){sdkh_poll(c);return;}
 const u8*request=(void*)(uintptr_t)(c->controls+IZ_SDK_INPUT_AT+IZ_SDK_REQUEST_AT);u8*response=(void*)(uintptr_t)(c->controls+IZ_SDK_INPUT_AT+IZ_SDK_RESPONSE_AT);
 u64 now=0;
 if(!s->pending){
  u32 sequence=GET(request,0);if(!sequence||sequence==s->sequence)return;s->sequence=sequence;
  if(s->retained){sdkm_reply(c,3,"Native SDK ownership retained",0);return;}
  u32 operation=GET(request,4);if((operation==1||operation==4)&&(s->capabilities&2)){sdkh_begin(c,request,operation);return;}
  if(operation!=3||!(s->capabilities&1)){sdkm_reply(c,1,"Native SDK operation unavailable",0);return;}
  u32 endpoint=GET(request,8),length=GET(request,12);if(endpoint>=s->count||!length||length>1024||!c->tick_hz||c->tick_hz>0xffffffffu){sdkm_reply(c,2,"Invalid native UDP request",0);return;}
  if(!sdkm_verify_table(s,iz_sdk_udp_code,sizeof iz_sdk_udp_code/sizeof *iz_sdk_udp_code,&s->verified)){sdkm_reply(c,3,"Native SDK signature differs",0);return;}
  u8 payload[1024],peer[16]={16,2};memcpy(payload,request+32,length);u32 ip=s->endpoints[endpoint].ip,port=s->endpoints[endpoint].port;peer[2]=(u8)(port>>8);peer[3]=(u8)port;for(unsigned i=0;i<4;i++)peer[4+i]=(u8)(ip>>(24-i*8));
  now=CALL(c,F_TICK,0,0,0,0,0,0,0,0);s->operation=3;s->pending=sequence;s->error=s->close_error=s->polls=0;s->start=s->next_poll=s->last_tick=now;s->start_frame=c->frames;s->budget=120;
  if(sdkm_call(s,SDKM_READY,0,0,0,0,0,0)!=1){sdkm_finish(c,3,SDKM_UNAVAILABLE,0);return;}
  s->socket=(int32_t)(u32)sdkm_call(s,SDKM_SOCKET,2,2,17,0,0,0);if(s->socket<0){sdkm_finish(c,3,(u32)sdkm_call(s,SDKM_ERRNO,0,0,0,0,0,0),0);return;}
  int64_t sent=(int64_t)sdkm_call(s,SDKM_SEND,(u32)s->socket,(u64)(uintptr_t)payload,length,0x80,(u64)(uintptr_t)peer,16);if(sent!=(int64_t)length){sdkm_finish(c,3,sent<0?(u32)sdkm_call(s,SDKM_ERRNO,0,0,0,0,0,0):SDKM_SEND_LENGTH,0);return;}
 }else now=CALL(c,F_TICK,0,0,0,0,0,0,0,0);
 if(now<s->last_tick){sdkm_finish(c,3,SDKM_CLOCK,0);return;}s->last_tick=now;
 if(s->polls&&(now-s->start>=c->tick_hz||c->frames-s->start_frame>=s->budget)){sdkm_finish(c,4,0,0);return;}
 if(now<s->next_poll&&c->frames-s->start_frame<s->budget)return;
 int64_t received=(int64_t)sdkm_call(s,SDKM_RECV,(u32)s->socket,(u64)(uintptr_t)(response+32),1024,0x80,0,0);s->polls++;
 if(received>=0){if(received>1024)sdkm_finish(c,3,SDKM_RECV_LENGTH,0);else sdkm_finish(c,0,0,(u32)received);return;}
 u32 error=(u32)sdkm_call(s,SDKM_ERRNO,0,0,0,0,0,0);if(error!=11&&error!=35){sdkm_finish(c,3,error,0);return;}
 s->next_poll=now+c->tick_hz/50u;
}
