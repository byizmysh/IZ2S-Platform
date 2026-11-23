#ifndef IZ_SD_CHECKPOINT_H
#define IZ_SD_CHECKPOINT_H
#include "abi.h"
#include "../instructions/checkpoint.h"
#ifndef IZ_CHECKPOINT_RUNTIME_IMAGE
#define IZ_CHECKPOINT_RUNTIME_IMAGE "checkpoint-runtime.inc"
#endif
#include IZ_CHECKPOINT_RUNTIME_IMAGE
enum { CP_NONE,CP_COLD,CP_RESTORED,CP_SAVING,CP_SAVED,CP_SKIPPED };
#define CP_INVALID 0xe733u
#define CP_CHUNK 65536u
typedef struct {
 char magic[8];u32 version,header_bytes;
 u64 bytes,generation,ticks,frames;
 u32 kind,private_bytes,logical_bytes,records,stream_crc,header_crc,tick_hz,reserved;
 IzInstructionSaved portable;u8 padding[400];IzAppHeader app;
} IzCheckpointHeader;
typedef struct {u32 logical,stored,crc,reserved;} IzCheckpointRecord;
typedef struct {u32 kind,bank,ending,save_blocked;u64 generation,frames;IzInstructionSaved portable;} IzCheckpointSession;
_Static_assert(sizeof(IzCheckpointHeader)==1024&&sizeof(IzCheckpointRecord)==16,"SD checkpoint format");
static void cp_path(char*out,const IzAppHeader*h,u32 bank){
 size_t n=sizeof(IZ_SD_ROOT)-1;memcpy(out,IZ_SD_ROOT,n);out[n++]='/';size_t k=strlen(h->id);memcpy(out+n,h->id,k);n+=k;
 const char*s="/.launcher-state-a.izsave";memcpy(out+n,s,strlen(s)+1);out[n+17]=(char)('a'+bank);
}
static u32 cp_kind(const IzAppHeader*h,const IzAppHeader*loaded,const u8*image){
 if(!(h->flags&IZ_APP_NATIVE))return 1;
 if(h->version==2&&!(h->flags&(IZ_APP_ENGINE_IMAGE|IZ_APP_COOPERATIVE|IZ_APP_WORKER|IZ_APP_PRESENTATION))&&loaded->section[SEC_NATIVE_CODE].bytes==sizeof iz_checkpoint_instruction_image&&!memcmp(image+loaded->section[SEC_NATIVE_CODE].offset,iz_checkpoint_instruction_image,sizeof iz_checkpoint_instruction_image))return 2;
 return 0;
}
static u32 cp_logical(const IzAppHeader*h){return 176u+(0x700000u-1024u)+h->private_bytes;}
/* Only byte-offset storage goes on disk. Input, touch, menu/exit requests,
 * host synchronization, SDK addresses and GPU/DSP objects are excluded. */
