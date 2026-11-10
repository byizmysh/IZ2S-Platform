/* Launcher-owned finite instruction controller. The existing supervisor owns
 * input, GPU, files and executable memory. This code only uses its app ABI. */
#include "native-app.h"
#include "checkpoint.h"
#define FAIL 0xe710u
#define MAGIC 0x434c5a49u
#define STATE_MAGIC 0x53525a49u
typedef uint32_t U;typedef uint64_t Q;typedef uint8_t B;
typedef struct {U magic,version,bytes,runtime_offset,guards,init_count,rules,stop_count,guard_at,init_at,rule_at,stop_at,ops_at,ops,strings_at,strings_bytes;} Program;
typedef struct {U space,offset,compare,value,mask,reserved;} Condition;
typedef struct {U conditions,count,operations,op_count,once,reserved[3];} Rule;
typedef struct {U kind,space,offset,source_space,source_offset,bytes,value,aux,reserved[2];} Operation;
typedef struct {
 _Alignas(16) B audio[3072];
 U magic,done_rules,rule,operation,read_done,init_done,stopped,failed;
 U attempted,opened,started,stop_audio,flushed,pending,unknown,closed;
} State;
_Static_assert(sizeof(Program)==64&&sizeof(Condition)==24&&sizeof(Rule)==32&&sizeof(Operation)==40&&sizeof(State)<=4096,"Local instruction ABI");
_Static_assert(sizeof(State)<=IZ_INSTRUCTION_MAILBOX,"Checkpoint mailbox is separate from live audio leases");
static Q address(const void*p){return (Q)(uintptr_t)p;}
static int range(Q offset,Q bytes,Q limit){return bytes&&offset<=limit&&bytes<=limit-offset;}
static void*memory(IzNativeApp2*a,U space,U offset,U bytes,int write){
 if(space==1)return a->private_memory&&range(offset,bytes,a->private_bytes)?(B*)a->private_memory+offset:0;
 if(space||!a->base.controls||!range(offset,bytes,a->base.controls_bytes)||!range(offset,bytes,0x700000))return 0;
 if(write&&!((offset>=784&&(Q)offset+bytes<=960)||(offset>=1024&&(Q)offset+bytes<=0x700000)))return 0;
 return (B*)a->base.controls+offset;
}
static int table(Program*p,U offset,U count,U stride){return offset>=64&&offset<=p->bytes&&(Q)count*stride<=p->bytes-offset;}
static const char*filename(Program*p,U at,int write){
 if(at<p->strings_at||at>=p->bytes)return 0;const char*s=(const char*)p+at;U n=0;
 while(at+n<p->bytes&&n<128&&s[n])n++;if(!n||n==128||at+n>=p->bytes||s[0]=='/')return 0;
 for(U i=0;i<n;){U start=i;while(i<n&&s[i]!='/'){U c=(B)s[i++];if(c<32||c==127||c=='\\'||c==':')return 0;}U length=i-start;if(!length||(length==1&&s[start]=='.')||(length==2&&s[start]=='.'&&s[start+1]=='.'))return 0;i++;}if(s[n-1]=='/')return 0;
 const char*blocked=write?"asset-":"";if(write){U same=1;for(U i=0;i<6;i++)if(i>=n||s[i]!=blocked[i])same=0;if(same)return 0;}
 const char*executable="launcher.izapp";U same=1;for(U i=0;i<13;i++)if(i>=n||s[i]!=executable[i])same=0;if(same&&(n==13||s[13]=='.'))return 0;return s;
}
static int valid_condition(IzNativeApp2*a,const Condition*c){return !c->reserved&&c->compare<=3&&!(c->offset&3)&&!(c->value&~c->mask)&&memory(a,c->space,c->offset,4,0);}
static int conditions(IzNativeApp2*a,Program*p,U at,U count){
 if(!table(p,at,count,sizeof(Condition)))return 0;const Condition*c=(void*)((B*)p+at);
 for(U i=0;i<count;i++){if(!valid_condition(a,c+i))return 0;U v=*(volatile U*)memory(a,c[i].space,c[i].offset,4,0)&c[i].mask;
  if((c[i].compare==0&&v!=c[i].value)||(c[i].compare==1&&v==c[i].value)||(c[i].compare==2&&v>c[i].value)||(c[i].compare==3&&v<c[i].value))return 0;}return 1;
}
static int valid_op(IzNativeApp2*a,Program*p,const Operation*o){
 if(o->reserved[0]||o->reserved[1]||!o->kind||o->kind>8)return 0;
 if(o->kind==1)return !(o->offset&3)&&memory(a,o->space,o->offset,4,1);
 if(o->kind==2){if(o->bytes>65536||!memory(a,o->space,o->offset,o->bytes,1)||!memory(a,o->source_space,o->source_offset,o->bytes,0))return 0;return o->space!=o->source_space||(Q)o->offset+o->bytes<=o->source_offset||(Q)o->source_offset+o->bytes<=o->offset;}
 if(o->kind==3||o->kind==4)return o->bytes<=16u*1024*1024&&(o->kind!=4||(o->bytes<=65536&&!o->source_offset))&&filename(p,o->aux,o->kind==4)&&memory(a,o->space,o->offset,o->bytes,o->kind==3);
 if(o->kind==6)return o->space==1&&!(o->offset&4095)&&o->bytes&&!(o->bytes&4095)&&o->value&&!(o->value&3)&&o->value<=o->bytes&&o->aux>=1&&o->aux<=32&&memory(a,1,o->offset,o->bytes,0);
 return 1;
}
static int validate(IzNativeApp2*a,Program*p){
 if(p->magic!=MAGIC||p->version!=1||p->bytes<64||p->bytes>65536||p->runtime_offset!=((p->bytes+15)&~15u)||!range(p->runtime_offset,IZ_INSTRUCTION_MAILBOX+sizeof(IzInstructionSaved),a->base.data_bytes)||p->guards>64||p->rules>32||p->init_count>32||p->stop_count>16||p->ops>256||p->strings_bytes>8192||p->guard_at!=64)return 0;
 if(!table(p,p->rule_at,p->rules,sizeof(Rule))||!table(p,p->ops_at,p->ops,sizeof(Operation))||p->ops_at!=p->rule_at+p->rules*sizeof(Rule)||p->init_at!=p->ops_at||p->strings_at!=p->ops_at+p->ops*sizeof(Operation)||p->strings_bytes!=p->bytes-p->strings_at)return 0;
 U ca=p->guard_at+p->guards*sizeof(Condition),oa=p->init_at+p->init_count*sizeof(Operation);const Rule*r=(void*)((B*)p+p->rule_at);
 for(U i=0;i<p->rules;i++){if(r[i].conditions!=ca||!r[i].count||r[i].count>64||r[i].operations!=oa||!r[i].op_count||r[i].op_count>32||r[i].once>1||r[i].reserved[0]||r[i].reserved[1]||r[i].reserved[2])return 0;ca+=r[i].count*sizeof(Condition);oa+=r[i].op_count*sizeof(Operation);}
 if(ca!=p->rule_at||p->stop_at!=oa||oa+p->stop_count*sizeof(Operation)!=p->strings_at||!table(p,p->guard_at,(ca-p->guard_at)/sizeof(Condition),sizeof(Condition)))return 0;
 if((ca-p->guard_at)/sizeof(Condition)>256)return 0;const Condition*c=(void*)((B*)p+p->guard_at);for(U i=0;i<(ca-p->guard_at)/sizeof(Condition);i++)if(!valid_condition(a,c+i))return 0;
 const Operation*o=(void*)((B*)p+p->ops_at);U queues=0,starts=0,stop_bytes=0;for(U i=0;i<p->ops;i++){if(!valid_op(a,p,o+i))return 0;queues+=o[i].kind==6;starts+=o[i].kind==7;if(p->ops_at+i*sizeof(Operation)>=p->stop_at){if(o[i].kind!=1&&o[i].kind!=2&&o[i].kind!=4)return 0;if(o[i].kind==4)stop_bytes+=o[i].bytes;}}
 if(queues>1||starts>1||stop_bytes>65536)return 0;if(queues||starts){if(!a->audio||a->audio->version!=1||a->audio->bytes!=sizeof(IzNativeAudio)||!a->ticks||!a->sleep||!a->base.tick_hz)return 0;for(U i=0;i<IZ_AUDIO_FUNCTIONS;i++)if(!a->audio->functions[i])return 0;}return 1;
}
static Q call(IzNativeApp2*a,U f,Q x,Q y,Q z,Q w){return a->audio->functions[f](x,y,z,w);}
static Q descriptor(State*s,U i){return address(s->audio+1024+i*64);}
static U queue(IzNativeApp2*a,State*s,const Operation*o){
 if(s->attempted||s->closed)return FAIL;call(a,IZ_AUDIO_INITIALIZE,address(s->audio+768),0,0,0);*(U*)(s->audio+768)=48000;*(uint16_t*)(s->audio+772)=2;
 s->attempted=1;if(call(a,IZ_AUDIO_OPEN,address(s->audio),address(s->audio+768),0,0))return s->unknown=1,FAIL;s->opened=1;
 if(call(a,IZ_AUDIO_RATE,address(s->audio),0,0,0)!=48000||call(a,IZ_AUDIO_CHANNELS,address(s->audio),0,0,0)!=2||call(a,IZ_AUDIO_FORMAT,address(s->audio),0,0,0)!=2)return FAIL;
 for(U i=0;i<o->aux;i++){call(a,IZ_AUDIO_BUFFER,descriptor(s,i),address(a->private_memory)+o->offset,o->bytes,o->value);Q rc=call(a,IZ_AUDIO_APPEND,address(s->audio),descriptor(s,i),0,0);if(rc!=1){if(rc>1)s->unknown=1;return FAIL;}s->pending|=1u<<i;}return 0;
}
static U cleanup(IzNativeApp2*a,State*s){
 if(s->unknown||(s->attempted&&!s->opened&&!s->closed))return FAIL;if(!s->opened)return 0;
 if(!s->stop_audio){if(call(a,IZ_AUDIO_STOP,address(s->audio),0,0,0))return s->unknown=1,FAIL;s->stop_audio=1;}
 if(!s->flushed){if(call(a,IZ_AUDIO_FLUSH,address(s->audio),0,0,0)>1)return s->unknown=1,FAIL;s->flushed=1;}
 Q until=a->ticks(&a->base)+a->base.tick_hz*2;while(s->pending){Q v=call(a,IZ_AUDIO_RELEASED,address(s->audio),0,0,0);if(!v){if(a->ticks(&a->base)>=until)return s->unknown=1,FAIL;a->sleep(&a->base,5000000);continue;}Q first=descriptor(s,0);if(v<first||(v-first)%64||(v-first)/64>=32)return s->unknown=1,FAIL;U bit=1u<<((v-first)/64);if(!(s->pending&bit))return s->unknown=1,FAIL;s->pending&=~bit;}
 if(call(a,IZ_AUDIO_COUNT,address(s->audio),0,0,0))return s->unknown=1,FAIL;call(a,IZ_AUDIO_CLOSE,address(s->audio),0,0,0);s->opened=0;s->closed=1;return 0;
}
/* Returns 1 to yield to presentation, 0 when complete, or an error. Reads
 * resume across frames and reopen/close through the supervisor on every step. */
