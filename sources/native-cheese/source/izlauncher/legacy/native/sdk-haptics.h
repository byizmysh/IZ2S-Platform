/* The native worker owns a fixed, copied envelope. It never reads application
 * controls or writes the GPU mailbox. The renderer owns cancellation and join. */
enum {SDKH_CREATE_UNKNOWN=0xe9e0u,SDKH_WAIT_UNKNOWN,SDKH_JOIN_UNKNOWN,SDKH_DRAIN_UNKNOWN};
static u64 sdkh_call(IzSdkOwner*s,unsigned function,u64 a,u64 b,u64 d,u64 e){
 u32 id=iz_sdk_haptic_code[function].offset;
 IzServiceFunction target=s->bridge?iz_service_target(s->bridge,id):(IzServiceFunction)(uintptr_t)(s->sdk+id);
 return target?target(a,b,d,e,0,0,0,0):SDKM_UNAVAILABLE;
}
static int sdkh_float(u32 bits,u32 low,u32 high){u32 magnitude=bits&0x7fffffffu;return (!(bits&0x80000000u)||!magnitude)&&magnitude>=low&&magnitude<=high;}
/* IEEE-754 rounding for the legacy permille request without FP registers. */
static u32 sdkh_amplitude(u32 permille){
 if(!permille)return 0;u32 exponent=127;while(permille<1000){permille*=2;exponent--;}
 u64 scaled=(u64)permille<<23;u32 fraction=(u32)(scaled/1000),remainder=(u32)(scaled%1000);if(remainder>500||(remainder==500&&(fraction&1)))fraction++;
 return (exponent<<23)+(fraction-(1u<<23));
}
static int sdkh_cancelled(IzSdkHaptic*h){return __atomic_load_n(&h->cancel,__ATOMIC_ACQUIRE)!=0;}
static void*sdkh_worker(void*context){
 IzSdkOwner*s=context;IzSdkHaptic*h=&s->haptic;u32 started=0,error=0;const u32 silence[]={0,0x43200000u,0,0x43a00000u};
 for(u32 i=0;i<h->step_count&&!error&&!sdkh_cancelled(h);i++){
  const IzSdkHapticStep*step=&h->steps[i];for(u32 left=step->milliseconds;left&&!error&&!sdkh_cancelled(h);){
   for(u32 j=0;j<h->handle_count;j++){started|=1u<<j;error=(u32)sdkh_call(s,SDKH_SEND,P(&h->handles[j]),P(step->value),0,0);if(error)break;}
   if(!error)error=(u32)sdkh_call(s,SDKH_READ,P(h->actual),P(h->handles),0,0);if(error||sdkh_cancelled(h))break;
   u32 milliseconds=left>5?5:left;sdkh_call(s,SDKH_SLEEP,(u64)milliseconds*1000000,0,0,0);left-=milliseconds;
  }
 }
 for(u32 j=0;j<h->handle_count;j++)if(started&(1u<<j)){u32 rc=(u32)sdkh_call(s,SDKH_SEND,P(&h->handles[j]),P(silence),0,0);if(rc&&!h->stop_error)h->stop_error=rc;}
 h->error=error?error:sdkh_cancelled(h)?SDKM_CANCELLED:0;__atomic_store_n(&h->done,1,__ATOMIC_RELEASE);return 0;
}
static void sdkh_retain(IzContext*c,u32 error){
 IzSdkOwner*s=&c->owner.sdk;__atomic_store_n(&s->haptic.cancel,1,__ATOMIC_RELEASE);s->retained=1;s->close_error=error;s->error=error;sdkm_reply(c,3,"Native haptic ownership retained",0);
}
static int sdkh_poll(IzContext*c){
 IzSdkOwner*s=&c->owner.sdk;IzSdkHaptic*h=&s->haptic;if(!h->thread||s->retained)return 0;
 u64 wait=sdkh_call(s,SDKH_WAIT,h->thread,0,0,0);if(!wait)return 1;
 if(wait!=1||!__atomic_load_n(&h->done,__ATOMIC_ACQUIRE)){sdkh_retain(c,SDKH_WAIT_UNKNOWN);return 0;}
 u64 result=~0ull;if(sdkh_call(s,SDKH_JOIN,h->thread,P(&result),0,0)||result){sdkh_retain(c,SDKH_JOIN_UNKNOWN);return 0;}
 h->thread=0;s->error=h->error;if(h->stop_error){sdkh_retain(c,h->stop_error);return 0;}
 if(s->ending||h->error){sdkm_reply(c,3,s->ending||h->error==SDKM_CANCELLED?"Haptic request cancelled":"Native haptic request failed",0);return 0;}
 u8*out=(void*)(uintptr_t)(c->controls+IZ_SDK_INPUT_AT+IZ_SDK_RESPONSE_AT);memcpy(out+32,h->actual,16);sdkm_reply(c,0,0,16);return 0;
}
static void sdkh_begin(IzContext*c,const u8*request,u32 operation){
 IzSdkOwner*s=&c->owner.sdk;IzSdkHaptic*h=&s->haptic;u32 side=GET(request,8),count=operation==1?1:GET(request,12),total=0;
 if(side>2||!count||count>32||(operation==1&&GET(request,16)>1000)){sdkm_reply(c,2,"Invalid haptic request",0);return;}
 memset(h,0,sizeof *h);h->step_count=count;
 if(operation==1){u32 amplitude=sdkh_amplitude(GET(request,16));h->steps[0]=(IzSdkHapticStep){GET(request,12),{amplitude,0x43200000u,amplitude,0x43a00000u}};}
 else memcpy(h->steps,request+32,count*sizeof *h->steps);
 for(u32 i=0;i<count;i++){IzSdkHapticStep*v=&h->steps[i];if(!v->milliseconds||v->milliseconds>500||!sdkh_float(v->value[0],0,0x3f800000u)||!sdkh_float(v->value[2],0,0x3f800000u)||!sdkh_float(v->value[1],0x42200000u,0x449c4000u)||!sdkh_float(v->value[3],0x42200000u,0x449c4000u)){sdkm_reply(c,2,"Invalid haptic step",0);return;}total+=v->milliseconds;}
 if(total>2000){sdkm_reply(c,2,"Haptic request exceeds 2000 ms",0);return;}
 if(!s->pad_connected){sdkm_reply(c,3,"Vibration device unavailable",0);return;}
 if(!sdkm_verify_table(s,iz_sdk_haptic_code,sizeof iz_sdk_haptic_code/sizeof *iz_sdk_haptic_code,&s->haptic_verified)){sdkm_reply(c,3,"Native haptic signature differs",0);return;}
 u8 allowed=0;if((u32)sdkh_call(s,SDKH_ALLOWED,P(&allowed),0,0,0)||!allowed){sdkm_reply(c,3,"System vibration disabled",0);return;}
 u32 handles[2]={0},id=s->pad_id;u32 found=(u32)sdkh_call(s,SDKH_HANDLES,P(handles),2,P(&id),s->pad_style);
 if(!found||found>2||(found==2&&handles[0]==handles[1])||(side<2&&side>=found)){sdkm_reply(c,3,"Vibration device unavailable",0);return;}
 h->handle_count=side==2?found:1;for(u32 i=0;i<h->handle_count;i++){h->handles[i]=handles[side==2?i:side];if((u32)sdkh_call(s,SDKH_INIT,P(&h->handles[i]),0,0,0)){sdkm_reply(c,3,"Vibration device initialization failed",0);return;}}
 s->pending=s->sequence;s->operation=operation;s->error=s->close_error=0;
 u64 result=sdkh_call(s,SDKH_CREATE,P(&h->thread),0,P(sdkh_worker),P(s));
 if(result&&!h->thread){sdkm_reply(c,3,"Native haptic worker creation failed",0);return;}
 if(result||!h->thread)sdkh_retain(c,SDKH_CREATE_UNKNOWN);
}
