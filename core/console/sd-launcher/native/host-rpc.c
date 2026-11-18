#include "abi.h"
#include "host-rpc.h"
/* Ordinary AAPCS64 entry. Thunks preserve all eight argument registers, put
 * binding/epoch in stack arguments 9/10, and retain the caller's SDK TLS. */
u64 iz_host_rpc_dispatch(u64 a,u64 b,u64 d,u64 e,u64 f,u64 h,u64 i,u64 j,IzHostRpcBinding*binding,u64 epoch){
 if(!binding||!epoch||epoch>0xffffffffu)return IZ_HOST_RPC_REJECT;
 IzHostRpcOwner*g=binding->owner;if(!g||g->magic!=IZ_HOST_RPC_MAGIC)return IZ_HOST_RPC_REJECT;
 if(!hr_admit(g,(u32)epoch)){__atomic_add_fetch(&g->rejected,1,__ATOMIC_RELAXED);return IZ_HOST_RPC_REJECT;}
 uintptr_t at=(uintptr_t)binding,start=(uintptr_t)g->bindings;
 if(at<start||at-start>=(u64)g->count*sizeof *binding||(at-start)%sizeof *binding||binding->epoch!=(u32)epoch||binding->effect>IZ_HOST_RPC_OPAQUE){__atomic_store_n(&g->opaque,1,__ATOMIC_RELEASE);hr_complete(g);return IZ_HOST_RPC_REJECT;}
 u64 target=binding->target;if(target<4096||target>=(1ull<<39)||target%4||target==(u64)(uintptr_t)iz_host_rpc_dispatch||target==(u64)(uintptr_t)iz_host_rpc_packet||target==(u64)(uintptr_t)iz_host_rpc_receipt||target==(u64)(uintptr_t)iz_host_rpc_full_write||target==(u64)(uintptr_t)iz_host_rpc_full_receipt){__atomic_store_n(&g->opaque,1,__ATOMIC_RELEASE);hr_complete(g);return IZ_HOST_RPC_REJECT;}
 if(hr_graphics_enter(g,(u32)epoch)){hr_complete(g);return IZ_HOST_RPC_REJECT;}
 if(binding->effect==IZ_HOST_RPC_OPAQUE)__atomic_store_n(&g->opaque,1,__ATOMIC_RELEASE);
 u64 result=((IzFunction)(uintptr_t)target)(a,b,d,e,f,h,i,j);u32 finished=hr_graphics_leave(g,1);hr_complete(g);return finished?IZ_HOST_RPC_REJECT:result;
}
static int packet_range(u64 address,u64 bytes){return address>=4096&&address<(1ull<<39)&&bytes<=(1ull<<39)-address;}
static int packet_overlap(u64 a,u64 n,u64 b,u64 m){return n&&m&&a<b+m&&b<a+n;}
static int packet_writable(IzHostRpcOwner*g,u64 address,u64 bytes){
 return packet_range(address,bytes)&&!packet_overlap(address,bytes,(u64)(uintptr_t)g,sizeof *g)&&!packet_overlap(address,bytes,g->protected_base,g->protected_bytes)&&!packet_overlap(address,bytes,g->sdk_scratch+0x40000,0x8080);
}
/* Ordinary CPU memory only, verified by the host mapping guard. Do not call
 * SDK memcpy through an unfenced pointer, or copy the protocol header as data. */
