#ifndef IZPS2_NATIVE_STALL_POSTJOIN_H
#define IZPS2_NATIVE_STALL_POSTJOIN_H

#include "stall-symbols.h"

typedef struct {
 uint32_t captured, attempted;
 uint64_t values[sizeof(stall_fields)/sizeof(stall_fields[0])];
} StallSnapshot;
_Static_assert(sizeof(StallSnapshot)<=sizeof(((R622AppData*)0)->hud_scratch),"Post-join snapshot fits existing app data");

static void stall_capture(R579AppData *data){
 R579Owner *s=&data->owner;
 const R583OsJit *j=&s->jit;
 unsigned char *storage=((R622AppData*)(void*)data)->hud_scratch;
 StallSnapshot snapshot={0};
 cp(&snapshot,storage,sizeof snapshot);
 if(s->phase!=POST_JOIN||snapshot.attempted)return;
 snapshot.attempted=1;
 const IzNativeImage *i=&s->image;
 const uint64_t base=P(i->data);
 if(j->state==R583_JIT_QUIESCENT&&!j->pending&&j->worker_join_calls==1&&
    j->worker_join_returned==1&&!j->worker_result&&!j->image_release_calls&&
    !s->worker_ticket&&s->image_verified&&s->context_ready&&s->joined.worker_ticket&&
    j->worker_ticket==s->joined.worker_ticket&&
    !s->joined.worker_result&&s->joined.image_token==s->image_ticket&&i->token==s->image_ticket&&
    j->image_ticket==s->image_ticket&&eq(i,&j->image,sizeof *i)&&
    j->context_data==base&&j->context_data_bytes==R579_DATA_MEMORY_BYTES&&
    i->data_bytes==R382_DATA_BYTES&&!(base&4095)&&span(base,i->data_bytes)){
  for(unsigned n=0;n<sizeof stall_fields/sizeof stall_fields[0];n++){
   const StallField *f=stall_fields+n;
   const unsigned char *p=(const unsigned char*)i->data+f->offset;
   for(unsigned byte=0;byte<f->bytes;byte++)
    snapshot.values[n]|=(uint64_t)p[byte]<<(8*byte);
  }
  snapshot.captured=1;
 }
 cp(storage,&snapshot,sizeof snapshot);
}

static int stall_append(const R579AppData *data,char *csv,size_t capacity,uint32_t *bytes){
 static const char footer[]="# END IZPS2_DIRECT_PRESENT_CADENCE_V1\n";
 const size_t tail=sizeof footer-1;
 if(*bytes>capacity||*bytes<tail||!eq(csv+*bytes-tail,footer,tail))return 0;
 StallSnapshot snapshot;
 cp(&snapshot,((const R622AppData*)(const void*)data)->hud_scratch,sizeof snapshot);

 // Count first. If the report cannot fit, the caller keeps the original CSV.
 size_t remaining=capacity-*bytes;
 R621Text out={csv,*bytes-tail,capacity,0};
 static const char present[]="# STALL_V1,captured,1\n";
 static const char unavailable[]="# STALL_V1,captured,0\n";
 size_t required=sizeof present-1;
 if(snapshot.captured){
  for(unsigned n=0;n<sizeof stall_fields/sizeof stall_fields[0];n++){
   const char *name=stall_names+stall_fields[n].name;
   uint64_t value=snapshot.values[n];
   size_t digits=1;
   while(value>=10){value/=10;digits++;}
   required+=sizeof "# STALL_V1,,"-1+1+digits;
   while(*name++)required++;
  }
 }
 if(required>remaining)return 0;
 r621_text(&out,snapshot.captured?present:unavailable);
 if(snapshot.captured){
  for(unsigned n=0;n<sizeof stall_fields/sizeof stall_fields[0];n++){
   r621_text(&out,"# STALL_V1,");
   r621_text(&out,stall_names+stall_fields[n].name);
   r621_char(&out,',');
   r621_number(&out,snapshot.values[n]);
   r621_char(&out,'\n');
  }
 }
 r621_text(&out,footer);
 *bytes=(uint32_t)out.bytes;
 return 1;
}
#endif
