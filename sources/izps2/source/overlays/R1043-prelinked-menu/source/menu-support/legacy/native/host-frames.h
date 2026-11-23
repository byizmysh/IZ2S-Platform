#ifndef IZ_HOST_FRAMES_H
#define IZ_HOST_FRAMES_H
/* Launcher-owned frame execution. These are prepared Vulkan command buffers,
 * not application code or an app manifest capability. Mutable app controls
 * must have their own allocation, disjoint from the resident menu/worker. */
typedef IzHostFramesPlan IzHostFrames;
_Static_assert(sizeof(IzHostFrames)==168,"Host frame plan");
typedef struct {
 IzHostFrames live,pending;u32 ticket,pending_ticket,observed_offer;
} IzHostFrameOwner;
enum {IZ_HOST_RETURN_USER=1,IZ_HOST_RETURN_APP,IZ_HOST_RETURN_PC,IZ_HOST_RETURN_STOP};
static int hf_range(u64 address,u64 bytes){return address>=4096&&address<(1ull<<39)&&bytes&&bytes<=(1ull<<39)-address;}
static int hf_overlap(u64 a,u64 n,u64 b,u64 m){return n&&m&&a<b+m&&b<a+n;}
static u32 hf_validate(IzContext*c,const IzHostFrames*p){
 if(p->version!=1||p->bytes!=sizeof *p||p->controls_bytes!=IZ_SHARED_BYTES||p->controls%4096||!hf_range(p->controls,p->controls_bytes)||p->reserved[0]||p->reserved[1]||!c->host_mode||!c->supervisor_base||!hf_range(c->supervisor_base,c->supervisor_bytes)||!c->image_count||c->image_count>IZ_MAX_IMAGES)return 0xe951;
 if(hf_overlap(p->controls,p->controls_bytes,c->controls,c->controls_bytes)||hf_overlap(p->controls,p->controls_bytes,c->supervisor_base,c->supervisor_bytes))return 0xe952;
 unsigned n=0;while(n<64&&p->id[n]){char ch=p->id[n];if(!((ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||(n&&ch=='-')))return 0xe953;n++;}if(!n||n>48)return 0xe953;for(unsigned i=n+1;i<64;i++)if(p->id[i])return 0xe953;
 for(unsigned i=0;i<IZ_MAX_IMAGES;i++){
  if(i<c->image_count){if(!p->commands[i])return 0xe954;for(unsigned j=0;j<c->image_count;j++)if(p->commands[i]==c->menu_commands[j])return 0xe954;for(unsigned j=0;j<i;j++)if(p->commands[j]==p->commands[i])return 0xe954;}
  else if(p->commands[i])return 0xe954;
 }return 0;
}
static void hf_return(IzContext*c,IzHostFrameOwner*s,u32 reason){
 u32 ticket=s->ticket;if(!ticket)return;
 s->ticket=0;c->host_frames_active=c->host_frames_paused=0;c->host_frames_reason=reason;
 /* Every submitted app frame already completed QueueWaitIdle. Retain all
  * caller-owned GPU/storage objects; returning does not authorize freeing. */
 __atomic_store_n(&c->host_frames_returned,ticket,__ATOMIC_RELEASE);
}
static void hf_pause(IzContext*c,IzHostFrameOwner*s){if(s->ticket){c->host_frames_active=0;__atomic_store_n(&c->host_frames_paused,s->ticket,__ATOMIC_RELEASE);}}
static void hf_offer(IzContext*c,IzHostFrameOwner*s,u32 lease){
 u32 cancel=__atomic_load_n(&c->host_frames_cancel,__ATOMIC_ACQUIRE);
 if(cancel&&cancel==s->ticket&&c->host_frames_paused==s->ticket)hf_return(c,s,IZ_HOST_RETURN_PC);
 u32 offer=__atomic_load_n(&c->host_frames_offer,__ATOMIC_ACQUIRE);
 if(!offer||offer!=lease||offer==s->observed_offer)return;s->observed_offer=offer;
 IzHostFrames plan=c->host_frames_plan;u32 error=hf_validate(c,&plan);
 if(!error&&(s->ticket||s->pending_ticket))error=0xe955;
 /* A complete offer may be accepted only by the exact granted PC lease.
  * It is copied to private stack state before acknowledging publication. */
 if(!error&&(offer!=__atomic_load_n(&c->host_frames_offer,__ATOMIC_ACQUIRE)||c->host_display_ready!=lease||c->host_display_release==lease))error=0xe956;
 if(!error){s->pending=plan;s->pending_ticket=lease;}
 c->host_frames_error=error;__atomic_store_n(&c->host_frames_ack,offer,__ATOMIC_RELEASE);
}
static void hf_resume(IzContext*c,IzHostFrameOwner*s,u32 lease){
 if(s->pending_ticket==lease){s->live=s->pending;s->ticket=lease;s->pending_ticket=0;c->host_frames_reason=0;}
 if(s->ticket){c->host_frames_paused=0;__atomic_store_n(&c->host_frames_active,s->ticket,__ATOMIC_RELEASE);}
}
#endif
