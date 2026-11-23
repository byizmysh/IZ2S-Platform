/* Current local function catalog discovery. No driver function is called.
 * QueryMemory proves the complete resident RX code extent before any byte
 * read. All seven independent current-provider signatures and a second exact
 * mapping identity check must succeed before returning the known GIPA entry.
 * MOD0 dynamic metadata is not assumed to fit inside the code extent. */
#include "module-discovery.h"
#include "startup-catalog-cuts.h"
#include <string.h>
#define LIMIT (UINT64_C(1)<<39)
#define CODE_BYTES UINT64_C(45989888)
static const unsigned char query_cut[24]={0xe1,0x0f,0x1f,0xf8,0xc1,0x00,0x00,0xd4,0xe2,0x03,0x40,0xf9,0x41,0x00,0x00,0xb9,0xff,0x43,0x00,0x91,0xc0,0x03,0x5f,0xd6};
static uint32_t u32(const unsigned char*p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t failed(R586Discovery*s,uint32_t rc){s->done=1;s->error=rc;s->gipa=(R432Function){0};return rc;}
static uint32_t query(R586Discovery*s,uint64_t at,R586MemoryInfo*out){
 *out=(R586MemoryInfo){0};uint32_t page=0,rc=s->query(out,&page,at);
 return !rc&&out->bytes&&!(out->base&4095)&&!(out->bytes&4095)&&out->base<LIMIT&&out->bytes<=LIMIT-out->base&&out->base<=at&&at-out->base<out->bytes?R586_OK:R586_QUERY_FAILED;
}
uint32_t r586_discovery_begin(R586Discovery*s,uint64_t sdk,uint64_t bytes,uint64_t pid){
 if(!s||!sdk||(sdk&4095)||bytes!=6164480||sdk>=LIMIT||bytes>LIMIT-sdk||!pid||memcmp((const void*)(uintptr_t)(sdk+0x158314),query_cut,sizeof query_cut))return R586_BAD_INPUT;
 memset(s,0,sizeof *s);s->sdk=sdk;s->sdk_bytes=bytes;s->process_id=pid;s->query=(R586QueryMemory)(uintptr_t)(sdk+0x158314);return R586_OK;
}
uint32_t r586_discovery_step(R586Discovery*s){
 if(!s||!s->query||s->sdk_bytes!=6164480||!s->process_id)return R586_BAD_INPUT;
 if(s->done)return s->error?s->error:s->gipa.address?R586_OK:R586_NOT_FOUND;
 if(s->cursor>=LIMIT)return failed(s,R586_NOT_FOUND);
 if(s->regions++==R586_MAX_REGIONS)return failed(s,R586_BUDGET);
 R586MemoryInfo r;uint32_t rc=query(s,s->cursor,&r);if(rc)return failed(s,rc);s->cursor=r.base+r.bytes;
 if(!r.base||r.bytes!=CODE_BYTES||r.type!=3||r.permissions!=5||r.attributes||r.ipc_references||r.device_references||r.padding)return R586_PENDING;
 const unsigned char*base=(const void*)(uintptr_t)r.base;
 uint32_t off=u32(base+4);if(!off||off>r.bytes-4||u32(base+off)!=0x30444f4d)return R586_PENDING;
 if(s->modules++==R586_MAX_MODULES)return failed(s,R586_BUDGET);
 for(unsigned k=0;k<7;k++){
  const RnoVkCut*c=rno_vk_cuts+k;
  if(c->offset>r.bytes-32||memcmp(base+c->offset,c->bytes,32))return R586_PENDING;
 }
 if(R432_GIPA_OFFSET>r.bytes-R432_GIPA_BYTES)return failed(s,R586_BAD_INPUT);
 R586MemoryInfo again;rc=query(s,r.base,&again);if(rc||memcmp(&again,&r,sizeof r))return failed(s,R586_QUERY_FAILED);
 s->captured[0]=r;s->extents[0]=(R432Extent){r.base,r.bytes,base,5};s->extent_count=1;
 s->gipa=(R432Function){r.base+R432_GIPA_OFFSET,R432_GIPA_BYTES,1,0};
 s->module_base=r.base;s->module_bytes=r.bytes;s->reserved=1; /* Current-catalog route, not parser metrics. */
 s->done=1;return R586_OK;
}
