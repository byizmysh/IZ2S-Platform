#ifndef IZ_HOST_RPC_H
#define IZ_HOST_RPC_H
/* The gate and its binding table live in retained supervisor storage. Native
 * entry thunks carry an immutable epoch, so reusing a table cannot revive an
 * old caller. No PC-written count or completion flag is an authority. */
#define IZ_HOST_RPC_MAX 128u
#define IZ_HOST_RPC_MAGIC 0x495a5247u
#define IZ_HOST_RPC_REVOKED 0x80000000ull
#define IZ_HOST_RPC_COUNT 0x7fffffffull
#define IZ_HOST_RPC_REJECT 0xfdfefdfefdfefd01ull
#define IZ_HOST_RPC_OWNER_OFFSET 0x8000u
#define IZ_HOST_RPC_STAGING_OFFSET 0x6000u
#define IZ_HOST_RPC_HISTORY_OFFSET 0x9000u
#include "host-sdk.h"
enum {IZ_HOST_RPC_BOUNDED=0,IZ_HOST_RPC_OPAQUE=1};
typedef struct IzHostRpcOwner IzHostRpcOwner;
typedef struct {IzHostRpcOwner*owner;u64 target;u32 epoch,effect;} IzHostRpcBinding;
typedef struct {u64 target;u32 effect,reserved;} IzHostRpcTarget;
typedef struct {u32 version,bytes,count,reserved;IzHostRpcTarget targets[IZ_HOST_RPC_MAX];u64 full_write_thunk,full_receipt_thunk;u32 frame_mode,reserved2;u64 full_backing,full_backing_bytes;} IzHostRpcPlan;
struct IzHostRpcOwner {
 u32 magic,count;u64 word,completed,rejected;u32 opaque,ever_bound;
 IzHostRpcBinding bindings[IZ_HOST_RPC_MAX];
 /* Packet receipts survive R3/re-registration; the serialized SDK dispatcher
  * retrieves their exact original values without consuming a result marker. */
 u64 sdk_scratch,protected_base,protected_bytes,packet_id,packet_result[2];
 u32 packet_epoch,packet_status;
 u64 full_write_thunk,full_receipt_thunk;
 /* Console-owned graphics arbitration. Mode 0 preserves legacy loans.
  * Counted SDK calls and native presentation share this private mutex. */
 u64 graphics_sleep,graphics_idle,graphics_queue;
 u32 graphics_mode,graphics_owner,graphics_fault,graphics_waiting;
 /* Monotonic retained source pages: an old RX alias must never acquire a
  * new epoch because its writable source page was reused. */
 u64 code_base,code_bytes,code_history;u32 code_used,map_identity;
 u64 map_source,map_rx,map_program_id;u32 map_epoch,map_state,map_index,map_error;
 u32 code_states[16];
 u32 claim_epoch,claim_seen;
 /* Only typed CPU jobs may overlap native frames. Their admission remains
  * counted until their native cleanup and receipt have completed. */
 u64 service_sdk;u32 cpu_jobs,service_tick_hz;
 /* Borrowed promoted SD root and fixed SDK IPC entries. They are captured
  * only by native registration, never selected by an application packet. */
 u64 service_send,service_close;u32 service_filesystem,service_reserved;
};
enum {IZ_RPC_MAP_IDLE,IZ_RPC_MAP_PREPARING,IZ_RPC_MAP_MAPPED,IZ_RPC_MAP_ACTIVE,IZ_RPC_MAP_FAILED,IZ_RPC_MAP_RETAINED,IZ_RPC_MAP_CANCELLED};
enum {IZ_RPC_PACKET_MAGIC=0x495a5250u,IZ_RPC_PACKET_HEADER=192,IZ_RPC_PACKET_CAPACITY=32768};
enum {IZ_RPC_CALL=1,IZ_RPC_WRITE,IZ_RPC_FILL,IZ_RPC_WRITE_CALL,IZ_RPC_PAIR,IZ_RPC_COPY,IZ_RPC_HAPTICS,IZ_RPC_UDP,IZ_RPC_READ_FILE};
enum {IZ_RPC_OK=0,IZ_RPC_REVOKED,IZ_RPC_INVALID,IZ_RPC_STALE,IZ_RPC_UNCERTAIN};
enum {IZ_RPC_GRAPHICS_FREE,IZ_RPC_GRAPHICS_SDK,IZ_RPC_GRAPHICS_NATIVE,IZ_RPC_GRAPHICS_POISONED};
typedef struct {
 u32 magic,version,bytes,op;u64 id,destination,source;
 u32 length,first,second,value,reserved[2];u64 args[2][8];
} IzHostRpcPacket;
_Static_assert(sizeof(IzHostRpcPacket)==IZ_RPC_PACKET_HEADER,"Guarded packet header ABI");
_Static_assert(sizeof(IzHostRpcBinding)==24,"Host RPC binding ABI");
_Static_assert(sizeof(IzHostRpcPlan)==2104,"Host RPC plan ABI");
_Static_assert(sizeof(IzHostRpcOwner)<=4096,"Retained RPC owner page");
static inline u32 hr_epoch(const IzHostRpcOwner*g){return (u32)(__atomic_load_n(&g->word,__ATOMIC_ACQUIRE)>>32);}
static inline u32 hr_inflight(const IzHostRpcOwner*g){return (u32)(__atomic_load_n(&g->word,__ATOMIC_ACQUIRE)&IZ_HOST_RPC_COUNT);}
static inline int hr_revoked(const IzHostRpcOwner*g){return (__atomic_load_n(&g->word,__ATOMIC_ACQUIRE)&IZ_HOST_RPC_REVOKED)!=0;}
static inline int hr_admit(IzHostRpcOwner*g,u32 epoch){
 u64 old=__atomic_load_n(&g->word,__ATOMIC_ACQUIRE);
 for(;;){if((u32)(old>>32)!=epoch||(old&IZ_HOST_RPC_REVOKED)||(old&IZ_HOST_RPC_COUNT)==IZ_HOST_RPC_COUNT)return 0;
  if(__atomic_compare_exchange_n(&g->word,&old,old+1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return 1;}
}
static inline void hr_revoke(IzHostRpcOwner*g,u32 epoch){
 u64 old=__atomic_load_n(&g->word,__ATOMIC_ACQUIRE);
 while((u32)(old>>32)==epoch&&!(old&IZ_HOST_RPC_REVOKED))if(__atomic_compare_exchange_n(&g->word,&old,old|IZ_HOST_RPC_REVOKED,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))break;
}
static inline void hr_complete(IzHostRpcOwner*g){__atomic_add_fetch(&g->completed,1,__ATOMIC_RELAXED);__atomic_fetch_sub(&g->word,1,__ATOMIC_RELEASE);}
static inline int hr_idle(const IzHostRpcOwner*g,u32 epoch){return __atomic_load_n(&g->word,__ATOMIC_ACQUIRE)==(((u64)epoch<<32)|IZ_HOST_RPC_REVOKED);}
static inline int hr_local_return(const IzHostRpcOwner*g,u32 epoch){return hr_idle(g,epoch)&&!__atomic_load_n(&g->cpu_jobs,__ATOMIC_ACQUIRE)&&!__atomic_load_n(&g->opaque,__ATOMIC_ACQUIRE)&&(!g->graphics_mode||(!__atomic_load_n(&g->graphics_owner,__ATOMIC_ACQUIRE)&&!__atomic_load_n(&g->graphics_fault,__ATOMIC_ACQUIRE)));}
/* A separate claim register is the publication point for guarded loans.
 * The original host drains and seals raw SDK mutation before this write.
 * Legacy display claims never acquire this early cancellation permission. */
static inline void hr_guarded_claim(IzContext*c,IzHostRpcOwner*g){
 u32 claim=__atomic_load_n(&c->host_rpc_claim,__ATOMIC_ACQUIRE);if(!claim||claim<=g->claim_seen)return;g->claim_seen=claim;
 if(!c->host_mode||c->state!=2||c->active||c->poisoned||c->host_display_ready||g->magic!=IZ_HOST_RPC_MAGIC||claim<=hr_epoch(g)||claim<=c->host_display_release||!hr_revoked(g)||hr_inflight(g)||g->cpu_jobs||g->opaque||g->graphics_owner||g->graphics_fault||g->map_state==IZ_RPC_MAP_RETAINED||((claim&1u)?(c->host_display_request!=claim||c->host_display_request==c->host_display_release||(!c->host_launch&&!c->host_remove)):(c->host_display_request!=c->host_display_release||claim<=c->host_display_observed))){
  if(claim>c->host_display_rejected)c->host_display_rejected=claim;if(claim>c->host_display_observed)c->host_display_observed=claim;
 }else{g->claim_epoch=claim;c->host_frames_error=0;__atomic_store_n(&c->host_display_claim,claim,__ATOMIC_RELEASE);}
 __atomic_store_n(&c->host_rpc_claim_ack,claim,__ATOMIC_RELEASE);
}
static inline void hr_graphics_poison(IzHostRpcOwner*g,u32 error){
 __atomic_store_n(&g->graphics_fault,error?error:0xe96a,__ATOMIC_RELEASE);__atomic_store_n(&g->opaque,1,__ATOMIC_RELEASE);hr_revoke(g,hr_epoch(g));__atomic_store_n(&g->graphics_owner,IZ_RPC_GRAPHICS_POISONED,__ATOMIC_RELEASE);
}
static inline u32 hr_graphics_enter(IzHostRpcOwner*g,u32 epoch){
 if(!g->graphics_mode)return IZ_RPC_OK;
 __atomic_add_fetch(&g->graphics_waiting,1,__ATOMIC_RELAXED);u32 status=IZ_RPC_OK;
 for(;;){
  if(__atomic_load_n(&g->graphics_fault,__ATOMIC_ACQUIRE)){status=IZ_RPC_UNCERTAIN;break;}
  if(hr_epoch(g)!=epoch||hr_revoked(g)){status=IZ_RPC_REVOKED;break;}
  u32 expected=IZ_RPC_GRAPHICS_FREE;
  if(__atomic_compare_exchange_n(&g->graphics_owner,&expected,IZ_RPC_GRAPHICS_SDK,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
   if(hr_epoch(g)!=epoch||hr_revoked(g)){__atomic_store_n(&g->graphics_owner,IZ_RPC_GRAPHICS_FREE,__ATOMIC_RELEASE);status=IZ_RPC_REVOKED;}break;
  }
  if(!g->graphics_sleep){hr_graphics_poison(g,0xe96b);status=IZ_RPC_UNCERTAIN;break;}
  ((IzFunction)(uintptr_t)g->graphics_sleep)(1000000,0,0,0,0,0,0,0);
 }
 __atomic_sub_fetch(&g->graphics_waiting,1,__ATOMIC_RELAXED);return status;
}
static inline u32 hr_graphics_leave(IzHostRpcOwner*g,int called){
 if(!g->graphics_mode)return IZ_RPC_OK;
 if(__atomic_load_n(&g->graphics_owner,__ATOMIC_ACQUIRE)!=IZ_RPC_GRAPHICS_SDK){hr_graphics_poison(g,0xe96a);return IZ_RPC_UNCERTAIN;}
 if(called){u32 rc=(u32)((IzFunction)(uintptr_t)g->graphics_idle)(g->graphics_queue,0,0,0,0,0,0,0);if(rc){hr_graphics_poison(g,rc);return IZ_RPC_UNCERTAIN;}}
 __atomic_store_n(&g->graphics_owner,IZ_RPC_GRAPHICS_FREE,__ATOMIC_RELEASE);return IZ_RPC_OK;
}
static inline int hr_graphics_native_begin(IzHostRpcOwner*g,u32 epoch){
 if(!g->graphics_mode||hr_epoch(g)!=epoch||hr_revoked(g)||hr_inflight(g)!=__atomic_load_n(&g->cpu_jobs,__ATOMIC_ACQUIRE)||__atomic_load_n(&g->opaque,__ATOMIC_ACQUIRE)||__atomic_load_n(&g->graphics_fault,__ATOMIC_ACQUIRE))return 0;
 u32 expected=IZ_RPC_GRAPHICS_FREE;if(!__atomic_compare_exchange_n(&g->graphics_owner,&expected,IZ_RPC_GRAPHICS_NATIVE,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return 0;
 if(hr_epoch(g)!=epoch||hr_revoked(g)||hr_inflight(g)!=__atomic_load_n(&g->cpu_jobs,__ATOMIC_ACQUIRE)){__atomic_store_n(&g->graphics_owner,IZ_RPC_GRAPHICS_FREE,__ATOMIC_RELEASE);return 0;}return 1;
}
static inline u32 hr_graphics_native_end(IzHostRpcOwner*g,u32 error){
 if(error){hr_graphics_poison(g,error);return error;}
 u32 expected=IZ_RPC_GRAPHICS_NATIVE;if(!__atomic_compare_exchange_n(&g->graphics_owner,&expected,IZ_RPC_GRAPHICS_FREE,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED)){hr_graphics_poison(g,0xe96a);return 0xe96a;}return 0;
}
static inline void hr_initialize(IzHostRpcOwner*g){memset(g,0,sizeof *g);g->magic=IZ_HOST_RPC_MAGIC;g->word=IZ_HOST_RPC_REVOKED;for(u32 i=0;i<IZ_HOST_RPC_MAX;i++)g->bindings[i].owner=g;}
static inline int hr_span(IzContext*c,u64 address,u64 bytes){return c->supervisor_base>=4096&&c->supervisor_base<(1ull<<39)&&c->supervisor_bytes<=(1ull<<39)-c->supervisor_base&&address>=c->supervisor_base&&address<c->supervisor_base+c->supervisor_bytes&&bytes<=c->supervisor_base+c->supervisor_bytes-address;}
static inline void hr_status(IzContext*c,IzHostRpcOwner*g){
 u64 word=__atomic_load_n(&g->word,__ATOMIC_ACQUIRE);c->host_rpc_epoch=(u32)(word>>32);c->host_rpc_inflight=(u32)(word&IZ_HOST_RPC_COUNT);c->host_rpc_opaque=__atomic_load_n(&g->opaque,__ATOMIC_ACQUIRE);
}
static inline u32 hr_code_state(const IzHostRpcOwner*g,u32 index){return (g->code_states[index/16]>>((index%16)*2))&3u;}
static inline void hr_code_mark(IzHostRpcOwner*g,u32 index,u32 state){u32 shift=(index%16)*2;g->code_states[index/16]=(g->code_states[index/16]&~(3u<<shift))|(state<<shift);}
static inline u32 hr_targets(IzContext*c,const IzHostRpcPlan*p){
 if(p->count>IZ_HOST_RPC_MAX)return 0xe961;
 for(u32 i=0;i<IZ_HOST_RPC_MAX;i++){
  const IzHostRpcTarget*t=&p->targets[i];if(i>=p->count){if(t->target||t->effect||t->reserved)return 0xe962;continue;}
  if(t->target<4096||t->target>=(1ull<<39)||t->target%4||t->effect>IZ_HOST_RPC_OPAQUE||t->reserved)return 0xe962;
  for(u32 j=0;j<i;j++)if(t->target==p->targets[j].target)return 0xe962;
  if(t->effect==IZ_HOST_RPC_BOUNDED){int known=iz_host_sdk_target(c,t->target);for(u32 j=0;j<F_COUNT&&!known;j++)if(j!=F_SEND&&c->functions[j]==t->target){known=1;break;}if(!known)return 0xe963;}
 }return 0;
}
static inline u32 hr_open(IzContext*c,IzHostRpcOwner*g,u32 epoch,const IzHostRpcPlan*p){
 u64 old=__atomic_load_n(&g->word,__ATOMIC_ACQUIRE);
 if(g->magic!=IZ_HOST_RPC_MAGIC||!epoch||epoch<=(u32)(old>>32)||!(old&IZ_HOST_RPC_REVOKED)||(old&IZ_HOST_RPC_COUNT)||p->version!=1||p->bytes!=sizeof *p||p->count>IZ_HOST_RPC_MAX||p->reserved||p->reserved2||p->frame_mode>1)return 0xe961;
 if(__atomic_load_n(&g->opaque,__ATOMIC_ACQUIRE)||__atomic_load_n(&g->cpu_jobs,__ATOMIC_ACQUIRE)||__atomic_load_n(&g->graphics_owner,__ATOMIC_ACQUIRE)||__atomic_load_n(&g->graphics_fault,__ATOMIC_ACQUIRE))return 0xe968;
 if(g->map_state==IZ_RPC_MAP_RETAINED)return 0xe976;
 if(p->frame_mode&&(!c->queue||!c->functions[F_SLEEP]||!c->functions[F_QUEUE_IDLE]))return 0xe969;
 if((p->full_write_thunk==0)!=(p->full_receipt_thunk==0)||(p->full_write_thunk&&(p->full_write_thunk<4096||p->full_receipt_thunk<4096||p->full_write_thunk>=(1ull<<39)||p->full_receipt_thunk>=(1ull<<39)||p->full_write_thunk%4||p->full_receipt_thunk%4||p->full_write_thunk==p->full_receipt_thunk)))return 0xe962;
 if(p->full_backing){
  u64 base=c->host_rpc_code_base,bytes=c->host_rpc_code_bytes;
  int mapped=g->map_state==IZ_RPC_MAP_MAPPED&&g->map_epoch==epoch&&g->map_source==p->full_backing&&g->map_rx==p->full_write_thunk&&g->map_index<g->code_used;
  u32 index=mapped?g->map_index:g->code_used;
  if(!p->full_write_thunk||p->full_backing_bytes!=4096||base%4096||!bytes||bytes%4096||bytes>1048576||!hr_span(c,base,bytes)||!hr_span(c,c->supervisor_base+IZ_HOST_RPC_HISTORY_OFFSET,4096)||c->stack<c->supervisor_base||!c->stack_bytes||c->stack_bytes>base-c->supervisor_base||c->stack>base-c->stack_bytes||index>=bytes/4096||g->code_used>bytes/4096||p->full_backing!=base+(u64)index*4096||(g->code_used&&(g->code_base!=base||g->code_bytes!=bytes||g->code_history!=c->supervisor_base+IZ_HOST_RPC_HISTORY_OFFSET)))return 0xe96c;
  if(p->full_write_thunk%4096||p->full_receipt_thunk!=p->full_write_thunk+80)return 0xe96d;
  for(u32 i=0;i<g->code_used;i++){const u64*h=(const void*)(uintptr_t)(g->code_history+(u64)i*16);u32 state=hr_code_state(g,i);if(!state||!h[1])return 0xe96d;if(mapped&&i==index){if(state!=3||h[0]!=p->full_write_thunk||h[1]!=epoch)return 0xe96d;continue;}if(h[1]>=epoch)return 0xe96d;if(state==1){if(h[0])return 0xe96d;continue;}if(h[0]<4096||h[0]>=(1ull<<39)-4096||h[0]%4096||h[0]==p->full_write_thunk)return 0xe96d;}
 }else if(p->full_backing_bytes||g->code_used)return 0xe96c;
 u32 error=hr_targets(c,p);if(error)return error;
 for(u32 i=0;i<p->count;i++){g->bindings[i].target=p->targets[i].target;g->bindings[i].epoch=epoch;g->bindings[i].effect=p->targets[i].effect;}
 g->count=p->count;g->ever_bound|=p->count!=0;__atomic_store_n(&g->opaque,0,__ATOMIC_RELEASE);
 g->full_write_thunk=p->full_write_thunk;g->full_receipt_thunk=p->full_receipt_thunk;
 g->graphics_sleep=c->functions[F_SLEEP];g->graphics_idle=c->functions[F_QUEUE_IDLE];g->graphics_queue=c->queue;g->graphics_mode=p->frame_mode;
 g->service_sdk=c->sdk;g->service_tick_hz=c->tick_hz;
 g->service_send=c->functions[F_SEND];g->service_close=c->functions[F_CLOSE];g->service_filesystem=c->filesystem;g->service_reserved=0;
 if(p->full_backing){if(g->map_state==IZ_RPC_MAP_MAPPED&&g->map_epoch==epoch&&g->map_source==p->full_backing&&g->map_rx==p->full_write_thunk){g->map_state=IZ_RPC_MAP_ACTIVE;}else{u64 history=c->supervisor_base+IZ_HOST_RPC_HISTORY_OFFSET,*h=(void*)(uintptr_t)(history+(u64)g->code_used*16);h[0]=p->full_write_thunk;h[1]=epoch;g->code_base=c->host_rpc_code_base;g->code_bytes=c->host_rpc_code_bytes;g->code_history=history;hr_code_mark(g,g->code_used,3);g->code_used++;}}
 __atomic_store_n(&g->word,(u64)epoch<<32,__ATOMIC_RELEASE);return 0;
}
static inline void hr_offer(IzContext*c,IzHostRpcOwner*g,u32 lease){
 u32 offer=__atomic_load_n(&c->host_rpc_offer,__ATOMIC_ACQUIRE);if(offer!=lease||offer==c->host_rpc_ack)return;
 u32 error=0xe964;if(c->host_display_ready==lease&&c->host_display_release!=lease&&hr_span(c,c->host_rpc_staging,sizeof(IzHostRpcPlan))){IzHostRpcPlan plan=*(IzHostRpcPlan*)(uintptr_t)c->host_rpc_staging;error=hr_open(c,g,lease,&plan);}
 c->host_rpc_error=error;if(!error){c->host_rpc_count=g->count;c->host_rpc_bindings=(u64)(uintptr_t)g->bindings;}hr_status(c,g);__atomic_store_n(&c->host_rpc_ack,offer,__ATOMIC_RELEASE);
}
__attribute__((visibility("hidden"))) u64 iz_host_rpc_dispatch(u64,u64,u64,u64,u64,u64,u64,u64,IzHostRpcBinding*,u64);
__attribute__((visibility("hidden"))) u64 iz_host_rpc_packet(IzHostRpcOwner*,u64,const void*,u64);
__attribute__((visibility("hidden"))) u64 iz_host_rpc_receipt(IzHostRpcOwner*,u64,u64,u64);
__attribute__((visibility("hidden"))) u64 iz_host_rpc_full_write(u64,u64,u64,u64,u64,u64,u64,u64,IzHostRpcOwner*,u64);
__attribute__((visibility("hidden"))) u64 iz_host_rpc_full_receipt(u64,u64,u64,u64,u64,u64,u64,u64,IzHostRpcOwner*,u64);
#endif
