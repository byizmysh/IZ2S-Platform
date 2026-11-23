/* Included only by the production packet executor. No host worker, arbitrary
 * target, caller pointer or follow-up cleanup command is accepted. */
#define IZ_UDP_MAGIC 0x495a5544u
#define IZ_UDP_DONTWAIT 0x80u
enum {IZ_UDP_SUCCESS=0,IZ_UDP_SERVICE_ERROR=3,IZ_UDP_TIMEOUT=4};
enum {IZ_UDP_NOT_READY=0xe9b0,IZ_UDP_SEND_LENGTH,IZ_UDP_RECV_LENGTH,IZ_UDP_CLOCK};
static IzFunction udp_service(IzHostRpcOwner*g,u32 epoch,u32 offset){
 for(u32 i=0;i<g->count;i++)if(g->bindings[i].target==g->service_sdk+offset&&g->bindings[i].effect==IZ_HOST_RPC_BOUNDED&&packet_target(g,epoch,i))return (IzFunction)(uintptr_t)g->bindings[i].target;
 return 0;
}
static u32 packet_udp(IzHostRpcOwner*g,u32 epoch,const IzHostRpcPacket*p,const void*data,u64 bytes,u64*result){
 if(!g->graphics_mode||!g->graphics_sleep||!g->service_tick_hz||g->service_sdk<4096||g->service_sdk>=(1ull<<39)-0x5e1000ull||p->destination||p->source||p->first||p->second||!p->length||p->length>1024||!p->value||p->value>5000||p->args[0][0]>0xffffffffu||!p->args[0][1]||p->args[0][1]>65535||bytes!=sizeof *p+p->length)return IZ_RPC_INVALID;
 for(unsigned i=0;i<8;i++)if(p->args[1][i]||(i>=2&&p->args[0][i]))return IZ_RPC_INVALID;
 const u32 offsets[]={IZ_UDP_INIT_OFFSET,IZ_UDP_SOCKET_OFFSET,IZ_UDP_SEND_OFFSET,IZ_UDP_RECV_OFFSET,IZ_UDP_CLOSE_OFFSET,IZ_UDP_ERRNO_OFFSET,IZ_UDP_TICK_OFFSET};IzFunction functions[7];
 for(unsigned i=0;i<7;i++){functions[i]=udp_service(g,epoch,offsets[i]);if(!functions[i])return IZ_RPC_INVALID;}
 IzFunction initialized=functions[0],socket=functions[1],send=functions[2],recv=functions[3],close=functions[4],last_error=functions[5],tick=functions[6],sleep=(IzFunction)(uintptr_t)g->graphics_sleep;
 u8 payload[1024],response[1024],address[16]={16,2};memcpy(payload,data,p->length);u32 ip=(u32)p->args[0][0],port=(u32)p->args[0][1];address[2]=(u8)(port>>8);address[3]=(u8)port;for(unsigned i=0;i<4;i++)address[4+i]=(u8)(ip>>(24-8*i));
 u32 status=IZ_RPC_OK,service=IZ_UDP_SUCCESS,error=0,close_result=0,flags=0,length=0,polls=0;int32_t fd=-1;
 __atomic_add_fetch(&g->cpu_jobs,1,__ATOMIC_RELEASE);
 if(hr_epoch(g)!=epoch||hr_revoked(g))status=IZ_RPC_REVOKED;
 else if(initialized(0,0,0,0,0,0,0,0)!=1){service=IZ_UDP_SERVICE_ERROR;error=IZ_UDP_NOT_READY;}
 else{
  fd=(int32_t)(u32)socket(2,2,17,0,0,0,0,0);
  if(fd<0){service=IZ_UDP_SERVICE_ERROR;error=(u32)last_error(0,0,0,0,0,0,0,0);}
  else{
   flags=1;
   if(hr_epoch(g)!=epoch||hr_revoked(g))status=IZ_RPC_REVOKED;
   else{
    int64_t sent=(int64_t)send((u32)fd,(u64)(uintptr_t)payload,p->length,IZ_UDP_DONTWAIT,(u64)(uintptr_t)address,16,0,0);
    if(sent!=(int64_t)p->length){service=IZ_UDP_SERVICE_ERROR;error=sent<0?(u32)last_error(0,0,0,0,0,0,0,0):IZ_UDP_SEND_LENGTH;}
    else{
     u64 start=tick(0,0,0,0,0,0,0,0),budget=(u64)g->service_tick_hz*p->value/1000u;u32 slept=0;
     for(;;){
      if(hr_epoch(g)!=epoch||hr_revoked(g)){status=IZ_RPC_REVOKED;break;}
      u64 now=tick(0,0,0,0,0,0,0,0);if(now<start){service=IZ_UDP_SERVICE_ERROR;error=IZ_UDP_CLOCK;break;}
      if(polls&&(now-start>=budget||slept>=p->value)){service=IZ_UDP_TIMEOUT;error=0;break;}
      int64_t n=(int64_t)recv((u32)fd,(u64)(uintptr_t)response,sizeof response,IZ_UDP_DONTWAIT,0,0,0,0);polls++;
      if(n>=0){if(n>(int64_t)sizeof response){service=IZ_UDP_SERVICE_ERROR;error=IZ_UDP_RECV_LENGTH;}else length=(u32)n;break;}
      error=(u32)last_error(0,0,0,0,0,0,0,0);if(error!=11&&error!=35){service=IZ_UDP_SERVICE_ERROR;break;}
      // Keep the existing 20 ms receive cadence, with cancellation observed
      // between 5 ms sleep slices. No extra network IPC is needed for R3.
      u32 pause=p->value-slept;if(pause>20)pause=20;
      while(pause){if(hr_epoch(g)!=epoch||hr_revoked(g)){status=IZ_RPC_REVOKED;break;}u32 ms=pause>5?5:pause;sleep((u64)ms*1000000,0,0,0,0,0,0,0);slept+=ms;pause-=ms;}
      if(status)break;
     }
    }
   }
   // Native cleanup precedes admission completion, including R3/caller loss.
   close_result=(u32)close((u32)fd,0,0,0,0,0,0,0);
   if(close_result){__atomic_store_n(&g->opaque,1,__ATOMIC_RELEASE);hr_revoke(g,epoch);status=IZ_RPC_UNCERTAIN;}
   else flags|=2;
  }
 }
 if(!status&&(hr_epoch(g)!=epoch||hr_revoked(g)))status=IZ_RPC_REVOKED;
 if(!service)error=0;u32 out[8]={IZ_UDP_MAGIC,1,length,error,close_result,polls,flags,service};u8*destination=(void*)(uintptr_t)(g->sdk_scratch+0x4040);memcpy(destination,out,sizeof out);if(length)memcpy(destination+sizeof out,response,length);result[0]=service;result[1]=length;
 __atomic_sub_fetch(&g->cpu_jobs,1,__ATOMIC_RELEASE);return status;
}
