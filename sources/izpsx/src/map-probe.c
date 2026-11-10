/* The same qualified one-page RO protocol as ro-jit.c, without host round trips.
 * The helper never changes or frees the RO endpoint, source or destination.
 * A failed/untrusted reply leaves unknown=1; the owner must retain everything.
 */
#include "abi.h"
#include "image-relocation.h"
typedef struct {
 HeavyContext base;
 uint32_t service,operation;
 uint64_t send,pid,ro_pid,source,destination,pages,mapped,unknown;
 uint64_t operation_result,transport,begin,end,failed_page,completed,reserved;
 uint32_t code_file,data_file;
 uint64_t data_destination,data_region_bytes,code_bytes,data_bytes,file_reads,file_bytes,zero_bytes;
 uint64_t code_size,data_size,prelude_complete,prelude_error;
 uint32_t relocation_file,reserved2;
 uint64_t relocation_buffer,relocation_capacity,relocation_bytes,relocation_size,relocation_result,relocation_applied;
 uint64_t canonical_code,canonical_data,canonical_region;
} MapProbe;
_Static_assert(sizeof(MapProbe)==560,"map helper ABI");
static void clear(void*p,size_t n){volatile uint8_t*b=p;while(n--)*b++=0;}
static uint32_t request(MapProbe*c,uint32_t command,uint64_t offset,uint8_t*buffer){
 clear(buffer,4096);uint32_t*p=(void*)buffer;
 p[0]=4;p[1]=0x80000000u+16;p[2]=3;p[5]=0xffff8001u;p[8]=0x49434653;p[10]=command;
 uint64_t*in=(void*)(buffer+48);in[0]=c->pid;in[1]=c->source+offset;in[2]=c->destination+offset;in[3]=4096;
 c->operation_result=0;c->transport=((uint32_t(*)(void*,uint64_t,uint32_t))(uintptr_t)c->send)(buffer,4096,c->service);
 if(c->transport)return 1;
 if(!(p[1]&0x80000000u)||p[2]!=1||p[8]!=0x4f434653)return 2;
 if(((uint64_t)p[3]|((uint64_t)p[4]<<32))!=c->ro_pid)return 3;
 c->operation_result=p[10];if(p[10])return 4;return 0;
}
static uint32_t file_request(MapProbe*c,uint32_t handle,uint32_t command,uint64_t destination,uint64_t offset,uint64_t bytes,uint64_t*value,uint8_t*message){
 clear(message,4096);uint32_t*p=(void*)message;unsigned raw=bytes?32:16;
 p[0]=4u|(bytes?1u<<24:0);p[1]=bytes?14:8;
 if(bytes){p[2]=(uint32_t)bytes;p[3]=(uint32_t)destination;p[4]=1u|(uint32_t)(destination>>36)<<2|(uint32_t)((destination>>32)&15)<<28;c->file_reads++;}
 p[raw/4]=0x49434653;p[raw/4+2]=command;
 if(bytes){uint64_t*in=(void*)(message+raw+16);in[0]=0;in[1]=offset;in[2]=bytes;}
 c->operation_result=0;c->transport=((uint32_t(*)(void*,uint64_t,uint32_t))(uintptr_t)c->send)(message,4096,handle);if(c->transport)return 1;
 unsigned at=8;if(p[1]&0x80000000u){uint32_t special=p[2];if(((special>>1)&15)||((special>>5)&15))return 2;at+=4;if(special&1)at+=8;}
 at+=((p[0]>>16)&15)*8+(((p[0]>>20)&15)+((p[0]>>24)&15)+((p[0]>>28)&15))*12;at=(at+15)&~15u;
 if(at>256||*(uint32_t*)(message+at)!=0x4f434653)return 2;
 c->operation_result=*(uint32_t*)(message+at+8);if(c->operation_result)return 3;
 *value=*(uint64_t*)(message+at+16);return 0;
}
static uint32_t read_file(MapProbe*c,uint32_t handle,uint64_t destination,uint64_t bytes,uint8_t*message){
 for(uint64_t at=0;at<bytes;){uint64_t n=bytes-at,value=0;if(n>65536)n=65536;
  uint32_t error=file_request(c,handle,0,destination+at,at,n,&value,message);if(error)return error;if(value!=n)return 5;c->file_bytes+=value;at+=n;
 }return 0;
}
static uint32_t prelude(MapProbe*c,uint8_t*message){
 if(!c->code_file||c->code_file>=0x80000000u||!c->data_file||c->data_file>=0x80000000u||!c->data_destination||(c->data_destination&4095)||
    !c->data_region_bytes||(c->data_region_bytes&4095)||c->data_region_bytes>128*1024*1024||!c->code_bytes||c->code_bytes>c->pages*4096||c->data_bytes>c->data_region_bytes||
    c->source+c->pages*4096>1ull<<39||c->data_destination+c->data_region_bytes<c->data_destination||c->data_destination+c->data_region_bytes>1ull<<39||
    (c->source<c->data_destination+c->data_region_bytes&&c->data_destination<c->source+c->pages*4096))return 6;
 uint32_t error=file_request(c,c->code_file,4,0,0,0,&c->code_size,message);if(error)return error;
 error=file_request(c,c->data_file,4,0,0,0,&c->data_size,message);if(error)return error;
 if(c->code_size!=c->code_bytes||c->data_size!=c->data_bytes)return 4;
 if(c->relocation_file){
  if(c->relocation_file>=0x80000000u||!c->relocation_buffer||(c->relocation_buffer&4095)||c->relocation_bytes<64||c->relocation_bytes>1024*1024||c->relocation_bytes>c->relocation_capacity||c->relocation_capacity>1024*1024||
     c->relocation_buffer+c->relocation_capacity<c->relocation_buffer||c->relocation_buffer+c->relocation_capacity>1ull<<39||c->canonical_region>c->data_region_bytes||
     (c->source<c->relocation_buffer+c->relocation_capacity&&c->relocation_buffer<c->source+c->pages*4096)||
     (c->data_destination<c->relocation_buffer+c->relocation_capacity&&c->relocation_buffer<c->data_destination+c->data_region_bytes))return 6;
  error=file_request(c,c->relocation_file,4,0,0,0,&c->relocation_size,message);if(error)return error;if(c->relocation_size!=c->relocation_bytes)return 4;
 }
 clear((void*)(uintptr_t)c->source,c->pages*4096);clear((void*)(uintptr_t)c->data_destination,c->data_region_bytes);c->zero_bytes=c->pages*4096+c->data_region_bytes;
 error=read_file(c,c->code_file,c->source,c->code_bytes,message);if(error)return error;
 error=read_file(c,c->data_file,c->data_destination,c->data_bytes,message);if(error)return error;
 if(c->relocation_file){
  error=read_file(c,c->relocation_file,c->relocation_buffer,c->relocation_bytes,message);if(error)return error;
  c->relocation_result=ps1_relocate_image((void*)(uintptr_t)c->source,c->code_bytes,(void*)(uintptr_t)c->data_destination,c->data_bytes,(void*)(uintptr_t)c->relocation_buffer,c->relocation_bytes,c->destination,c->data_destination,c->canonical_code,c->canonical_data,c->canonical_region);
  if(c->relocation_result)return 7;c->relocation_applied=1;
 }
 c->prelude_complete=1;return 0;
}
uint32_t heavy_main(HeavyContext*base){
 MapProbe*c=(void*)base;
 if(!c||base->magic!=HEAVY_MAGIC||base->version!=1||!c->service||!c->send||!c->pid||!c->ro_pid||
    !c->source||!c->destination||((c->source|c->destination)&4095)||!c->pages||c->pages>4096||
    c->mapped>c->pages||c->unknown||(c->operation!=1&&c->operation!=2)||(c->operation==1&&c->mapped)||
    c->source+c->pages*4096<c->source||c->destination+c->pages*4096<c->destination||
    (c->source<c->destination+c->pages*4096&&c->destination<c->source+c->pages*4096))return 1;
 _Alignas(4096) uint8_t message[4096];base->status=1;base->error=0;base->phase=c->operation;
 c->completed=0;c->failed_page=~0ull;if(base->tick)c->begin=((uint64_t(*)(void))(uintptr_t)base->tick)();
 if(c->operation==1&&(c->code_file||c->data_file)){
  c->prelude_complete=c->prelude_error=c->file_reads=c->file_bytes=c->zero_bytes=c->code_size=c->data_size=0;
  c->relocation_size=c->relocation_result=c->relocation_applied=0;
  c->prelude_error=prelude(c,message);if(c->prelude_error)base->error=100+(uint32_t)c->prelude_error;
 }
 if(!base->error)
 while(c->operation==1?c->mapped<c->pages:c->mapped>0){
  uint64_t page=c->operation==1?c->mapped:c->mapped-1;c->unknown=1;
  uint32_t error=request(c,c->operation==1?0x524f0012:0x524f0013,page*4096,message);
  if(error){
   /* Only the explicitly reported map failure/permission rollback proves that
    * the attempted page is absent. Validation failure and unmap failure remain
    * conservative: nothing gets released based on a guessed outcome. */
   if(error==4&&c->operation==1){uint64_t*out=(void*)(message+48);if(out[3]||(out[4]&&!out[5]))c->unknown=0;}
   c->failed_page=page;base->error=10+error;break;
  }
  if(c->operation==1)c->mapped++;else c->mapped--;c->unknown=0;c->completed++;
 }
 if(base->tick)c->end=((uint64_t(*)(void))(uintptr_t)base->tick)();
 base->status=base->error?3:2;base->phase=3;return base->error;
}
