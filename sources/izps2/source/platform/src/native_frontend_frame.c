#include "native_frontend_frame.h"
#include <string.h>
_Static_assert(sizeof(Ps2FrontendFrameSlot)==64,"Local frame slot ABI");
_Static_assert(sizeof(Ps2FrontendFrame)==PS2_FRONTEND_FRAME_HEADER_BYTES,"Local frame channel ABI");
static uint32_t load(const uint32_t*p){return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
static void store(uint32_t*p,uint32_t v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
static int range(uintptr_t p,size_t n){return p&&n&&p<=UINTPTR_MAX-n;}
static int separate(uintptr_t p,size_t n,uintptr_t q,size_t m){return range(p,n)&&range(q,m)&&(p+n<=q||q+m<=p);}
static int valid(const Ps2FrontendFrame*q){
 if(!q||((uintptr_t)q&63)||q->magic!=PS2_FRONTEND_FRAME_MAGIC||q->version!=1||q->bytes!=sizeof(*q)||
    q->slot_count!=3||!q->maximum_width||q->maximum_width>1024||!q->maximum_height||q->maximum_height>1024||
    !range((uintptr_t)q,(size_t)q->backing_bytes)||q->backing_bytes<sizeof(*q)||q->reserved)return 0;
 const uint64_t capacity=(uint64_t)q->maximum_width*q->maximum_height*4;
 for(unsigned i=0;i<3;i++){
  const Ps2FrontendFrameSlot*s=q->slot+i;
  if(s->pixels_offset<sizeof(*q)||(s->pixels_offset&63)||s->capacity!=capacity||s->pixels_offset>q->backing_bytes||capacity>q->backing_bytes-s->pixels_offset||
     s->reserved[0]||s->reserved[1]||s->reserved[2])return 0;
  for(unsigned j=0;j<i;j++)if(s->pixels_offset<q->slot[j].pixels_offset+capacity&&q->slot[j].pixels_offset<s->pixels_offset+capacity)return 0;
 }
 return 1;
}
uint32_t ps2_frontend_frame_initialize(Ps2FrontendFrame*q,size_t bytes,unsigned width,unsigned height){
 if(!q||((uintptr_t)q&63)||!range((uintptr_t)q,bytes)||!width||width>1024||!height||height>1024)return PS2_FRONTEND_INVALID;
 const uint64_t capacity=(uint64_t)width*height*4,stride=(capacity+63)&~UINT64_C(63),needed=sizeof(*q)+3*stride;
 if(bytes<needed)return PS2_FRONTEND_INVALID;
 memset(q,0,sizeof(*q));q->magic=PS2_FRONTEND_FRAME_MAGIC;q->version=1;q->bytes=sizeof(*q);
 q->maximum_width=width;q->maximum_height=height;q->slot_count=3;q->backing_bytes=bytes;
 for(unsigned i=0;i<3;i++){q->slot[i].pixels_offset=sizeof(*q)+i*stride;q->slot[i].capacity=capacity;}
 store(&q->state,PS2_FRONTEND_LIVE);return PS2_FRONTEND_OK;
}
uint32_t ps2_frontend_frame_begin(Ps2FrontendFrame*q,Ps2FrontendFrameWrite*w,void**pixels,size_t*capacity){
 if(w)*w=(Ps2FrontendFrameWrite){0};if(pixels)*pixels=0;if(capacity)*capacity=0;
 if(!w||!pixels||!capacity||!valid(q))return PS2_FRONTEND_INVALID;
 if(load(&q->state)!=PS2_FRONTEND_LIVE)return PS2_FRONTEND_NO_FRAME;
 uint32_t inactive=0;
 if(!__atomic_compare_exchange_n(&q->producer_active,&inactive,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return PS2_FRONTEND_BUSY;
 if(load(&q->state)!=PS2_FRONTEND_LIVE){store(&q->producer_active,0);return PS2_FRONTEND_NO_FRAME;}
 unsigned selected=3;
 for(unsigned i=0;i<3;i++){uint32_t free=PS2_FRONTEND_FREE;if(__atomic_compare_exchange_n(&q->slot[i].state,&free,PS2_FRONTEND_WRITING,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){selected=i;break;}}
 if(selected==3){
  // Replacing an unconsumed READY frame is permitted; never touch a READING
  // slot. A slow frontend drops pictures without blocking emulated execution.
  for(unsigned i=0;i<3;i++){uint32_t ready=PS2_FRONTEND_READY;if(__atomic_compare_exchange_n(&q->slot[i].state,&ready,PS2_FRONTEND_WRITING,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){selected=i;break;}}
 }
 if(selected==3){store(&q->producer_active,0);return PS2_FRONTEND_NO_FRAME;}
 const uint64_t sequence=q->next_sequence;
 if(sequence==UINT64_MAX){store(&q->slot[selected].state,PS2_FRONTEND_FREE);store(&q->state,PS2_FRONTEND_POISONED);store(&q->producer_active,0);return PS2_FRONTEND_OVERFLOW;}
 q->next_sequence=sequence+1;*w=(Ps2FrontendFrameWrite){selected,0,sequence+1};
 *pixels=(unsigned char*)q+q->slot[selected].pixels_offset;*capacity=(size_t)q->slot[selected].capacity;return PS2_FRONTEND_OK;
}
static int writer(Ps2FrontendFrame*q,const Ps2FrontendFrameWrite*w){return valid(q)&&w&&!w->reserved&&w->slot<3&&w->sequence&&load(&q->producer_active)==1&&load(&q->slot[w->slot].state)==PS2_FRONTEND_WRITING&&q->next_sequence==w->sequence;}
uint32_t ps2_frontend_frame_commit(Ps2FrontendFrame*q,const Ps2FrontendFrameWrite*w,unsigned width,unsigned height,uint32_t frame){
 if(!writer(q,w)||!width||width>q->maximum_width||!height||height>q->maximum_height)return PS2_FRONTEND_INVALID;
 Ps2FrontendFrameSlot*s=q->slot+w->slot;s->width=width;s->height=height;s->guest_frame=frame;__atomic_store_n(&s->sequence,w->sequence,__ATOMIC_RELEASE);
 store(&s->state,PS2_FRONTEND_READY);store(&q->producer_active,0);return PS2_FRONTEND_OK;
}
uint32_t ps2_frontend_frame_cancel(Ps2FrontendFrame*q,const Ps2FrontendFrameWrite*w){
 if(!writer(q,w))return PS2_FRONTEND_INVALID;
 store(&q->slot[w->slot].state,PS2_FRONTEND_FREE);store(&q->producer_active,0);return PS2_FRONTEND_OK;
}
uint32_t ps2_frontend_frame_receive(Ps2FrontendFrame*q,void*output,size_t output_bytes){
 if(!valid(q)||!output||((uintptr_t)output&3)||output_bytes<4096)return PS2_FRONTEND_INVALID;
 const size_t output_span=4096+(size_t)q->maximum_width*q->maximum_height*4;
 if(output_bytes<output_span||!separate((uintptr_t)q,(size_t)q->backing_bytes,(uintptr_t)output,output_span))return PS2_FRONTEND_INVALID;
 if(load(&q->state)==PS2_FRONTEND_POISONED)return PS2_FRONTEND_INVALID;
 unsigned selected=3;uint64_t newest=__atomic_load_n(&q->consumed_sequence,__ATOMIC_ACQUIRE);
 for(unsigned i=0;i<3;i++)if(load(&q->slot[i].state)==PS2_FRONTEND_READY){
  const uint64_t sequence=__atomic_load_n(&q->slot[i].sequence,__ATOMIC_ACQUIRE);
  if(sequence>newest){selected=i;newest=sequence;}
 }
 if(selected==3)return PS2_FRONTEND_NO_FRAME;
 uint32_t ready=PS2_FRONTEND_READY;
 if(!__atomic_compare_exchange_n(&q->slot[selected].state,&ready,PS2_FRONTEND_READING,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return PS2_FRONTEND_NO_FRAME;
 Ps2FrontendFrameSlot*s=q->slot+selected;
 const unsigned width=s->width,height=s->height;const uint64_t sequence=s->sequence,bytes=(uint64_t)width*height*4;
 if(!width||width>q->maximum_width||!height||height>q->maximum_height||bytes>s->capacity||bytes>output_bytes-4096){store(&s->state,PS2_FRONTEND_FREE);return PS2_FRONTEND_INVALID;}
 // All pixel reads occur after the READY->READING acquisition. The producer
 // cannot reclaim this slot until its final FREE publication below.
 memcpy((unsigned char*)output+4096,(unsigned char*)q+s->pixels_offset,(size_t)bytes);
 uint32_t*h=output;h[0]=width;h[1]=height;h[2]++;h[3]=h[4]=h[5]=h[7]=0;h[6]=100;
 __atomic_thread_fence(__ATOMIC_RELEASE);__atomic_store_n(&q->consumed_sequence,sequence,__ATOMIC_RELEASE);
 store(&s->state,PS2_FRONTEND_FREE);return PS2_FRONTEND_OK;
}
uint32_t ps2_frontend_frame_stop(Ps2FrontendFrame*q){
 if(!valid(q))return PS2_FRONTEND_INVALID;
 uint32_t live=PS2_FRONTEND_LIVE;
 if(__atomic_compare_exchange_n(&q->state,&live,PS2_FRONTEND_CLOSING,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)||live==PS2_FRONTEND_CLOSING||live==PS2_FRONTEND_CLOSED)return PS2_FRONTEND_OK;
 return PS2_FRONTEND_INVALID;
}
uint32_t ps2_frontend_frame_finish(Ps2FrontendFrame*q){
 if(!valid(q)||load(&q->state)!=PS2_FRONTEND_CLOSING)return PS2_FRONTEND_INVALID;
 if(load(&q->producer_active))return PS2_FRONTEND_BUSY;
 for(unsigned i=0;i<3;i++){const uint32_t s=load(&q->slot[i].state);if(s==PS2_FRONTEND_WRITING||s==PS2_FRONTEND_READING)return PS2_FRONTEND_BUSY;if(s>PS2_FRONTEND_READING)return PS2_FRONTEND_INVALID;}
 for(unsigned i=0;i<3;i++)store(&q->slot[i].state,PS2_FRONTEND_FREE);
 store(&q->state,PS2_FRONTEND_CLOSED);return PS2_FRONTEND_OK;
}
