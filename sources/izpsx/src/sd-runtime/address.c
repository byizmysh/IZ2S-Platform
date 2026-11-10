#include "address.h"
#define LIMIT (1ull<<39)
static int span(u64 base,u64 bytes){return !(base&4095)&&bytes&&!(bytes&4095)&&base<LIMIT&&bytes<=LIMIT-base;}
static int overlap(u64 a,u64 n,u64 b,u64 m){return m&&a<b+m&&b<a+n;}
static u32 stopped(Ps1SdServices*c){if(!c||!c->pid)return IZ_ADDRESS_INVALID;if(c->poisoned)return 0xe615;return __atomic_load_n(&c->stop,__ATOMIC_ACQUIRE)?0xe613:0;}
static u32 identity(Ps1SdServices*c){u64 pid=0;u32 rc=iz_self_process_id(&pid);return rc?rc:pid!=c->pid?IZ_ADDRESS_PID:0;}
static u32 capture(Ps1SdServices*c,IzAddressSpace*s){
 u32 rc=stopped(c);if(rc)return rc;rc=identity(c);if(rc)return rc;memset(s,0,sizeof *s);s->pid=c->pid;
 /* GetInfo: ASLR 12/13; reserved alias 2/3, heap 4/5 and stack 14/15. */
 rc=iz_self_info(&s->aslr.base,12);if(rc)return rc;rc=iz_self_info(&s->aslr.bytes,13);if(rc)return rc;
 for(unsigned i=0;i<3;i++){u32 field=i==0?2:i==1?4:14;rc=iz_self_info(&s->reserved[i].base,field);if(rc)return rc;rc=iz_self_info(&s->reserved[i].bytes,field+1);if(rc)return rc;}
 if(!span(s->aslr.base,s->aslr.bytes))return IZ_ADDRESS_INVALID;
 for(unsigned i=0;i<3;i++){const IzAddressRange*r=s->reserved+i;if(r->bytes?!span(r->base,r->bytes):(r->base>=LIMIT||(r->base&4095)))return IZ_ADDRESS_INVALID;}
 rc=stopped(c);return rc?rc:identity(c);
}
static u32 query(u64 at,IzAddressRegion*r){
 memset(r,0,sizeof *r);u32 rc=iz_self_query(r,at);if(rc)return rc;
 if(!span(r->base,r->bytes)||r->base>at||r->base+r->bytes<=at)return IZ_ADDRESS_INVALID;
 if(!r->type&&(r->attributes||r->permission||r->ipc_refs||r->device_refs))return IZ_ADDRESS_INVALID;return 0;
}
static int allowed(const IzAddressSpace*s,u64 base,u64 bytes){
 if(!base||!span(base,bytes)||base<s->aslr.base||base-s->aslr.base>s->aslr.bytes||bytes>s->aslr.bytes-(base-s->aslr.base))return 0;
 for(unsigned i=0;i<3;i++)if(overlap(base,bytes,s->reserved[i].base,s->reserved[i].bytes))return 0;return 1;
}
u32 iz_address_begin(Ps1SdServices*c,IzAddressCursor*w,u64 bytes,const IzAddressPlan*p){
 if(!w)return IZ_ADDRESS_INVALID;memset(w,0,sizeof *w);w->bytes=bytes;
 u32 rc=0;if(!bytes||(bytes&4095)||bytes>128u*1024*1024)rc=IZ_ADDRESS_INVALID;
 if(!rc&&p&&(!span(p->window_base,p->window_bytes)||!p->code_base||(p->code_base&4095)||p->code_base<p->window_base||bytes>p->window_bytes||p->code_base-p->window_base>p->window_bytes-bytes))rc=IZ_ADDRESS_INVALID;
 if(!rc)rc=capture(c,&w->space);if(!rc&&p&&!allowed(&w->space,p->window_base,p->window_bytes))rc=IZ_ADDRESS_INVALID;
 if(rc){w->error=rc;return rc;}w->mode=p?2:1;
 if(p){w->plan=*p;w->at=p->code_base;w->end=p->code_base+bytes;}
 else{w->at=w->space.aslr.base?w->space.aslr.base:4096;w->end=w->space.aslr.base+w->space.aslr.bytes;}return 0;
}
u32 iz_address_step(Ps1SdServices*c,IzAddressCursor*w,u32*done){
 if(!w||!done)return IZ_ADDRESS_INVALID;*done=0;if(w->error)return w->error;if(w->done){*done=1;return 0;}
 u32 rc=stopped(c);if(rc)goto fail;if(!w->mode){rc=IZ_ADDRESS_INVALID;goto fail;}
 if(w->at>=w->end){if(w->mode==1){rc=IZ_ADDRESS_FULL;goto fail;}w->done=*done=1;return 0;}
 if(w->regions++==1024){rc=IZ_ADDRESS_LIMIT;goto fail;}
 IzAddressRegion r;rc=query(w->at,&r);if(rc)goto fail;u64 next=r.base+r.bytes;if(next>w->end)next=w->end;
 if(w->mode==2){if(r.type){rc=IZ_ADDRESS_FULL;goto fail;}w->at=next;rc=stopped(c);if(rc)goto fail;if(w->at==w->end)w->done=*done=1;return 0;}
 if(!r.type){u64 candidate=w->at;
  for(unsigned pass=0;pass<4&&candidate<next&&w->bytes<=next-candidate;pass++){
   u64 skip=candidate;for(unsigned i=0;i<3;i++){const IzAddressRange*b=w->space.reserved+i;if(overlap(candidate,w->bytes,b->base,b->bytes)&&skip<b->base+b->bytes)skip=b->base+b->bytes;}
   if(skip!=candidate){candidate=skip;continue;}
   u64 extent=next;for(unsigned i=0;i<3;i++){const IzAddressRange*b=w->space.reserved+i;if(b->bytes&&b->base>=candidate&&b->base<extent)extent=b->base;}
   if(extent<=candidate||w->bytes>extent-candidate||!allowed(&w->space,candidate,extent-candidate)){rc=IZ_ADDRESS_INVALID;goto fail;}
   rc=stopped(c);if(rc)goto fail;w->plan=(IzAddressPlan){candidate,candidate,extent-candidate};w->done=*done=1;return 0;
  }
 }w->at=next;if(w->at==w->end){rc=IZ_ADDRESS_FULL;goto fail;}return 0;
fail:w->error=rc;return rc;
}
u32 iz_address_find(Ps1SdServices*c,u64 bytes,IzAddressPlan*out){
 if(!out)return IZ_ADDRESS_INVALID;memset(out,0,sizeof *out);IzAddressCursor w;u32 rc=iz_address_begin(c,&w,bytes,0),done=0;while(!rc&&!done)rc=iz_address_step(c,&w,&done);if(!rc)*out=w.plan;return rc;
}
u32 iz_address_admit(Ps1SdServices*c,const IzAddressPlan*p,u64 bytes){
 if(!p)return IZ_ADDRESS_INVALID;IzAddressCursor w;u32 rc=iz_address_begin(c,&w,bytes,p),done=0;while(!rc&&!done)rc=iz_address_step(c,&w,&done);return rc;
}
