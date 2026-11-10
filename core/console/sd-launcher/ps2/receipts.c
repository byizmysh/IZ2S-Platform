/* Current R176 paired-GPU/native-IOP record validator, derived from the
 * earlier local decoder and the exact unchanged current JS validators.
 * It grants no SDK join, Main/global or resource-release authority. */
#include "receipts.h"
static void pg_zero(void*p,size_t n){volatile unsigned char*b=p;while(n--)*b++=0;}
static void pg_copy(void*p,const void*q,size_t n){unsigned char*a=p;const unsigned char*b=q;while(n--)*a++=*b++;}
static int pg_range(uint64_t a,uint64_t n){return a&&n&&a<(1ull<<39)&&n<=(1ull<<39)-a;}
static int pg_apart(const void*a,size_t n,const void*b,size_t m){uint64_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=UINT64_MAX-n&&y<=UINT64_MAX-m&&(x+n<=y||y+m<=x);}
static int pg_text(const Rj*j,uint16_t obj,const char*k,const char*t){uint16_t n=rj_get(j,obj,k);if(!rj_is(j,n,RJ_STRING))return 0;const RjNode*p=j->nodes+n;uint32_t i=0;while(t[i]&&p->start+i<p->end&&(unsigned char)t[i]==j->text[p->start+i])i++;return !t[i]&&p->start+i==p->end;}
static int pg_bool(const Rj*j,const char*k,int yes){return rj_is(j,rj_get(j,0,k),yes?RJ_TRUE:RJ_FALSE);}
static int pg_num(const Rj*j,const char*k,uint64_t*v){uint16_t n=rj_get(j,0,k);return rj_is(j,n,RJ_NUMBER)&&rj_u64(j,n,v)&&*v<=UINT64_C(9007199254740991);}
static int pg_u32(const Rj*j,const char*k,uint32_t*v){uint64_t n;if(!pg_num(j,k,&n)||n>UINT32_MAX)return 0;*v=(uint32_t)n;return 1;}
static int pg_s32(const Rj*j,const char*k,int32_t*v){uint16_t n=rj_get(j,0,k);if(!rj_is(j,n,RJ_NUMBER))return 0;uint32_t a=j->nodes[n].start,e=j->nodes[n].end;int neg=a<e&&j->text[a]=='-';if(neg)a++;if(a==e)return 0;uint64_t z=0;for(;a<e;a++){unsigned c=j->text[a];if(c<'0'||c>'9'||z>(2147483648ull-(c-'0'))/10)return 0;z=z*10+c-'0';}if(z>2147483647ull+(unsigned)neg)return 0;*v=neg?(int32_t)(-(int64_t)z):(int32_t)z;return 1;}
static int pg_decimal(const Rj*j,const char*k,uint64_t*v){uint16_t n=rj_get(j,0,k);if(!rj_is(j,n,RJ_STRING))return 0;const RjNode*p=j->nodes+n;if(p->start==p->end||(p->end-p->start>1&&j->text[p->start]=='0'))return 0;return rj_u64(j,n,v);}
static int pg_exact(const Rj*j,const char*pool,const uint16_t*offsets,unsigned count){unsigned found=0;for(uint16_t k=j->nodes[0].child;k!=RJ_NONE;){uint16_t v=j->nodes[k].next;if(v==RJ_NONE)return 0;found++;k=j->nodes[v].next;}if(found!=count)return 0;for(unsigned i=0;i<count;i++)if(rj_get(j,0,pool+offsets[i])==RJ_NONE)return 0;return 1;}
static int pg_utf8(const unsigned char*b,size_t n){
 for(size_t i=0;i<n;){unsigned c=b[i++];if(c<128)continue;unsigned tail,min;
  if(c>=0xc2&&c<=0xdf){tail=1;min=0x80;c&=31;}else if(c>=0xe0&&c<=0xef){tail=2;min=0x800;c&=15;}else if(c>=0xf0&&c<=0xf4){tail=3;min=0x10000;c&=7;}else return 0;
  if(tail>n-i)return 0;while(tail--){unsigned x=b[i++];if((x&0xc0)!=0x80)return 0;c=(c<<6)|(x&63);}
  if(c<min||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return 0;
 }return 1;
}
static int pg_row(Rj*j,const IzPs2ReceiptCapsule*p,unsigned i){const IzPs2ReceiptRow*r=p->rows+i;return r->id==i+1&&r->state==1&&!r->error&&r->bytes&&r->bytes<=4096&&pg_utf8(r->json,r->bytes)&&rj_parse(j,r->json,r->bytes);}
static int pg_onum(const Rj*j,uint16_t obj,const char*k,uint64_t*out){uint16_t n=rj_get(j,obj,k);return rj_is(j,n,RJ_NUMBER)&&rj_u64(j,n,out)&&*out<=UINT64_C(9007199254740991);}
static int pg_obool(const Rj*j,uint16_t obj,const char*k,int yes){return rj_is(j,rj_get(j,obj,k),yes?RJ_TRUE:RJ_FALSE);}
static int pg_odecimal(const Rj*j,uint16_t obj,const char*k,uint64_t*out){uint16_t n=rj_get(j,obj,k);if(!rj_is(j,n,RJ_STRING))return 0;const RjNode*q=j->nodes+n;return q->start<q->end&&(q->end-q->start==1||j->text[q->start]!='0')&&rj_u64(j,n,out);}
static int pg_iop(IzPs2Receipts*c,Rj*j){
 uint16_t i=rj_get(j,0,"nativeIOP");uint64_t n,finishes,joins;
 if(!pg_text(j,i,"format","ps2-native-iop-lifecycle/1")||!pg_obool(j,i,"compileTimeOptIn",1)||!pg_obool(j,i,"joinedClosed",1)||!pg_obool(j,i,"SDKReapClaimed",0))return 0;
 if(!pg_onum(j,i,"errors",&n)||n||!pg_onum(j,i,"peakAppThreadBudget",&n)||n!=5||!pg_onum(j,i,"userThreadLimit",&n)||n!=6||!pg_onum(j,i,"publications",&n))return 0;
 if(!pg_onum(j,i,"starts",&c->iop_starts)||!pg_onum(j,i,"finishes",&finishes)||!pg_onum(j,i,"actualPthreadJoins",&joins)||c->iop_starts!=finishes||finishes!=joins||c->iop_starts>UINT64_C(9007199254740987)||c->created!=4+c->iop_starts)return 0;
 if(!pg_odecimal(j,i,"owner",&c->iop_owner)||!pg_onum(j,i,"state",&n))return 0;
 if(!c->iop_starts)return !c->iop_owner&&!n;
 if(!c->iop_owner||n!=4||!pg_odecimal(j,i,"availableCoreMask",&c->iop_mask)||!(c->iop_mask&1))return 0;
 uint64_t mask=c->iop_mask&~UINT64_C(1);unsigned bits=0;while(mask){mask&=mask-1;bits++;}if(bits<5)return 0;
 if(!pg_onum(j,i,"rxBytes",&n)||n!=4194304||!pg_odecimal(j,i,"rxBegin",&n)||!n)return 0;
 uint16_t owner=rj_get(j,i,"runtimeOwner");uint64_t musl,sdk,core;
 if(!pg_odecimal(j,owner,"muslTP",&musl)||!musl||!pg_odecimal(j,owner,"sdkTP",&sdk)||musl==sdk||!pg_odecimal(j,owner,"sdkThreadObject",&n)||!n||!pg_odecimal(j,owner,"sdkBootStack",&n)||!n||!pg_odecimal(j,owner,"muslStackTop",&n)||!n)return 0;
 if(!pg_onum(j,owner,"runtimeTid",&n)||!n||!pg_onum(j,owner,"assignedCore",&core)||!core||core>=64||!(c->iop_mask&(UINT64_C(1)<<core)))return 0;return 1;
}
#define PG_N(k,m) do{if(!pg_num(j,k,&c->m))return 0;}while(0)
#define PG_D(k,m) do{if(!pg_decimal(j,k,&c->m))return 0;}while(0)
static int pg_common(IzPs2Receipts*c,Rj*j,const IzPs2ReceiptCapsule*p){
 if(!pg_row(j,p,0))return 0;PG_N("threads",tls_threads);PG_N("allocations",tls_allocations);PG_N("releases",tls_releases);c->after_static_dtors=pg_bool(j,"afterStaticDestructors",1);
 if(c->tls_threads||c->tls_allocations!=c->tls_releases||!c->after_static_dtors)return 0;
 if(!pg_row(j,p,1))return 0;c->vm_passed=pg_bool(j,"passed",1)&&pg_bool(j,"sdkDataFingerprintsVerified",1);uint64_t n;
 if(!c->vm_passed||!pg_num(j,"dataBytes",&n)||n!=66060288||!pg_num(j,"codeBytes",&n)||n!=IZ_PS2_JIT_BYTES)return 0;
 PG_N("createdThreads",created);PG_N("joinedThreads",joined);PG_N("codeMaps",code_maps);PG_N("codeUnmaps",code_unmaps);PG_N("dataAllocations",data_allocations);PG_N("dataReleases",data_releases);
 if(c->created!=c->joined||c->code_maps!=c->code_unmaps||c->data_allocations!=1||c->data_releases!=1)return 0;
 if(!pg_row(j,p,2))return 0;c->domains_production=pg_bool(j,"productionDomains",1);c->gs_joined=pg_bool(j,"gsJoined",1);
 if(!c->domains_production||!c->gs_joined||!pg_u32(j,"lastDomainError",&c->last_domain_error)||c->last_domain_error)return 0;
 if(!pg_row(j,p,3))return 0;c->av_passed=pg_bool(j,"passed",1)&&pg_bool(j,"cleanupCompleted",1)&&pg_bool(j,"gsJoined",1)&&pg_bool(j,"privateMuslWorkerTlsChecked",1);c->queue_idle=pg_bool(j,"queueIdle",1);c->av_retained=!pg_bool(j,"retained",0);
 if(!pg_num(j,"version",&n)||n!=2||!pg_num(j,"error",&n)||n||!pg_text(j,0,"errorSource","none")||!pg_num(j,"presented",&c->presented)||!pg_num(j,"audioAppends",&c->audio_appends))return 0;
 PG_N("audioOpens",audio_opens);PG_N("audioCloses",audio_closes);PG_N("workers",audio_workers);PG_N("joins",audio_joins);
 if(!c->av_passed||!c->queue_idle||c->av_retained||c->audio_opens!=1||c->audio_closes!=1||c->audio_workers!=1||c->audio_joins!=1)return 0;
 if(!pg_row(j,p,4))return 0;PG_N("opened",sd_opened);PG_N("closed",sd_closed);if(c->sd_opened!=c->sd_closed||!pg_num(j,"hostGameReads",&n)||n||!pg_num(j,"hostBiosReads",&n)||n||!pg_bool(j,"readOnlyGameMount",1)||!pg_bool(j,"borrowedCapabilitiesClosed",0))return 0;
 if(!pg_row(j,p,5)||!pg_num(j,"version",&n)||n!=2||!pg_text(j,0,"phase","vm")||!pg_bool(j,"startupSucceeded",1)||!pg_bool(j,"rollbackCompleted",0)||!pg_num(j,"bootResult",&n)||n)return 0;
 if(!pg_row(j,p,6)||!pg_text(j,0,"format","ps2-native-hwgs-thread-lifecycle/1")||!pg_bool(j,"passed",1)||!pg_bool(j,"afterActualGsJoin",1)||!pg_bool(j,"gsNeverStarted",0)||!pg_bool(j,"noSoftwareWorkerNamesOrJoins",1)||!pg_num(j,"expectedSoftwareWorkers",&n)||n||!pg_num(j,"error",&n)||n)return 0;
 PG_D("gsHandle",gs_handle);if(!c->gs_handle||!pg_num(j,"expectedNormalPrivateFacadeThreads",&n)||n!=5||!pg_iop(c,j))return 0;c->hwgs_closed=1;
 if(!pg_row(j,p,7)||!pg_text(j,0,"format","ps2-native-hwgs-av-lifecycle/1")||!pg_bool(j,"passed",1)||!pg_bool(j,"afterActualGsJoin",1)||!pg_bool(j,"gsNeverStarted",0)||!pg_bool(j,"ready",1)||!pg_bool(j,"noLiveInstanceDevice",1)||!pg_bool(j,"cpuStagingReleased",1))return 0;
 static const char resource_keys[]="instancePhase\0""instanceDriverCalled\0""instanceDestroyed\0""instanceResult\0""instanceOutput\0""devicePhase\0""deviceDriverCalled\0""deviceDestroyed\0""deviceResult\0""deviceOutput\0";static const uint16_t resource_keys_offsets[]={0,14,35,53,68,83,95,114,130,143};
 for(unsigned i=0;i<2;i++){const uint16_t*k=resource_keys_offsets+i*5;const char*phase=resource_keys+k[0],*called=resource_keys+k[1],*destroy=resource_keys+k[2],*result=resource_keys+k[3],*output=resource_keys+k[4];uint64_t*dest=i?&c->device:&c->instance;
  if(!pg_num(j,phase,&n)||n!=2||!pg_num(j,called,&n)||n!=1||!pg_num(j,destroy,&n)||n!=1||!pg_num(j,result,&n)||n||!pg_decimal(j,output,dest)||!*dest)return 0;
 }
 if(!pg_num(j,"closeKind",&n)||n!=1||!pg_num(j,"uncertainInstanceOutput",&n)||n||!pg_num(j,"uncertainDeviceOutput",&n)||n||!pg_num(j,"draining",&n)||n||!pg_decimal(j,"ownerPthread",&n)||n!=c->gs_handle)return 0;
 PG_N("submitEpoch",submit_epoch);PG_N("idleEpoch",idle_epoch);PG_N("sentinelsCreated",sentinels_created);PG_N("sentinelsDestroyed",sentinels_destroyed);
 if(c->submit_epoch!=c->idle_epoch||c->sentinels_created!=c->sentinels_destroyed)return 0;
 static const char submit_keys[]="queueResultCount\0""regularSubmits\0""sentinelSubmits\0";static const uint16_t submit_keys_offsets[]={0,17,32};
 for(unsigned i=0;i<3;i++)if(!pg_num(j,submit_keys+submit_keys_offsets[i],&n))return 0;
 c->private_device_closed=1;return 1;
}
#undef PG_N
#undef PG_D
static int pg_fence(IzPs2ReceiptFence*f,Rj*j,const IzPs2ReceiptCapsule*p){
 static const char keys[]="format\0""captured\0""afterActualGsJoin\0""nativeFinishSucceeded\0""finalQueueIdleKnown\0""videoPoisoned\0""statsAvailable\0""noTrackedFenceAtFinish\0""acceptedIsUniqueFPS\0""mode\0""configureAttempted\0""closed\0""createReturned\0""pending\0""retained\0""uncertain\0""pendingPresentMask\0""diagnosticWriteFailed\0""createResult\0""lastWait\0""lastReset\0""finalIdle\0""accepted\0""fenceCompleted\0""idleCompleted\0""enqueued\0""returns\0""waitCalls\0""waitTimeouts\0""resetCalls\0""finalIdleCalls\0""destroyCalls\0""destroyReturns\0""currentFence\0""createdFence\0""legacyVideoReturns\0";static const uint16_t keys_offsets[]={0,7,16,34,56,76,90,105,128,148,153,172,179,194,202,211,221,240,262,275,284,294,304,313,328,342,351,359,369,382,393,408,421,436,449,462};
 if(!pg_row(j,p,8)||!pg_exact(j,keys,keys_offsets,sizeof keys_offsets/sizeof*keys_offsets)||!pg_text(j,0,"format","ps2-native-presenter-fence-final/2")||!pg_bool(j,"captured",1)||!pg_bool(j,"afterActualGsJoin",1)||!pg_bool(j,"nativeFinishSucceeded",1)||!pg_bool(j,"finalQueueIdleKnown",1)||!pg_bool(j,"videoPoisoned",0)||!pg_bool(j,"acceptedIsUniqueFPS",0))return 0;
 if(pg_bool(j,"statsAvailable",1))f->stats_available=1;else if(!pg_bool(j,"statsAvailable",0))return 0;
 if(!pg_bool(j,"noTrackedFenceAtFinish",!f->stats_available))return 0;
#define F_U(k,m) if(!pg_u32(j,k,&f->m))return 0
#define F_S(k,m) if(!pg_s32(j,k,&f->m))return 0
#define F_D(k,m) if(!pg_decimal(j,k,&f->m))return 0
 F_U("mode",mode);F_U("configureAttempted",configure_attempted);F_U("closed",closed);F_U("createReturned",create_returned);F_U("pending",pending);F_U("retained",retained);F_U("uncertain",uncertain);F_U("pendingPresentMask",pending_mask);F_U("diagnosticWriteFailed",diagnostic_write_failed);
 F_S("createResult",create_result);F_S("lastWait",last_wait);F_S("lastReset",last_reset);F_S("finalIdle",final_idle);
 F_D("accepted",accepted);F_D("fenceCompleted",fence_completed);F_D("idleCompleted",idle_completed);F_D("enqueued",enqueued);F_D("returns",returns);F_D("waitCalls",wait_calls);F_D("waitTimeouts",wait_timeouts);F_D("resetCalls",reset_calls);F_D("finalIdleCalls",final_idle_calls);F_D("destroyCalls",destroy_calls);F_D("destroyReturns",destroy_returns);F_D("currentFence",current_fence);F_D("createdFence",created_fence);F_D("legacyVideoReturns",legacy_returns);
#undef F_U
#undef F_S
#undef F_D
 if(f->pending||f->retained||f->uncertain||f->pending_mask||f->current_fence||f->diagnostic_write_failed>1)return 0;
 if(!f->stats_available||f->configure_attempted!=1||f->closed!=1||f->create_returned!=1||f->final_idle||f->final_idle_calls!=1||f->mode!=2)return 0;
 return !f->create_result&&f->created_fence&&f->destroy_calls==2&&f->destroy_returns==2&&f->idle_completed<=2&&f->fence_completed<=UINT64_MAX-f->idle_completed&&f->accepted==f->fence_completed+f->idle_completed&&f->enqueued==f->accepted&&f->returns==f->accepted&&f->legacy_returns==f->accepted&&f->reset_calls==f->accepted&&!f->last_reset&&f->fence_completed<=UINT64_MAX-f->wait_timeouts&&f->wait_calls==f->fence_completed+f->wait_timeouts;
}
static int pg_readback(IzPs2ReceiptReadback*a,Rj*j,const IzPs2ReceiptCapsule*p){
 static const char keys[]="format\0""captured\0""afterActualGsJoin\0""nativeReadbackReleased\0""liveSlots\0""countersAreUniqueFrames\0""epoch\0""queued\0""completed\0""read\0""presenterAccepted\0""dropped\0""discarded\0""bootstrapSyncs\0";static const uint16_t keys_offsets[]={0,7,16,34,57,67,91,97,104,114,119,137,145,155};uint64_t n;
 if(!pg_row(j,p,9)||!pg_exact(j,keys,keys_offsets,sizeof keys_offsets/sizeof*keys_offsets)||!pg_text(j,0,"format","ps2-native-async-readback-final/1")||!pg_bool(j,"captured",1)||!pg_bool(j,"afterActualGsJoin",1)||!pg_bool(j,"nativeReadbackReleased",1)||!pg_bool(j,"countersAreUniqueFrames",0)||!pg_num(j,"liveSlots",&n)||n)return 0;
#define A_D(k,m) if(!pg_decimal(j,k,&a->m))return 0
 A_D("epoch",epoch);A_D("queued",queued);A_D("completed",completed);A_D("read",read);A_D("presenterAccepted",presenter_accepted);A_D("dropped",dropped);A_D("discarded",discarded);A_D("bootstrapSyncs",bootstrap_syncs);
#undef A_D
 return a->epoch&&a->queued==a->completed&&a->presenter_accepted<=a->read&&a->read<=a->completed&&a->bootstrap_syncs<=a->read;
}
uint32_t iz_ps2_decode_receipts(IzPs2Receipts*out,IzPs2ReceiptScratch*s,const IzPs2ReceiptCapsule*p,const IzPs2ReceiptImage*image){
 uint32_t error=1;if(!out)return error;
 if(((uintptr_t)out%_Alignof(IzPs2Receipts))||(s&&(uintptr_t)s%_Alignof(IzPs2ReceiptScratch))||(p&&(uintptr_t)p%_Alignof(IzPs2ReceiptCapsule))||(image&&(uintptr_t)image%_Alignof(IzPs2ReceiptImage)))return error;
 if((s&&!pg_apart(out,sizeof *out,s,sizeof *s))||(p&&!pg_apart(out,sizeof *out,p,sizeof *p))||(image&&!pg_apart(out,sizeof *out,image,sizeof *image))||(s&&p&&!pg_apart(s,sizeof *s,p,sizeof *p))||(s&&image&&!pg_apart(s,sizeof *s,image,sizeof *image)))return error;
 pg_zero(out,sizeof*out);if(!s||!p||!image){out->error=error;return error;}pg_zero(&s->candidate,sizeof s->candidate);IzPs2Receipts*c=&s->candidate;Rj*j=&s->json;
 uint64_t code_span=(IZ_PS2_CODE_BYTES+4095)&~UINT64_C(4095),data_span=(IZ_PS2_DATA_BYTES+4095)&~UINT64_C(4095);
 if(!pg_range(image->context,4096)||!pg_range(image->code_base,code_span)||!pg_range(image->data_base,data_span)||(image->context|image->code_base|image->data_base)&4095||image->code_bytes!=IZ_PS2_CODE_BYTES||image->data_bytes!=IZ_PS2_DATA_BYTES||!(image->code_base+code_span<=image->data_base||image->data_base+data_span<=image->code_base)||!(image->context+4096<=image->code_base||image->code_base+code_span<=image->context)||!(image->context+4096<=image->data_base||image->data_base+data_span<=image->context)){error=2;goto bad;}
 if(p->magic!=0x31524c50||p->version!=2||p->bytes!=53528||__atomic_load_n(&p->phase,__ATOMIC_ACQUIRE)!=2||p->error||p->count!=13||p->reserved||p->context!=image->context||p->code_base!=image->code_base||p->code_bytes!=image->code_bytes||p->data_base!=image->data_base||p->data_bytes!=image->data_bytes){error=3;goto bad;}
 unsigned present=0;for(unsigned i=0;i<13;i++){const IzPs2ReceiptRow*r=p->rows+i;if(r->id!=i+1){error=4;goto bad;}if(r->state==1){if(!r->bytes||r->bytes>4096||r->error){error=5;goto bad;}present++;}else if(i<10||r->state!=2||r->bytes||r->error!=2){error=6;goto bad;}}
 if(p->present!=present||p->rows[10].state!=2||p->rows[12].state!=2){error=7;goto bad;}
 if(!pg_common(c,j,p)){error=8;goto bad;}if(!pg_fence(&c->fence,j,p)){error=9;goto bad;}if(!pg_readback(&c->readback,j,p)){error=10;goto bad;}
 if(p->rows[11].state==1){if(!pg_row(j,p,11)||!pg_bool(j,"requested",1)||!pg_bool(j,"cooperativeShutdown",1)||!pg_bool(j,"autosaveAttempted",0)){error=11;goto bad;}c->exit_receipt_present=1;}
 c->version=1;c->bytes=sizeof*c;c->image=*image;c->transport_validated=1;pg_copy(out,c,sizeof*c);return 0;
bad:pg_zero(&s->candidate,sizeof s->candidate);out->error=error;return error;
}
static uint32_t pg_word(const uint8_t*p){uint32_t v=0;for(unsigned i=0;i<4;i++)v|=(uint32_t)p[i]<<(i*8);return v;}
static uint64_t pg_quad(const uint8_t*p){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(i*8);return v;}
uint32_t iz_ps2_validate_storage_status(const uint8_t s[128]){
 if(!s)return 0xf401;static const uint32_t expected[8]={0x31494c50,1,128,4,0,1,1,1};for(unsigned i=0;i<8;i++)if(pg_word(s+i*4)!=expected[i])return 0xf401;
 if(pg_quad(s+40)||pg_quad(s+72)||pg_quad(s+56)!=pg_quad(s+64)||pg_quad(s+88)!=IZ_PS2_MAIN_BYTES||pg_quad(s+96)!=IZ_PS2_JIT_BYTES||pg_quad(s+104)||pg_quad(s+112)||pg_quad(s+120))return 0xf401;return 0;
}
