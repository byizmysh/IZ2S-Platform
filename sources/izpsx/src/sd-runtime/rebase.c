#include "image.h"
static uint32_t fail(IzRelocState*s,uint32_t code){s->failed=code;return code;}
static int address(uint64_t base,uint32_t bytes){return base&&!(base&4095)&&base<(1ull<<39)&&bytes&&(uint64_t)bytes<=(1ull<<39)-base;}
static int overlap(uint64_t a,uint32_t n,uint64_t b,uint32_t m){return a<b+m&&b<a+n;}
static uint64_t read_word(const uint8_t*p,unsigned bytes){uint64_t v=0;for(unsigned i=0;i<bytes;i++)v|=(uint64_t)p[i]<<(i*8);return v;}
static void write_word(uint8_t*p,unsigned bytes,uint64_t value){for(unsigned i=0;i<bytes;i++)p[i]=(uint8_t)(value>>(i*8));}
uint32_t iz_reloc_begin(IzRelocState*s,const IzRelocHeader*h,uint64_t bytes,void*segments[2],const uint64_t bases[2]){
 if(!s)return 0xe601;
 s->header=h;s->processed=s->last_segment=s->last_end=s->failed=0;
 if(!h||!segments||!bases)return fail(s,0xe601);
 const char magic[]="IZNATRL1";for(unsigned i=0;i<8;i++)if(h->magic[i]!=magic[i])return fail(s,0xe601);
 if(h->version!=1||h->header_bytes!=256||h->total_bytes!=bytes||bytes>80u*1024*1024||h->flags||h->record_bytes!=32||h->record_count>1048576)return fail(s,0xe601);
 for(unsigned i=0;i<sizeof h->padding;i++)if(h->padding[i])return fail(s,0xe601);
 for(unsigned i=0;i<2;i++){
  if(!segments[i]||!address(h->source_bases[i],h->memory_bytes[i])||!address(bases[i],h->memory_bytes[i])||h->memory_bytes[i]>128u*1024*1024||h->memory_bytes[i]%4096||h->file_bytes[i]>h->memory_bytes[i])return fail(s,0xe602);
  s->segments[i]=segments[i];s->bases[i]=bases[i];
 }
 if(!h->file_bytes[0]||h->entry_offset>=h->file_bytes[0]||h->spawn_offset>=h->file_bytes[0]||(h->entry_offset|h->spawn_offset)&3)return fail(s,0xe602);
 if(overlap(h->source_bases[0],h->memory_bytes[0],h->source_bases[1],h->memory_bytes[1])||overlap(bases[0],h->memory_bytes[0],bases[1],h->memory_bytes[1]))return fail(s,0xe602);
 uint64_t end=256;for(unsigned i=0;i<3;i++){uint64_t n=i<2?h->file_bytes[i]:(uint64_t)h->record_count*32;if(h->offsets[i]!=((end+7u)&~7ull)||h->offsets[i]>bytes||n>bytes-h->offsets[i])return fail(s,0xe603);end=h->offsets[i]+n;}
 if(end!=bytes)return fail(s,0xe603);
 return 0;
}
uint32_t iz_reloc_batch(IzRelocState*s,const IzRelocRecord*rows,uint32_t count){
 if(!s||!s->header)return 0xe601;
 if(s->failed)return s->failed;
 const IzRelocHeader*h=s->header;if((!rows&&count)||count>IZ_RELOC_CHUNK_RECORDS||s->processed>h->record_count||count>h->record_count-s->processed)return fail(s,0xe604);
 for(uint32_t i=0;i<count;i++){
  const IzRelocRecord*r=rows+i;unsigned width=r->kind==IZ_RELOC_ABS64||r->kind==IZ_RELOC_PREL64?8:4;
  if(r->segment>1||r->target_segment>1||r->reserved||r->kind<IZ_RELOC_ABS64||r->kind>IZ_RELOC_MOVW||r->offset>h->file_bytes[r->segment]||width>h->file_bytes[r->segment]-r->offset||r->target_offset>h->memory_bytes[r->target_segment])return fail(s,0xe604);
  if(s->processed&&(r->segment<s->last_segment||(r->segment==s->last_segment&&r->offset<s->last_end)))return fail(s,0xe604);
  uint8_t*place=s->segments[r->segment]+r->offset;uint64_t old=read_word(place,width);if(old!=r->expected)return fail(s,0xe605);
  uint64_t target=s->bases[r->target_segment]+r->target_offset,value=target;int64_t delta=(int64_t)target-(int64_t)(s->bases[r->segment]+r->offset);
  if(r->kind==IZ_RELOC_PREL32){if(delta<INT32_MIN||delta>INT32_MAX)return fail(s,0xe606);value=(uint32_t)delta;}
  else if(r->kind==IZ_RELOC_PREL64)value=(uint64_t)delta;
  else if(r->kind==IZ_RELOC_ADRP){
   if(r->offset%4||(old&0x9f000000u)!=0x90000000u)return fail(s,0xe607);
   int64_t pages=((int64_t)(target&~4095ull)-(int64_t)((s->bases[r->segment]+r->offset)&~4095ull))/4096;
   if(pages<-(1ll<<20)||pages>=(1ll<<20))return fail(s,0xe606);
   uint32_t imm=(uint32_t)pages&0x1fffff;value=((uint32_t)old&~0x60ffffe0u)|((imm&3)<<29)|((imm>>2)<<5);
  }else if(r->kind==IZ_RELOC_MOVW){
   uint32_t op=(uint32_t)old&0xff800000u;if(r->offset%4||(op!=0xd2800000u&&op!=0xf2800000u))return fail(s,0xe607);
   unsigned shift=((uint32_t)old>>21&3)*16;value=((uint32_t)old&~0x1fffe0u)|((uint32_t)(target>>shift)&0xffffu)<<5;
  }
  write_word(place,width,value);s->processed++;s->last_segment=r->segment;s->last_end=r->offset+width;
 }return 0;
}
uint32_t iz_reloc_finish(IzRelocState*s){if(!s||!s->header)return 0xe601;if(s->failed)return s->failed;return s->processed==s->header->record_count?0:fail(s,0xe608);}