static u8*cp_region(IzContext*c,void*private_memory,u32 position,u32*available){
 if(position<176){*available=176-position;return (u8*)(uintptr_t)c->controls+784+position;}
 position-=176;if(position<0x700000u-1024u){*available=0x700000u-1024u-position;return (u8*)(uintptr_t)c->controls+1024+position;}
 position-=0x700000u-1024u;*available=0xffffffffu-position;return (u8*)private_memory+position;
}
static void cp_apply(IzContext*c,void*private_memory,u32 position,const u8*bytes,u32 count){
 while(count){u32 available=0;u8*out=cp_region(c,private_memory,position,&available);u32 n=count<available?count:available;if(bytes){memcpy(out,bytes,n);bytes+=n;}else memset(out,0,n);position+=n;count-=n;}
}
static u32 cp_header_crc(IzCheckpointHeader*h){u32 old=h->header_crc;h->header_crc=0;u32 crc=iz_crc(h,sizeof *h);h->header_crc=old;return crc;}
static int cp_portable(const IzCheckpointHeader*h){
 const IzInstructionSaved*p=&h->portable;
 if(h->kind==1){IzInstructionSaved zero={0};return !memcmp(p,&zero,sizeof zero);}
 return p->magic==IZ_INSTRUCTION_SAVED&&p->version==1&&p->audio_open<=1&&p->audio_started<=p->audio_open&&!p->reserved[0]&&!p->reserved[1]&&!p->reserved[2];
}
static int cp_header_valid(IzContext*c,IzCheckpointHeader*h,u64 size,const IzAppHeader*app,u32 kind){
 if(memcmp(h->magic,"IZSAVE01",8)||h->version!=1||h->header_bytes!=sizeof *h||h->bytes!=size||size<sizeof *h||size>144u*1024*1024||!h->generation||h->kind!=kind||h->private_bytes!=app->private_bytes||h->logical_bytes!=cp_logical(app)||!h->records||h->records>(h->logical_bytes+CP_CHUNK-1)/CP_CHUNK+3||h->tick_hz!=c->tick_hz||h->reserved||memcmp(&h->app,app,sizeof *app)||h->header_crc!=cp_header_crc(h)||!cp_portable(h))return 0;
 for(unsigned i=0;i<sizeof h->padding;i++)if(h->padding[i])return 0;return 1;
}
static u32 cp_open(IzFs*fs,const IzAppHeader*app,u32 kind,const char*path,IzCheckpointHeader*h,u32*file){
 u64 size=0;u32 rc=iz_fs_open(fs,path,file,&size);
 if(rc)return rc;
 rc=size<sizeof *h?CP_INVALID:iz_fs_read(fs,*file,0,h,sizeof *h);
 if(!rc&&!cp_header_valid(fs->c,h,size,app,kind))rc=CP_INVALID;
 return rc;
}
static u32 cp_close(IzFs*fs,u32 file,u32 rc){
 if(file&&!fs->c->poisoned){u32 close=iz_fs_close(fs,file);if(!rc)rc=close;}
 return rc;
}
typedef struct {
 u64 offset,size;
 u32 next,available;
 u8 bytes[CP_CHUNK];
} IzCheckpointReader;
static u32 cp_take(IzFs*fs,u32 file,IzCheckpointReader*r,void*out,u32 count){
 u8*dest=out;
 while(count){
  if(!r->available){
   u64 remaining=r->size-r->offset;
   if(!remaining)return CP_INVALID;
   u32 chunk=remaining<CP_CHUNK?(u32)remaining:CP_CHUNK;
   u32 rc=iz_fs_read(fs,file,r->offset,r->bytes,chunk);
   if(rc)return rc;
   r->offset+=chunk;r->next=0;r->available=chunk;
  }
  u32 n=count<r->available?count:r->available;
  memcpy(dest,r->bytes+r->next,n);
  r->next+=n;r->available-=n;dest+=n;count-=n;
 }
 return 0;
}
/* Validate each record before applying it. The caller must keep the app
 * stopped until the entire stream succeeds or initial memory is rebuilt. */
