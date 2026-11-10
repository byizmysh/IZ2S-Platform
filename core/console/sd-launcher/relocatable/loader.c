#include "loader.h"
#define ADDRESS(p) ((u64)(uintptr_t)(p))
static int span(u64 base,u64 bytes){return !(base&4095)&&bytes&&!(bytes&4095)&&base<(1ull<<39)&&bytes<=(1ull<<39)-base;}
static int range(u64 base,u64 bytes){return base&&span(base,bytes);}
static int overlap(u64 a,u64 n,u64 b,u64 m){return a<b+m&&b<a+n;}
static u32 retain(IzImage*n,u32 error){n->pending=1;n->ready=0;n->entry=n->spawn=0;n->stage=IZ_IMAGE_RETAINED;n->error=error;n->operation=0;if(n->fs&&n->fs->c){n->fs->c->poisoned=1;n->fs->c->native_status=error;}return error;}
static u32 ro(IzImage*n,u32 command,const u64 values[4],u64*out){
 IzContext*c=n->fs->c;_Alignas(4096) u8 message[4096];memset(message,0,sizeof message);u32*p=(void*)message;
 p[0]=4;p[1]=0x80000010u;p[2]=3;p[5]=0xffff8001u;p[8]=0x49434653;p[10]=command;memcpy(message+48,values,32);
 n->pending=1;u32 rc=(u32)CALL(c,F_SEND,message,4096,c->ro_handle,0,0,0,0,0);
 if(rc)return retain(n,rc);
 if(p[0]||!(p[1]&0x80000000u)||p[2]!=1||p[8]!=0x4f434653||p[9]||p[11]||((u64)p[3]|((u64)p[4]<<32))!=c->ro_pid||(p[1]&1023)<24)return retain(n,0xe611);
 u64*v=(void*)(message+48);if(v[0]!=c->pid||v[2]||(n->identity_bound&&v[1]!=n->program_id))return retain(n,0xe611);
 n->program_id=v[1];n->identity_bound=1;u32 count=command==0x524f0010&&!p[10]?13:8;
 if((p[1]&1023)<8+count*2)return retain(n,0xe611);
 if(out)memcpy(out,v,count*8);n->pending=0;return p[10];
}
enum {PH_OPEN=1,PH_HEADER,PH_ADDRESS_BEGIN,PH_ADDRESS_STEP,PH_DESTINATION,PH_ALLOC_CODE,PH_CHECK_CODE,PH_ALLOC_DATA,PH_CHECK_DATA,PH_RELOCATE_BEGIN,PH_ZERO_CODE,PH_ZERO_DATA,PH_READ_CODE,PH_READ_DATA,PH_FIXUPS,PH_CLOSE_INPUT,PH_ADMIT_BEGIN,PH_ADMIT_STEP,PH_CHECK_DESTINATION,PH_MAP,PH_PUBLISH,PH_CLOSE,PH_UNMAP,PH_FREE_CODE,PH_FREE_DATA,PH_CLEAN_CODE_BEGIN,PH_CLEAN_CODE_CHECK,PH_CLEAN_DATA_BEGIN,PH_CLEAN_DATA_CHECK,PH_REACH,PH_PREALLOC_DATA};
static u32 lock(IzImage*n){u32 expected=0;return __atomic_compare_exchange_n(&n->busy,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)?0:0xe619;}
static void unlock(IzImage*n){__atomic_store_n(&n->busy,0,__ATOMIC_RELEASE);}
static void walk_begin(IzImage*n,u64 at,u64 bytes,u32 type,int code,u32 next){n->walk_at=at;n->walk_end=at+bytes;n->walk_type=type;n->walk_code=(u32)code;n->walk_regions=0;n->walk_next=next;}
static u32 walk_step(IzImage*n){
 if(++n->walk_regions>1024)return 0xe612;u64 values[]={n->fs->c->pid,n->walk_at,0,0},v[13];u32 rc=ro(n,0x524f0010,values,v);if(rc)return rc;
 if(v[3]||!span(v[4],v[5])||v[4]>n->walk_at||v[4]+v[5]<=n->walk_at||v[6]!=n->walk_type||v[7]!=(n->walk_type?3u:0u)||v[8])return 0xe612;
 if(n->walk_code&&(!range(v[9],v[10])||n->walk_at<v[9]||n->walk_end>v[9]+v[10]))return 0xe612;
 u64 next=v[4]+v[5];n->walk_at=next<n->walk_end?next:n->walk_end;if(n->walk_at==n->walk_end)n->phase=n->walk_next;return 0;
}
static u32 allocate(IzImage*n,u64 bytes,u64*out){
 *out=0;u32 rc=(u32)CALL(n->fs->c,F_ALLOCATE,out,4096,bytes,0,0,0,0,0);
 if(rc)return *out?retain(n,0xe614):rc;
 if(!range(*out,bytes)||overlap(*out,bytes,ADDRESS(n->fs->buffer),65536)||overlap(*out,bytes,ADDRESS(n),sizeof *n))return retain(n,0xe614);return 0;
}
static u32 close_file(IzImage*n){if(!n->file)return 0;u32 rc=iz_fs_close(n->fs,n->file);if(rc)return retain(n,rc);n->file=0;return 0;}
static void closing(IzImage*n,u32 error){n->error=error;n->ready=0;n->entry=n->spawn=0;n->operation=2;n->phase=PH_CLEAN_CODE_BEGIN;n->stage=IZ_IMAGE_CLOSING;}
static u32 read_step(IzImage*n,unsigned section,u32 next){
 u32 bytes=n->header.file_bytes[section];if(n->offset<bytes){u32 count=(u32)(bytes-n->offset);if(count>65536)count=65536;
  u32 rc=iz_fs_read(n->fs,n->file,n->header.offsets[section]+n->offset,n->fs->buffer,count);if(rc)return rc;
  n->section_crc=iz_crc_update(n->section_crc,n->fs->buffer,count);memcpy((void*)(uintptr_t)((section?n->data:n->source)+n->offset),n->fs->buffer,count);n->offset+=count;n->read_bytes+=count;
 }if(n->offset==bytes){if(n->section_crc!=n->header.crc32[section])return 0xe110;n->offset=0;n->section_crc=0;n->phase=next;n->stage=section?IZ_IMAGE_FIXUPS:IZ_IMAGE_DATA;}return 0;
}
static u32 advance(IzImage*n){
 IzContext*c=n->fs->c;u32 rc=0,done=0;u64 code_bytes=n->header.memory_bytes[0],data_bytes=n->header.memory_bytes[1];
 switch(n->phase){
 case PH_OPEN:rc=iz_fs_open(n->fs,n->path,&n->file,&n->file_bytes);if(!rc)n->phase=PH_HEADER;break;
 case PH_HEADER:{if(n->file_bytes<sizeof n->header)return 0xe601;rc=iz_fs_read(n->fs,n->file,0,&n->header,sizeof n->header);if(rc)break;n->read_bytes+=sizeof n->header;void*segments[2]={(void*)1,(void*)1};rc=iz_reloc_begin(&n->reloc,&n->header,n->file_bytes,segments,n->header.source_bases);if(!rc){rc=iz_reloc_reach_begin(&n->reach,&n->header);n->phase=PH_REACH;}break;}
 case PH_REACH:{
  if(n->reach.processed<n->header.record_count){u32 count=n->header.record_count-n->reach.processed;if(count>IZ_RELOC_CHUNK_RECORDS)count=IZ_RELOC_CHUNK_RECORDS;u32 bytes=count*sizeof(IzRelocRecord);rc=iz_fs_read(n->fs,n->file,n->header.offsets[2]+(u64)n->reach.processed*sizeof(IzRelocRecord),n->fs->buffer,bytes);if(rc)break;n->section_crc=iz_crc_update(n->section_crc,n->fs->buffer,bytes);rc=iz_reloc_reach_batch(&n->reach,&n->header,(const void*)n->fs->buffer,count);if(rc)break;n->read_bytes+=bytes;}
  if(n->reach.processed==n->header.record_count){if(n->section_crc!=n->header.crc32[2])return 0xe110;n->section_crc=0;n->phase=n->reach.restricted?PH_PREALLOC_DATA:PH_ADDRESS_BEGIN;}break;}
 case PH_PREALLOC_DATA:rc=allocate(n,data_bytes,&n->data);if(!rc){walk_begin(n,n->data,data_bytes,5,0,PH_ADDRESS_BEGIN);n->phase=PH_CHECK_DATA;}break;
 case PH_ADDRESS_BEGIN:{
  IzAddressRange bounds,*limit=0;if(n->reach.restricted){int64_t lo=(int64_t)n->data+n->reach.minimum,hi=(int64_t)n->data+n->reach.maximum;int64_t last=(1ll<<39)-(int64_t)code_bytes;if(lo<4096)lo=4096;if(hi>last)hi=last;lo=(lo+4095)&~4095ll;hi&=~4095ll;if(lo>hi)return 0xe606;
   bounds=(IzAddressRange){(u64)lo,(u64)(hi-lo)+code_bytes};limit=&bounds;if(n->has_plan&&(n->plan.code_base<(u64)lo||n->plan.code_base>(u64)hi))return 0xe606;
  }rc=iz_address_begin_bounded(c,&n->address,code_bytes,n->has_plan?&n->plan:0,limit);if(!rc)n->phase=PH_ADDRESS_STEP;break;}
 case PH_ADDRESS_STEP:rc=iz_address_step(c,&n->address,&done);if(!rc&&done){n->plan=n->address.plan;n->destination=n->plan.code_base;n->pages=code_bytes/4096;walk_begin(n,n->destination,code_bytes,0,1,PH_ALLOC_CODE);n->phase=PH_DESTINATION;}break;
 case PH_DESTINATION:rc=walk_step(n);break;
 case PH_ALLOC_CODE:rc=allocate(n,code_bytes,&n->source);if(!rc){walk_begin(n,n->source,code_bytes,5,0,PH_ALLOC_DATA);n->phase=PH_CHECK_CODE;}break;
 case PH_CHECK_CODE:rc=walk_step(n);if(rc&&!n->pending)rc=retain(n,rc);if(!rc&&n->phase==PH_ALLOC_DATA)n->verified|=1;break;
 case PH_ALLOC_DATA:if(n->data){n->phase=PH_RELOCATE_BEGIN;break;}rc=allocate(n,data_bytes,&n->data);if(!rc){walk_begin(n,n->data,data_bytes,5,0,PH_RELOCATE_BEGIN);n->phase=PH_CHECK_DATA;}break;
 case PH_CHECK_DATA:rc=walk_step(n);if(rc&&!n->pending)rc=retain(n,rc);if(!rc&&n->phase!=PH_CHECK_DATA)n->verified|=2;break;
 case PH_RELOCATE_BEGIN:{if(overlap(n->source,code_bytes,n->data,data_bytes)||overlap(n->destination,code_bytes,n->source,code_bytes)||overlap(n->destination,code_bytes,n->data,data_bytes))return retain(n,0xe614);void*segments[2]={(void*)(uintptr_t)n->source,(void*)(uintptr_t)n->data};u64 bases[]={n->destination,n->data};rc=iz_reloc_begin(&n->reloc,&n->header,n->file_bytes,segments,bases);if(!rc){n->offset=n->header.file_bytes[0];n->phase=PH_ZERO_CODE;}break;}
 case PH_ZERO_CODE:case PH_ZERO_DATA:{unsigned section=n->phase==PH_ZERO_DATA;u32 bytes=n->header.memory_bytes[section];if(n->offset<bytes){u32 count=(u32)(bytes-n->offset);if(count>65536)count=65536;memset((void*)(uintptr_t)((section?n->data:n->source)+n->offset),0,count);n->offset+=count;n->cleared_bytes+=count;}if(n->offset==bytes){if(!section){n->offset=n->header.file_bytes[1];n->phase=PH_ZERO_DATA;}else{n->offset=0;n->phase=PH_READ_CODE;n->stage=IZ_IMAGE_CODE;}}break;}
 case PH_READ_CODE:rc=read_step(n,0,PH_READ_DATA);break;
 case PH_READ_DATA:rc=read_step(n,1,PH_FIXUPS);break;
 case PH_FIXUPS:{
  if(n->records_processed<n->header.record_count){u32 count=n->header.record_count-n->records_processed;if(count>IZ_RELOC_CHUNK_RECORDS)count=IZ_RELOC_CHUNK_RECORDS;u32 bytes=count*sizeof(IzRelocRecord);rc=iz_fs_read(n->fs,n->file,n->header.offsets[2]+(u64)n->records_processed*sizeof(IzRelocRecord),n->fs->buffer,bytes);if(rc)break;n->section_crc=iz_crc_update(n->section_crc,n->fs->buffer,bytes);rc=iz_reloc_batch(&n->reloc,(const void*)n->fs->buffer,count);if(rc)break;n->records_processed+=count;n->read_bytes+=bytes;}
  if(n->records_processed==n->header.record_count){if(n->section_crc!=n->header.crc32[2])return 0xe110;rc=iz_reloc_finish(&n->reloc);if(!rc)n->phase=PH_CLOSE_INPUT;}break;}
 case PH_CLOSE_INPUT:rc=close_file(n);if(!rc)n->phase=PH_ADMIT_BEGIN;break;
 case PH_ADMIT_BEGIN:rc=iz_address_begin(c,&n->address,code_bytes,&n->plan);if(!rc)n->phase=PH_ADMIT_STEP;break;
 case PH_ADMIT_STEP:rc=iz_address_step(c,&n->address,&done);if(!rc&&done){walk_begin(n,n->destination,code_bytes,0,1,PH_MAP);n->phase=PH_CHECK_DESTINATION;}break;
 case PH_CHECK_DESTINATION:rc=walk_step(n);if(!rc&&n->phase==PH_MAP)n->stage=IZ_IMAGE_MAP;break;
 case PH_MAP:{if(n->mapped==n->pages){n->phase=PH_PUBLISH;break;}u64 values[]={c->pid,n->source+n->mapped*4096,n->destination+n->mapped*4096,4096},v[13];rc=ro(n,0x524f0012,values,v);if(n->pending)return rc;
  if(rc){if(v[4]&&(!v[3]&&v[4]==rc)){if(v[5]){n->mapped++;return retain(n,rc);}}else if(v[4]||v[5])return retain(n,0xe617);break;}
  if(v[3]||v[4]||v[5]){n->mapped++;return retain(n,0xe617);}n->mapped++;if(n->mapped==n->pages)n->phase=PH_PUBLISH;break;}
 case PH_PUBLISH:n->entry=n->destination+n->header.entry_offset;n->spawn=n->destination+n->header.spawn_offset;n->ready=1;n->stage=IZ_IMAGE_READY;n->operation=0;break;
 case PH_CLEAN_CODE_BEGIN:if(n->source&&!(n->verified&1)){walk_begin(n,n->source,code_bytes,5,0,PH_CLEAN_DATA_BEGIN);n->phase=PH_CLEAN_CODE_CHECK;}else n->phase=PH_CLEAN_DATA_BEGIN;break;
 case PH_CLEAN_CODE_CHECK:rc=walk_step(n);if(rc&&!n->pending)rc=retain(n,rc);if(!rc&&n->phase==PH_CLEAN_DATA_BEGIN)n->verified|=1;break;
 case PH_CLEAN_DATA_BEGIN:if(n->data&&!(n->verified&2)){walk_begin(n,n->data,data_bytes,5,0,PH_CLOSE);n->phase=PH_CLEAN_DATA_CHECK;}else n->phase=PH_CLOSE;break;
 case PH_CLEAN_DATA_CHECK:rc=walk_step(n);if(rc&&!n->pending)rc=retain(n,rc);if(!rc&&n->phase==PH_CLOSE)n->verified|=2;break;
 case PH_CLOSE:rc=close_file(n);if(!rc)n->phase=PH_UNMAP;break;
 case PH_UNMAP:if(n->mapped){u64 page=n->mapped-1,values[]={c->pid,n->source+page*4096,n->destination+page*4096,4096},v[13];rc=ro(n,0x524f0013,values,v);if(rc)return retain(n,rc);n->mapped--;}if(!n->mapped)n->phase=PH_FREE_CODE;break;
 case PH_FREE_CODE:if(n->source){CALL(c,F_FREE,n->source,0,0,0,0,0,0,0);n->source=0;}n->phase=PH_FREE_DATA;break;
 case PH_FREE_DATA:if(n->data){CALL(c,F_FREE,n->data,0,0,0,0,0,0,0);n->data=0;}n->entry=n->spawn=0;n->ready=0;n->fs=0;n->operation=0;n->stage=n->error?IZ_IMAGE_FAILED:IZ_IMAGE_CLOSED;break;
 default:return 0xe618;
 }return rc;
}
u32 iz_image_begin(IzFs*fs,IzImage*n,const char*path,const IzImagePlan*plan){
 if(!n)return 0xe618;u32 rc=lock(n);if(rc)return rc;
 if(n->operation||n->source||n->data||n->mapped||n->file||n->ready||n->pending){unlock(n);return 0xe619;}
 memset((u8*)n+sizeof n->busy,0,sizeof *n-sizeof n->busy);n->fs=fs;
 if(!fs||!fs->c||!fs->buffer||!path||!fs->c->ro_handle||!fs->c->pid||!fs->c->ro_pid||!fs->c->functions[F_SEND]||!fs->c->functions[F_ALLOCATE]||!fs->c->functions[F_FREE]){n->fs=0;rc=0xe618;}
 else if(fs->c->poisoned)rc=retain(n,0xe615);
 else{size_t bytes=0;while(bytes<sizeof n->path&&path[bytes])bytes++;if(!bytes||bytes==sizeof n->path){n->fs=0;rc=0xe618;}else{memcpy(n->path,path,bytes+1);if(plan){n->plan=*plan;n->has_plan=1;}n->stage=IZ_IMAGE_HEADER;n->phase=PH_OPEN;n->operation=1;}}
 unlock(n);return rc;
}
u32 iz_image_close_begin(IzImage*n,u32 error){
 if(!n)return 0xe618;u32 rc=lock(n);if(rc)return rc;
 if(n->pending||(n->fs&&n->fs->c->poisoned))rc=retain(n,n->error?n->error:0xe615);
 else if(!n->fs){n->stage=IZ_IMAGE_CLOSED;n->operation=0;n->ready=0;n->error=0;}
 else if(n->operation!=2)closing(n,error);unlock(n);return rc;
}
u32 iz_image_step(IzImage*n,u32 budget,u32*done){
 if(!n||!done||!budget||budget>64)return 0xe618;*done=0;u32 rc=lock(n);if(rc)return rc;n->last_units=0;
 if(n->pending||(n->fs&&n->fs->c->poisoned)){rc=retain(n,n->error?n->error:0xe615);n->operation=0;*done=1;unlock(n);return rc;}
 while(n->operation&&n->last_units<budget){
  if(n->operation==1&&__atomic_load_n(&n->fs->c->stop,__ATOMIC_ACQUIRE))closing(n,0xe613);
  rc=advance(n);n->last_units++;n->units++;
  if(rc){if(n->pending||(n->fs&&n->fs->c->poisoned)){rc=retain(n,rc);n->operation=0;break;}closing(n,rc);rc=0;}
 }
 if(!n->operation){*done=1;rc=n->error;}unlock(n);return rc;
}
u32 iz_image_load(IzFs*fs,IzImage*n,const char*path,const IzImagePlan*plan,IzImageProgress progress,void*user){
 u32 rc=iz_image_begin(fs,n,path,plan);if(rc)return rc;u32 done=0;
 while(!done){if(n->operation==1&&progress){rc=progress(user,n);if(rc){u32 close=iz_image_close_begin(n,rc);if(close)return close;}}rc=iz_image_step(n,1,&done);if(rc&&!done)return rc;}return rc;
}
u32 iz_image_unload(IzImage*n){
 if(!n)return 0xe618;if(n->operation)return 0xe619;u32 rc=iz_image_close_begin(n,0);if(rc)return rc;u32 done=0;while(!done){rc=iz_image_step(n,64,&done);if(rc&&!done)return rc;}return rc;
}
