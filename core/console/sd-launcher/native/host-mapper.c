#include "abi.h"
#include "host-mapper.h"
static void publish(IzContext*c,IzHostRpcOwner*g,u32 epoch){
 c->host_rpc_map_error=g->map_error;c->host_rpc_map_state=g->map_state;c->host_rpc_map_index=g->map_index;c->host_rpc_map_rx=g->map_rx;c->host_rpc_map_source=g->map_source;__atomic_store_n(&c->host_rpc_map_ack,epoch,__ATOMIC_RELEASE);
}
static void finish(IzContext*c,IzHostRpcOwner*g,IzHostRpcMapper*m,u32 error,u32 state){
 g->map_state=state;g->map_error=error;m->phase=0;
 if(error){
  /* No app call was admitted for this epoch. Consume it as closed so the
   * supervisor can restore its menu without freeing any possible RX alias. */
  if(hr_epoch(g)<m->offer&&hr_revoked(g)&&!hr_inflight(g)&&!g->opaque&&!g->graphics_owner&&!g->graphics_fault){g->count=0;__atomic_store_n(&g->word,((u64)m->offer<<32)|IZ_HOST_RPC_REVOKED,__ATOMIC_RELEASE);c->host_rpc_offer=m->offer;c->host_rpc_error=error;c->host_rpc_count=0;c->host_rpc_bindings=(u64)(uintptr_t)g->bindings;hr_status(c,g);__atomic_store_n(&c->host_rpc_ack,m->offer,__ATOMIC_RELEASE);}
  else if(hr_epoch(g)!=m->offer){c->poisoned=1;}
  c->last_app_error=state==IZ_RPC_MAP_CANCELLED?0:error;
 }
 if(state==IZ_RPC_MAP_RETAINED)c->ro_handle=0;
 publish(c,g,m->offer);
}
static u64 symbol(u32 which){
#ifdef __aarch64__
 return which?(u64)(uintptr_t)iz_host_rpc_full_receipt:(u64)(uintptr_t)iz_host_rpc_full_write;
#else
 return iz_host_rpc_code_symbol(which);
#endif
}
static void emit(u8*out,u64 owner,u32 epoch){
 const u32 words[]={0xd50324df,0xd10083ff,0x58000150,0xf90003f0,0x58000150,0xf90007f0,0xf9000bfe,0x58000130,0xd63f0200,0xf9400bfe,0x910083ff,0xd65f03c0};
 memset(out,0,160);for(u32 i=0;i<2;i++){u8*p=out+i*80;memcpy(p,words,sizeof words);memcpy(p+48,&owner,8);u64 token=epoch,target=symbol(i);memcpy(p+56,&token,8);memcpy(p+64,&target,8);}
}
static u32 map_page(IzContext*c,IzHostRpcOwner*g,u32*unknown){
 _Alignas(4096) u8 message[4096];memset(message,0,sizeof message);u32*p=(void*)message;u64 args[]={c->pid,g->map_source,g->map_rx,4096};
 p[0]=4;p[1]=0x80000010u;p[2]=3;p[5]=0xffff8001u;p[8]=0x49434653;p[10]=0x524f0012;memcpy(message+48,args,sizeof args);*unknown=1;
 u32 rc=(u32)CALL(c,F_SEND,message,4096,c->ro_handle,0,0,0,0,0);if(rc)return rc;
 if(p[0]||!(p[1]&0x80000000u)||p[2]!=1||p[8]!=0x4f434653||p[9]||p[11]||((u64)p[3]|((u64)p[4]<<32))!=c->ro_pid||(p[1]&1023)<24)return 0xe974;
 const u64*v=(const void*)(message+48);if(v[0]!=c->pid||v[2]||(g->map_identity&&v[1]!=g->map_program_id))return 0xe974;
 if(p[10]){if(v[5]||(v[4]&&(v[3]||v[4]!=p[10])))return p[10];*unknown=0;return p[10];}
 if(v[3]||v[4]||v[5])return 0xe974;g->map_program_id=v[1];g->map_identity=1;*unknown=0;return 0;
}
static int region(const IzAddressRegion*r,u64 at){
 const u64 limit=1ull<<39;return !(r->base%4096)&&r->bytes&&!(r->bytes%4096)&&r->base<limit&&r->bytes<=limit-r->base&&r->base<=at&&at-r->base<=r->bytes&&4096<=r->bytes-(at-r->base);
}
static u32 begin(IzContext*c,IzHostRpcOwner*g,IzHostRpcMapper*m,u32 epoch){
 u64 old=__atomic_load_n(&g->word,__ATOMIC_ACQUIRE);m->offer=epoch;
 if(!c->host_mode||c->host_display_ready!=epoch||c->host_display_release==epoch||g->magic!=IZ_HOST_RPC_MAGIC||(u32)(old>>32)>=epoch||(old&0xffffffffu)!=IZ_HOST_RPC_REVOKED||g->opaque||g->graphics_owner||g->graphics_fault)return 0xe970;
 if(g->map_state==IZ_RPC_MAP_RETAINED||!c->pid||!c->ro_pid||!c->ro_handle||!c->queue||!c->functions[F_SLEEP]||!c->functions[F_QUEUE_IDLE]||!hr_span(c,c->host_rpc_staging,sizeof m->plan))return 0xe971;
 m->plan=*(const IzHostRpcPlan*)(uintptr_t)c->host_rpc_staging;IzHostRpcPlan*p=&m->plan;
 if(p->version!=1||p->bytes!=sizeof *p||p->reserved||p->reserved2||p->frame_mode!=1||p->full_write_thunk||p->full_receipt_thunk||p->full_backing||p->full_backing_bytes)return 0xe961;
 u32 rc=hr_targets(c,p);if(rc)return rc;
 u64 base=c->host_rpc_code_base,bytes=c->host_rpc_code_bytes,history=c->supervisor_base+IZ_HOST_RPC_HISTORY_OFFSET;
 if(base%4096||!bytes||bytes%4096||bytes>1048576||!hr_span(c,base,bytes)||!hr_span(c,history,4096)||!c->stack_bytes||c->stack<c->supervisor_base||c->stack_bytes>base-c->supervisor_base||c->stack>base-c->stack_bytes||g->code_used>=bytes/4096||(g->code_used&&(g->code_base!=base||g->code_bytes!=bytes||g->code_history!=history)))return 0xe96c;
 u64 lower=4096,previous_epoch=0;
 for(u32 i=0;i<g->code_used;i++){const u64*h=(const void*)(uintptr_t)(history+(u64)i*16);u32 state=hr_code_state(g,i);if(!state||h[1]<=previous_epoch||h[1]>=epoch)return 0xe96d;previous_epoch=h[1];if(state==1){if(h[0])return 0xe96d;continue;}if(h[0]<4096||h[0]>=(1ull<<39)-4096||h[0]%4096)return 0xe96d;for(u32 j=0;j<i;j++){const u64*p=(const void*)(uintptr_t)(history+(u64)j*16);if(h[0]==p[0])return 0xe96d;}if(h[0]>=lower)lower=h[0]+4096;}
 u64 first=symbol(0),second=symbol(1);if(first<4096||second<4096||first>=(1ull<<39)||second>=(1ull<<39)||first%4||second%4||first==second)return 0xe972;
 u64 source=base+(u64)g->code_used*4096;IzAddressRegion r;rc=iz_self_query(&r,source);if(rc)return rc;
 if(!region(&r,source)||r.permission!=3||r.attributes||r.ipc_refs||r.device_refs||!r.type||r.type==1||r.type==16)return 0xe973;
 g->code_base=base;g->code_bytes=bytes;g->code_history=history;g->map_source=source;g->map_rx=0;g->map_index=g->code_used++;g->map_epoch=epoch;g->map_state=IZ_RPC_MAP_PREPARING;g->map_error=0;hr_code_mark(g,g->map_index,1);
 u64*h=(void*)(uintptr_t)(history+(u64)g->map_index*16);h[0]=0;h[1]=epoch;memset((void*)(uintptr_t)source,0,4096);emit((void*)(uintptr_t)source,(u64)(uintptr_t)g,epoch);
 if(lower>=(1ull<<39))return IZ_ADDRESS_FULL;IzAddressRange bounds={lower,(1ull<<39)-lower};rc=iz_address_begin_bounded(c,&m->address,4096,0,&bounds);if(rc)return rc;m->phase=1;return 0;
}
void iz_host_rpc_mapper_cancel(IzContext*c,IzHostRpcOwner*g,IzHostRpcMapper*m,u32 epoch){
 if(m->phase){if(m->offer==epoch)finish(c,g,m,0xe977,IZ_RPC_MAP_CANCELLED);return;}
 if(g->claim_epoch==epoch&&c->host_display_ready==epoch&&c->host_display_release!=epoch&&hr_epoch(g)<epoch){m->offer=epoch;g->map_epoch=epoch;g->map_source=g->map_rx=0;g->map_index=0;finish(c,g,m,0xe977,IZ_RPC_MAP_CANCELLED);}
}
void iz_host_rpc_mapper_step(IzContext*c,IzHostRpcOwner*g,IzHostRpcMapper*m,u32 lease){
 u32 offer=__atomic_load_n(&c->host_rpc_map_offer,__ATOMIC_ACQUIRE);
 if(!m->phase){if(!offer||offer!=lease||offer==c->host_rpc_map_ack)return;if(g->map_epoch==offer&&g->map_state!=IZ_RPC_MAP_IDLE){publish(c,g,offer);return;}u32 rc=begin(c,g,m,offer);if(rc){if(g->map_epoch!=offer){g->map_epoch=offer;g->map_source=g->map_rx=0;g->map_index=0;}finish(c,g,m,rc,g->map_state==IZ_RPC_MAP_RETAINED?IZ_RPC_MAP_RETAINED:IZ_RPC_MAP_FAILED);return;}}
 if(m->offer!=lease||offer!=lease||c->host_display_ready!=lease||c->host_display_release==lease||c->stop){iz_host_rpc_mapper_cancel(c,g,m,m->offer);return;}
 u32 done=0,rc=iz_address_step(c,&m->address,&done);if(rc){finish(c,g,m,rc,IZ_RPC_MAP_FAILED);return;}if(!done)return;
 g->map_rx=m->address.plan.code_base;u64*h=(void*)(uintptr_t)(g->code_history+(u64)g->map_index*16);h[0]=g->map_rx;hr_code_mark(g,g->map_index,2);
 u32 unknown=0;rc=map_page(c,g,&unknown);if(rc){finish(c,g,m,rc,unknown?IZ_RPC_MAP_RETAINED:IZ_RPC_MAP_FAILED);return;}
 IzAddressRegion r;rc=iz_self_query(&r,g->map_rx);if(rc||!region(&r,g->map_rx)||r.permission!=5||r.attributes||r.ipc_refs||r.device_refs||!r.type){finish(c,g,m,rc?rc:0xe975,IZ_RPC_MAP_RETAINED);return;}
 u8 expected[160];emit(expected,(u64)(uintptr_t)g,lease);if(memcmp((const void*)(uintptr_t)g->map_rx,expected,sizeof expected)){finish(c,g,m,0xe975,IZ_RPC_MAP_RETAINED);return;}
 hr_code_mark(g,g->map_index,3);g->map_state=IZ_RPC_MAP_MAPPED;m->plan.full_write_thunk=g->map_rx;m->plan.full_receipt_thunk=g->map_rx+80;m->plan.full_backing=g->map_source;m->plan.full_backing_bytes=4096;
 if(c->host_display_release==lease||c->stop){finish(c,g,m,0xe977,IZ_RPC_MAP_CANCELLED);return;}
 rc=hr_open(c,g,lease,&m->plan);if(rc){finish(c,g,m,rc,IZ_RPC_MAP_FAILED);return;}
 c->host_rpc_offer=lease;c->host_rpc_error=0;c->host_rpc_count=g->count;c->host_rpc_bindings=(u64)(uintptr_t)g->bindings;hr_status(c,g);__atomic_store_n(&c->host_rpc_ack,lease,__ATOMIC_RELEASE);finish(c,g,m,0,IZ_RPC_MAP_ACTIVE);
}