static u32 cp_stream(IzFs*fs,u32 file,const IzCheckpointHeader*h,void*private_memory,int apply){
 IzContext*c=fs->c;u32 rc=0;
 u8 bytes[CP_CHUNK];u64 size=h->bytes,at=sizeof *h;u32 position=0,stream=0;
 IzCheckpointReader reader;reader.offset=sizeof *h;reader.size=size;reader.next=reader.available=0;
 for(u32 i=0;!rc&&i<h->records;i++){
  IzCheckpointRecord record;
  if(at>size||sizeof record>size-at){rc=CP_INVALID;break;}
  rc=cp_take(fs,file,&reader,&record,sizeof record);if(rc)break;at+=sizeof record;stream=iz_crc_update(stream,&record,sizeof record);
  if(!record.logical||record.logical>h->logical_bytes-position||record.reserved||(record.stored&&(record.stored!=record.logical||record.stored>CP_CHUNK))||(!record.stored&&record.crc)||record.stored>size-at){rc=CP_INVALID;break;}
  if(record.stored){rc=cp_take(fs,file,&reader,bytes,record.stored);if(rc)break;at+=record.stored;if(iz_crc(bytes,record.stored)!=record.crc){rc=CP_INVALID;break;}stream=iz_crc_update(stream,bytes,record.stored);}
  if(apply)cp_apply(c,private_memory,position,record.stored?bytes:0,record.logical);position+=record.logical;
  if(c->checkpoint_state==CP_SAVING)c->checkpoint_progress=(u64)h->logical_bytes+position;
 }
 if(!rc&&(position!=h->logical_bytes||at!=size||stream!=h->stream_crc))rc=CP_INVALID;
 return rc;
}
static u32 cp_read(IzFs*fs,const IzAppHeader*app,u32 kind,const char*path,IzCheckpointHeader*out){
 u32 file=0;
 u32 rc=cp_open(fs,app,kind,path,out,&file);
 if(!rc)rc=cp_stream(fs,file,out,0,0);
 return cp_close(fs,file,rc);
}
static u32 cp_restore(IzFs*fs,const IzAppHeader*h,u32 kind,u32 rules,void*private_memory,IzCheckpointSession*s){
 IzContext*c=fs->c;memset(s,0,sizeof *s);s->kind=kind;c->checkpoint_state=kind&&!c->ram_only?CP_COLD:CP_SKIPPED;c->checkpoint_error=0;c->checkpoint_kind=kind;c->checkpoint_bank=0;c->checkpoint_generation=c->checkpoint_logical=c->checkpoint_stored=c->checkpoint_time_bias=c->checkpoint_progress=0;
 if(!kind||c->ram_only){if(!kind&&c->checkpoint_enabled&&(h->flags&IZ_APP_NATIVE))c->checkpoint_error=0xe734;return 0;}
 IzCheckpointHeader headers[2];u32 handles[2]={0},bank=0,failed=0;u64 generations[2]={0};char path[160];
 // Keep the verified handles through selection and body reads. Reopening
 // the chosen path would add SD calls and could select a different file.
 for(unsigned i=0;i<2;i++){
  cp_path(path,h,i);
  u32 rc=cp_open(fs,h,kind,path,&headers[i],&handles[i]);
  if(c->poisoned)return rc;
  if(!rc&&kind==2&&rules<32&&(headers[i].portable.done_rules>>rules))rc=CP_INVALID;
  if(!rc){generations[i]=headers[i].generation;continue;}
  if(handles[i]||(rc&0x3fffffu)!=0x202){
   c->checkpoint_error=rc;
   if(rc!=CP_INVALID)s->save_blocked=1;
  }
  rc=cp_close(fs,handles[i],rc);handles[i]=0;
  if(c->poisoned)return rc;
 }
 bank=generations[1]>generations[0];
 for(unsigned attempt=0;attempt<2;attempt++,bank^=1u){
  if(!handles[bank])continue;
  IzCheckpointHeader*chosen=&headers[bank];
  u32 rc=cp_stream(fs,handles[bank],chosen,private_memory,1);
  rc=cp_close(fs,handles[bank],rc);handles[bank]=0;
  if(c->poisoned)return rc;
  if(!rc){
   rc=cp_close(fs,handles[bank^1u],0);
   if(rc)return rc;
   s->bank=bank;s->generation=chosen->generation;s->frames=chosen->frames;s->portable=chosen->portable;
   c->checkpoint_state=CP_RESTORED;c->checkpoint_bank=bank;c->checkpoint_generation=chosen->generation;c->checkpoint_logical=chosen->logical_bytes;c->checkpoint_stored=chosen->bytes;
   c->checkpoint_time_bias=CALL(c,F_TICK,0,0,0,0,0,0,0,0)-chosen->ticks;
   *(u64*)((u8*)(uintptr_t)c->controls+0x700d70)=chosen->ticks;
   return 0;
  }
  if(rc!=CP_INVALID)s->save_blocked=1;
  failed=c->checkpoint_error=rc;
 }
 return failed;
}