static U execute(IzNativeApp2*a,Program*p,State*s,U at,U count,U*work,U*io){
 const Operation*ops=(void*)((B*)p+at);while(s->operation<count){if(!*work)return 1;const Operation*o=ops+s->operation;U rc=0;(*work)--;
  if(o->kind==1)*(volatile U*)memory(a,o->space,o->offset,4,1)=o->value;
  else if(o->kind==2){B*d=memory(a,o->space,o->offset,o->bytes,1),*b=memory(a,o->source_space,o->source_offset,o->bytes,0);for(U i=0;i<o->bytes;i++)d[i]=b[i];}
  else if(o->kind==3){U n=o->bytes-s->read_done;if(n>*io)n=*io;if(!n)return 1;U actual=0;B*d=memory(a,o->space,o->offset,o->bytes,1);rc=a->base.read_file(&a->base,filename(p,o->aux,0),(Q)o->source_offset+s->read_done,d+s->read_done,n,&actual);if(rc)return rc;if(actual!=n)return FAIL;s->read_done+=n;*io-=n;if(s->read_done<o->bytes)return 1;s->read_done=0;}
  else if(o->kind==4){if(o->bytes>*io)return 1;rc=a->base.write_file(&a->base,filename(p,o->aux,1),memory(a,o->space,o->offset,o->bytes,0),o->bytes);*io-=o->bytes;}
  else if(o->kind==5){a->base.request_exit(&a->base);return 2;}
  else if(o->kind==6)rc=queue(a,s,o);
  else if(o->kind==7){if(!s->opened||s->started||s->unknown||s->closed)return FAIL;if(call(a,IZ_AUDIO_START,address(s->audio),0,0,0))return s->unknown=1,FAIL;s->started=1;}
  else if(o->kind==8)iz_native_request_connection(&a->base);
  if(rc)return rc;s->operation++;
 }s->operation=0;return 0;
}
__attribute__((section(".text.iz_app"))) U iz_app(IzNativeApp*base,U event){
 if(!base||base->magic!=0x414e5a49||base->version!=2||base->bytes!=sizeof(IzNativeApp2)||!base->data||base->data_bytes<64||!base->controls||base->controls_bytes<1024||!base->read_file||!base->write_file||!base->request_exit)return FAIL;
 IzNativeApp2*a=(void*)base;Program*p=base->data;if(!validate(a,p))return FAIL;State*s=(void*)((B*)p+p->runtime_offset);U work=64,io=65536,rc=0;
 IzInstructionSaved*saved=(void*)((B*)s+IZ_INSTRUCTION_MAILBOX);
 if(event==IZ_INSTRUCTION_CAPTURE){
  if(s->magic!=STATE_MAGIC||s->stopped||s->failed||!s->init_done||s->rule||s->operation||s->read_done||s->unknown||s->closed||s->stop_audio||s->flushed||(s->attempted&&!s->opened))return IZ_INSTRUCTION_BUSY;
  *saved=(IzInstructionSaved){.magic=IZ_INSTRUCTION_SAVED,.version=1,.done_rules=s->done_rules,.audio_open=s->opened,.audio_started=s->started};return 0;
 }
 if(event==IZ_NATIVE_STOP){if(s->magic!=STATE_MAGIC||s->stopped)return 0;if(cleanup(a,s))return FAIL;if(s->failed||!s->init_done||s->read_done){s->stopped=1;return 0;}s->operation=s->read_done=0;rc=execute(a,p,s,p->stop_at,p->stop_count,&work,&io);if(rc)return s->failed=1,rc==1?FAIL:rc;s->stopped=1;return 0;}
 if(event==IZ_NATIVE_INIT){
  if(s->magic)return FAIL;s->magic=STATE_MAGIC;
  if(saved->magic){
   if(saved->magic!=IZ_INSTRUCTION_SAVED||saved->version!=1||saved->audio_open>1||saved->audio_started>saved->audio_open||saved->reserved[0]||saved->reserved[1]||saved->reserved[2]||(p->rules<32&&(saved->done_rules>>p->rules)))return s->failed=1,FAIL;
   if(!conditions(a,p,p->guard_at,p->guards))return s->failed=1,FAIL;
   s->done_rules=saved->done_rules;s->init_done=1;
   /* Recreate fresh service objects from the validated program. Never replay
    * reads, writes, initialization or one-shot application actions. */
   const Operation*ops=(void*)((B*)p+p->ops_at);U queued=0,started=0;
   for(U i=0;i<p->ops;i++){
    if(ops[i].kind==6&&saved->audio_open){rc=queue(a,s,ops+i);queued=1;}
    if(ops[i].kind==7&&saved->audio_started){if(!s->opened)return s->failed=1,FAIL;rc=(U)call(a,IZ_AUDIO_START,address(s->audio),0,0,0);if(!rc)s->started=started=1;else s->unknown=1;}
    if(rc)return s->failed=1,FAIL;
   }
   if(queued!=saved->audio_open||started!=saved->audio_started)return s->failed=1,FAIL;
   *saved=(IzInstructionSaved){0};return 0;
  }
 }else if(event!=IZ_NATIVE_FRAME||s->magic!=STATE_MAGIC||s->stopped||s->failed)return FAIL;
 if(!conditions(a,p,p->guard_at,p->guards))return s->failed=1,FAIL;
 if(!s->init_done){rc=execute(a,p,s,p->init_at,p->init_count,&work,&io);if(rc==1||rc==2)return 0;if(rc)return s->failed=1,rc;s->init_done=1;}
 if(event==IZ_NATIVE_INIT)return 0;
 const Rule*r=(void*)((B*)p+p->rule_at);for(U i=s->rule?s->rule-1:0;i<p->rules;i++){
  if(!s->rule){if((r[i].once&&(s->done_rules&(1u<<i)))||!conditions(a,p,r[i].conditions,r[i].count))continue;s->rule=i+1;}
  rc=execute(a,p,s,r[i].operations,r[i].op_count,&work,&io);if(rc==1||rc==2)return 0;if(rc)return s->failed=1,rc;s->done_rules|=1u<<i;s->rule=0;
 }return 0;
}