typedef u64 RpcUnalignedWord __attribute__((aligned(1),may_alias));
static void packet_copy(u64 destination,u64 source,u64 bytes){
 u8*d=(void*)(uintptr_t)destination;const u8*s=(void*)(uintptr_t)source;
 if(destination>source&&destination-source<bytes){while(bytes){bytes--;d[bytes]=s[bytes];}return;}
 while(bytes>=32){u64 a=*(const RpcUnalignedWord*)s,b=*(const RpcUnalignedWord*)(s+8),c=*(const RpcUnalignedWord*)(s+16),e=*(const RpcUnalignedWord*)(s+24);*(RpcUnalignedWord*)d=a;*(RpcUnalignedWord*)(d+8)=b;*(RpcUnalignedWord*)(d+16)=c;*(RpcUnalignedWord*)(d+24)=e;d+=32;s+=32;bytes-=32;}while(bytes--)*d++=*s++;
}
static void packet_fill(u64 destination,u32 value,u64 bytes){
 u8*d=(void*)(uintptr_t)destination;u8 byte=(u8)value;u64 word=(u64)byte*0x0101010101010101ull;
 while(bytes>=32){*(RpcUnalignedWord*)d=word;*(RpcUnalignedWord*)(d+8)=word;*(RpcUnalignedWord*)(d+16)=word;*(RpcUnalignedWord*)(d+24)=word;d+=32;bytes-=32;}while(bytes--)*d++=byte;
}
static int packet_target(IzHostRpcOwner*g,u32 epoch,u32 index){
 if(index>=g->count)return 0;const IzHostRpcBinding*b=&g->bindings[index];u64 target=b->target;
 return b->owner==g&&b->epoch==epoch&&b->effect<=IZ_HOST_RPC_OPAQUE&&packet_range(target,4)&&!(target%4)&&target!=(u64)(uintptr_t)iz_host_rpc_dispatch&&target!=(u64)(uintptr_t)iz_host_rpc_packet&&target!=(u64)(uintptr_t)iz_host_rpc_receipt&&target!=(u64)(uintptr_t)iz_host_rpc_full_write&&target!=(u64)(uintptr_t)iz_host_rpc_full_receipt;
}
static u64 packet_call(IzHostRpcOwner*g,u32 index,const u64*a){return ((IzFunction)(uintptr_t)g->bindings[index].target)(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7]);}
typedef struct {u32 milliseconds,value[4];} RpcHapticStep;
_Static_assert(sizeof(RpcHapticStep)==20,"Native haptic step ABI");
static int haptic_number(u32 bits,u32 low,u32 high){u32 magnitude=bits&0x7fffffffu;return (!(bits&0x80000000u)||!magnitude)&&magnitude>=low&&magnitude<=high;}
static u32 packet_haptics(IzHostRpcOwner*g,u32 epoch,const IzHostRpcPacket*p,const void*data,u64 bytes,u64*result){
 RpcHapticStep steps[32];u32 handles[2],actual[4]={0},after[4]={0},silence[4]={0,0x43200000u,0,0x43a00000u},started=0,total=0,done=0,error=0,status=0;
 if(!g->graphics_mode||!g->graphics_sleep||g->service_sdk<4096||g->service_sdk>=(1ull<<39)-0x5e1000ull||p->destination||p->source||p->value<1||p->value>2||!p->length||p->length>32||bytes!=sizeof *p+(u64)p->length*sizeof *steps||!packet_target(g,epoch,p->first)||!packet_target(g,epoch,p->second)||g->bindings[p->first].effect||g->bindings[p->second].effect||g->bindings[p->first].target!=g->service_sdk+IZ_HAPTIC_SEND_OFFSET||g->bindings[p->second].target!=g->service_sdk+IZ_HAPTIC_READ_OFFSET)return IZ_RPC_INVALID;
 for(unsigned i=0;i<8;i++)if(p->args[1][i]||(i>=p->value&&p->args[0][i])||(i<p->value&&p->args[0][i]>0xffffffffu))return IZ_RPC_INVALID;
 handles[0]=(u32)p->args[0][0];handles[1]=(u32)p->args[0][1];if(p->value==2&&handles[0]==handles[1])return IZ_RPC_INVALID;
 memcpy(steps,data,p->length*sizeof *steps);
 for(u32 i=0;i<p->length;i++){const RpcHapticStep*s=&steps[i];if(!s->milliseconds||s->milliseconds>500||!haptic_number(s->value[0],0,0x3f800000u)||!haptic_number(s->value[2],0,0x3f800000u)||!haptic_number(s->value[1],0x42200000u,0x449c4000u)||!haptic_number(s->value[3],0x42200000u,0x449c4000u))return IZ_RPC_INVALID;total+=s->milliseconds;}if(total>2000)return IZ_RPC_INVALID;
 IzFunction send=(IzFunction)(uintptr_t)g->bindings[p->first].target,read=(IzFunction)(uintptr_t)g->bindings[p->second].target,sleep=(IzFunction)(uintptr_t)g->graphics_sleep;
 __atomic_add_fetch(&g->cpu_jobs,1,__ATOMIC_RELEASE);
 for(u32 i=0;i<p->length&&!status&&!error;i++){
  for(u32 left=steps[i].milliseconds;left&&!status&&!error;){
   if(hr_epoch(g)!=epoch||hr_revoked(g)){status=IZ_RPC_REVOKED;break;}
   for(u32 j=0;j<p->value;j++){started|=1u<<j;error=(u32)send((u64)(uintptr_t)&handles[j],(u64)(uintptr_t)steps[i].value,0,0,0,0,0,0);if(error)break;}
   if(!error)error=(u32)read((u64)(uintptr_t)actual,(u64)(uintptr_t)handles,0,0,0,0,0,0);if(error)break;
   u32 ms=left>5?5:left;sleep((u64)ms*1000000,0,0,0,0,0,0,0);left-=ms;
  }if(!status&&!error)done++;
 }
 /* Stop every handle which may have received a value, even after revocation
  * or a known send/read error. This cleanup does not need another PC packet. */
 for(u32 j=0;j<p->value;j++)if(started&(1u<<j)){u32 rc=(u32)send((u64)(uintptr_t)&handles[j],(u64)(uintptr_t)silence,0,0,0,0,0,0);if(rc){if(!error)error=rc;__atomic_store_n(&g->opaque,1,__ATOMIC_RELEASE);hr_revoke(g,epoch);status=IZ_RPC_UNCERTAIN;}}
 if(started){u32 rc=(u32)read((u64)(uintptr_t)after,(u64)(uintptr_t)handles,0,0,0,0,0,0);if(rc&&!error)error=rc;}
 if(!status&&(hr_epoch(g)!=epoch||hr_revoked(g)))status=IZ_RPC_REVOKED;
 u32 out[12]={0x495a4850u,1,done,error};memcpy(out+4,actual,16);memcpy(out+8,after,16);memcpy((void*)(uintptr_t)(g->sdk_scratch+0x4040),out,sizeof out);result[0]=error?3:0;result[1]=done;
 __atomic_sub_fetch(&g->cpu_jobs,1,__ATOMIC_RELEASE);return status;
}
#include "host-udp.h"
#include "host-files.h"
u64 iz_host_rpc_packet(IzHostRpcOwner*g,u64 epoch,const void*source,u64 bytes){
 if(!g||g->magic!=IZ_HOST_RPC_MAGIC||!epoch||epoch>0xffffffffu||!g->sdk_scratch||!packet_range(g->sdk_scratch,0x80000)||(u64)(uintptr_t)source!=g->sdk_scratch+0x40080||bytes<sizeof(IzHostRpcPacket)||bytes>IZ_RPC_PACKET_CAPACITY)return IZ_RPC_INVALID;
 IzHostRpcPacket p;memcpy(&p,source,sizeof p);if(p.magic!=IZ_RPC_PACKET_MAGIC||p.version!=1||p.bytes!=bytes||!p.id||p.reserved[0]||p.reserved[1])return IZ_RPC_INVALID;
 if(!hr_admit(g,(u32)epoch)){__atomic_add_fetch(&g->rejected,1,__ATOMIC_RELAXED);return IZ_RPC_REVOKED;}
 if(g->packet_epoch==(u32)epoch&&p.id<=__atomic_load_n(&g->packet_id,__ATOMIC_ACQUIRE)){hr_complete(g);return IZ_RPC_STALE;}
 int first=p.op==IZ_RPC_CALL||p.op==IZ_RPC_WRITE_CALL||p.op==IZ_RPC_PAIR,second=p.op==IZ_RPC_PAIR;
 u32 status=IZ_RPC_OK;u64 result[2]={0,0};
 if(p.op<IZ_RPC_CALL||p.op>IZ_RPC_READ_FILE||(first&&!packet_target(g,(u32)epoch,p.first))||(second&&!packet_target(g,(u32)epoch,p.second)))status=IZ_RPC_INVALID;
 if(p.op==IZ_RPC_WRITE||p.op==IZ_RPC_WRITE_CALL){if(bytes!=sizeof p+(u64)p.length||!packet_writable(g,p.destination,p.length))status=IZ_RPC_INVALID;}
 else if(p.op!=IZ_RPC_HAPTICS&&p.op!=IZ_RPC_UDP&&p.op!=IZ_RPC_READ_FILE&&bytes!=sizeof p)status=IZ_RPC_INVALID;
 if((p.op==IZ_RPC_FILL||p.op==IZ_RPC_COPY)&&!packet_writable(g,p.destination,p.length))status=IZ_RPC_INVALID;
 if(p.op==IZ_RPC_COPY&&!packet_range(p.source,p.length))status=IZ_RPC_INVALID;
 if(p.op==IZ_RPC_HAPTICS){if(!status)status=packet_haptics(g,(u32)epoch,&p,(const u8*)source+sizeof p,bytes,result);}
 else if(p.op==IZ_RPC_UDP){if(!status)status=packet_udp(g,(u32)epoch,&p,(const u8*)source+sizeof p,bytes,result);}
 else if(p.op==IZ_RPC_READ_FILE){if(!status)status=packet_read_file(g,(u32)epoch,&p,(const u8*)source+sizeof p,bytes,result);}
 else{if(!status)status=hr_graphics_enter(g,(u32)epoch);
 if(!status){
  /* Both pair targets and the complete write are validated before any side
   * effect. One admission spans write+call or both pair calls. */
  if((first&&g->bindings[p.first].effect==IZ_HOST_RPC_OPAQUE)||(second&&g->bindings[p.second].effect==IZ_HOST_RPC_OPAQUE))__atomic_store_n(&g->opaque,1,__ATOMIC_RELEASE);
  if(p.op==IZ_RPC_WRITE||p.op==IZ_RPC_WRITE_CALL){packet_copy(p.destination,(u64)(uintptr_t)source+sizeof p,p.length);result[0]=p.destination;}
  if(p.op==IZ_RPC_FILL){packet_fill(p.destination,p.value,p.length);result[0]=p.destination;}
  if(p.op==IZ_RPC_COPY){packet_copy(p.destination,p.source,p.length);result[0]=p.destination;}
  if(first)result[0]=packet_call(g,p.first,p.args[0]);if(second)result[1]=packet_call(g,p.second,p.args[1]);status=hr_graphics_leave(g,first||second);
 }}
 g->packet_result[0]=result[0];g->packet_result[1]=result[1];g->packet_epoch=(u32)epoch;g->packet_status=status;__atomic_store_n(&g->packet_id,p.id,__ATOMIC_RELEASE);hr_complete(g);return status;
}
u64 iz_host_rpc_receipt(IzHostRpcOwner*g,u64 epoch,u64 id,u64 index){
 if(!g||g->magic!=IZ_HOST_RPC_MAGIC||index>1||!id||epoch>0xffffffffu)return IZ_HOST_RPC_REJECT;
 /* R3 can run between the two records of one SDK packet. Completed results
  * remain readable after revocation, without reopening native admission. */
 if(__atomic_load_n(&g->packet_id,__ATOMIC_ACQUIRE)!=id||g->packet_epoch!=(u32)epoch)return IZ_HOST_RPC_REJECT;return g->packet_result[index];
}
u64 iz_host_rpc_full_write(u64 destination,u64 source,u64 bytes,u64 tag,u64 x4,u64 x5,u64 x6,u64 x7,IzHostRpcOwner*g,u64 epoch){
 (void)x4;(void)x5;(void)x6;(void)x7;u64 id=tag&0x00ffffffffffffffull;u32 index=(u32)(tag>>56);
 if(!g||g->magic!=IZ_HOST_RPC_MAGIC||!g->sdk_scratch||!epoch||epoch>0xffffffffu||!id||bytes>32768||source!=g->sdk_scratch+0x40080)return IZ_RPC_INVALID;
 if(!hr_admit(g,(u32)epoch)){__atomic_add_fetch(&g->rejected,1,__ATOMIC_RELAXED);return IZ_RPC_REVOKED;}
 if(g->packet_epoch==(u32)epoch&&id<=__atomic_load_n(&g->packet_id,__ATOMIC_ACQUIRE)){hr_complete(g);return IZ_RPC_STALE;}
 u64 records[10];memcpy(records,(void*)(uintptr_t)(g->sdk_scratch+0x40000),sizeof records);u32 status=IZ_RPC_OK;u64 result=0,args[8]={0};memcpy(args,records+6,32);
 if(records[0]!=g->full_write_thunk||records[5]!=g->full_receipt_thunk||!g->full_write_thunk||records[1]!=destination||records[2]!=source||records[3]!=bytes||records[4]!=tag||!packet_writable(g,destination,bytes)||(index!=255&&!packet_target(g,(u32)epoch,index)))status=IZ_RPC_INVALID;
 if(!status)status=hr_graphics_enter(g,(u32)epoch);
 if(!status){
  if(index!=255&&g->bindings[index].effect==IZ_HOST_RPC_OPAQUE)__atomic_store_n(&g->opaque,1,__ATOMIC_RELEASE);
  packet_copy(destination,source,bytes);result=index==255?destination:packet_call(g,index,args);status=hr_graphics_leave(g,index!=255);
 }
 g->packet_result[0]=result;g->packet_result[1]=0;g->packet_epoch=(u32)epoch;g->packet_status=status;__atomic_store_n(&g->packet_id,id,__ATOMIC_RELEASE);hr_complete(g);return status;
}
u64 iz_host_rpc_full_receipt(u64 x0,u64 x1,u64 x2,u64 x3,u64 x4,u64 x5,u64 x6,u64 x7,IzHostRpcOwner*g,u64 epoch){
 (void)x0;(void)x1;(void)x2;(void)x3;(void)x4;(void)x5;(void)x6;(void)x7;
 if(!g||g->magic!=IZ_HOST_RPC_MAGIC||!g->sdk_scratch)return IZ_HOST_RPC_REJECT;
 u64 tag;memcpy(&tag,(void*)(uintptr_t)(g->sdk_scratch+0x40000+32),8);return iz_host_rpc_receipt(g,epoch,tag&0x00ffffffffffffffull,0);
}