typedef struct {
 u64 offset,capacity;
 u32 used;
 u8 bytes[CP_CHUNK];
} IzCheckpointWriter;
static u32 cp_flush(IzFs*fs,u32 file,IzCheckpointWriter*w){
 if(!w->used)return 0;
 u64 end=w->offset+w->used;
 if(end>w->capacity){
  u64 capacity=(end+1048575)&~1048575ull;
  u32 rc=iz_fs_checkpoint_resize(fs,file,capacity);
  if(rc)return rc;
  w->capacity=capacity;
 }
 u32 rc=iz_fs_checkpoint_write(fs,file,w->offset,w->bytes,w->used);
 if(!rc){w->offset=end;w->used=0;}
 return rc;
}
static u32 cp_append(IzFs*fs,u32 file,IzCheckpointWriter*w,const void*data,u32 count){
 const u8*source=data;
 while(count){
  u32 available=CP_CHUNK-w->used,n=count<available?count:available;
  memcpy(w->bytes+w->used,source,n);
  w->used+=n;source+=n;count-=n;
  if(w->used==CP_CHUNK){u32 rc=cp_flush(fs,file,w);if(rc)return rc;}
 }
 return 0;
}
static u32 cp_write_record(IzFs*fs,u32 file,IzCheckpointHeader*h,IzCheckpointWriter*w,const IzCheckpointRecord*r,const void*data){
 u32 rc=cp_append(fs,file,w,r,sizeof *r);
 if(rc)return rc;
 h->bytes+=sizeof *r;h->stream_crc=iz_crc_update(h->stream_crc,r,sizeof *r);
 if(r->stored){
  rc=cp_append(fs,file,w,data,r->stored);
  if(rc)return rc;
  h->bytes+=r->stored;h->stream_crc=iz_crc_update(h->stream_crc,data,r->stored);
 }
 h->records++;
 return 0;
}
static u32 cp_skipped(IzContext*c,u32 rc){c->checkpoint_state=CP_SKIPPED;c->checkpoint_error=rc;return rc;}
static u32 cp_save(IzFs*fs,const IzAppHeader*app,void*private_memory,IzNative*n,IzCheckpointSession*s){
 IzContext*c=fs->c;if(s->save_blocked||!s->kind||c->ram_only){c->checkpoint_state=CP_SKIPPED;return 0;}
 if(c->poisoned||n->unknown||n->worker_state||n->presentation_owned||n->image_state)return cp_skipped(c,CP_INVALID);
 IzInstructionSaved portable={0};u32 rc=0;
 if(s->kind==2){
  if(!n->initialized||n->stopping||n->app.version!=2||n->app.data_bytes<64)return cp_skipped(c,CP_INVALID);
  u32*program=n->app.data;u64 at=(u64)program[3]+IZ_INSTRUCTION_MAILBOX;
  if(at>n->app.data_bytes||sizeof portable>n->app.data_bytes-at)return cp_skipped(c,CP_INVALID);
  rc=iz_native_event(n,IZ_INSTRUCTION_CAPTURE);if(rc){c->checkpoint_state=CP_SKIPPED;c->checkpoint_error=rc;return rc;}memcpy(&portable,(u8*)n->app.data+at,sizeof portable);
 }
 if(s->generation==~0ull)return cp_skipped(c,CP_INVALID);
 u32 bank=s->generation?s->bank^1u:0;char path[160];cp_path(path,app,bank);
 IzCheckpointHeader h;memset(&h,0,sizeof h);memcpy(h.magic,"IZSAVE01",8);h.version=1;h.header_bytes=sizeof h;h.bytes=sizeof h;h.generation=s->generation+1;h.ticks=CALL(c,F_TICK,0,0,0,0,0,0,0,0)-c->checkpoint_time_bias;h.frames=n->initialized?n->app.frames:0;h.kind=s->kind;h.private_bytes=app->private_bytes;h.logical_bytes=cp_logical(app);h.tick_hz=(u32)c->tick_hz;h.portable=portable;h.app=*app;
 c->checkpoint_state=CP_SAVING;c->checkpoint_error=0;c->checkpoint_logical=h.logical_bytes;c->checkpoint_stored=0;c->checkpoint_progress=0;
 u32 file=0,position=0,zeros=0;IzCheckpointWriter writer;
 writer.offset=writer.capacity=sizeof h;writer.used=0;
 u8 bytes[CP_CHUNK];rc=iz_fs_checkpoint_begin(fs,path,&file);
 while(!rc&&position<h.logical_bytes){
  u32 available=0;u8*source=cp_region(c,private_memory,position,&available);u32 count=h.logical_bytes-position;if(count>CP_CHUNK)count=CP_CHUNK;if(count>available)count=available;
  memcpy(bytes,source,count);u8 nonzero=0;for(u32 i=0;i<count;i++)nonzero|=bytes[i];position+=count;
  if(!nonzero)zeros+=count;
  else{
   if(zeros){IzCheckpointRecord zero={.logical=zeros};rc=cp_write_record(fs,file,&h,&writer,&zero,0);zeros=0;}
   if(!rc){IzCheckpointRecord record={.logical=count,.stored=count,.crc=iz_crc(bytes,count)};rc=cp_write_record(fs,file,&h,&writer,&record,bytes);}
  }
  c->checkpoint_stored=h.bytes;c->checkpoint_progress=position;
  /* No FRAME or graphics submission can race this capture. Audio may only
   * read its immutable PCM region; its handles are recreated on restoration. */
  if(!rc)CALL(c,F_SLEEP,0,0,0,0,0,0,0,0);
 }
 if(!rc&&zeros){IzCheckpointRecord zero={.logical=zeros};rc=cp_write_record(fs,file,&h,&writer,&zero,0);}
 if(!rc)rc=cp_flush(fs,file,&writer);
 if(!rc)rc=iz_fs_checkpoint_resize(fs,file,h.bytes);
 if(!rc){h.header_crc=cp_header_crc(&h);rc=iz_fs_checkpoint_write(fs,file,0,&h,sizeof h);}
 if(!rc){rc=iz_fs_checkpoint_finish(fs,file);file=0;}
 if(file&&!c->poisoned){u32 close=iz_fs_close(fs,file);if(!rc)rc=close;}
 if(!rc){IzCheckpointHeader verified;rc=cp_read(fs,app,s->kind,path,&verified);if(!rc&&memcmp(&h,&verified,sizeof h))rc=CP_INVALID;}
 c->checkpoint_error=rc;c->checkpoint_state=rc?CP_SKIPPED:CP_SAVED;c->checkpoint_stored=h.bytes;
 if(!rc){s->bank=bank;s->generation=h.generation;c->checkpoint_bank=bank;c->checkpoint_generation=h.generation;}return rc;
}
#endif
