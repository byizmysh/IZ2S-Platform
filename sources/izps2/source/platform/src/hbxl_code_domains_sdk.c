#include "hbxl_code_domains.h"
#include "hbx_sdk.h"
#include "hbxl.h"
#include <string.h>
#include <pthread.h>
const Ps2HbxlCodeDomainsConfig*ps2_hbxl_code_domains_default_config(void){
    if(!hbxl_ctx||hbxl_ctx->magic!=HBX_ABI_MAGIC||hbxl_ctx->abi_version!=3||hbxl_ctx->size<sizeof(*hbxl_ctx)||!hbxl_ctx->args[1]||(hbxl_ctx->args[1]&7)||hbxl_ctx->args[1]>=(1ull<<39))return 0;
    return (const Ps2HbxlCodeDomainsConfig*)(uintptr_t)hbxl_ctx->args[1];
}
static uint64_t current(void){return (uint64_t)(uintptr_t)pthread_self();}
static void sleep_ns(uint64_t n){hbxl_svc_sleep(n);}
uint32_t ps2_hbxl_code_domains_initialize(const Ps2HbxlCodeDomainsConfig*c){
    if(!hbxl_ctx||hbxl_ctx->magic!=HBX_ABI_MAGIC||hbxl_ctx->abi_version!=3||hbxl_ctx->size<sizeof(*hbxl_ctx)||!c)return PS2_HBXL_CODE_INVALID;
    const uint64_t audio=hbxl_ctx->audio.initialize_parameter;
    if(audio<0x1c7f30)return PS2_HBXL_CODE_SDK;
    const uint64_t sdk=audio-0x1c7f30;
    if(!sdk||(sdk&4095)||sdk>=(1ull<<39)-0x5e1000||c->base.send!=sdk+0x15845c)return PS2_HBXL_CODE_SDK;
    static const unsigned char q[]={0xe1,0x0f,0x1f,0xf8,0xc1,0x00,0x00,0xd4,0xe2,0x03,0x40,0xf9,0x41,0x00,0x00,0xb9,0xff,0x43,0x00,0x91,0xc0,0x03,0x5f,0xd6};
    static const unsigned char s[]={0x41,0x04,0x00,0xd4,0xc0,0x03,0x5f,0xd6};
    if(memcmp((void*)(uintptr_t)(sdk+0x158314),q,sizeof(q))||memcmp((void*)(uintptr_t)(sdk+0x15845c),s,sizeof(s)))return PS2_HBXL_CODE_SDK;
    const Ps2HbxlCodeDomainsApi api={{(uint32_t(*)(Ps2HbxlMemoryInfo*,uint32_t*,uintptr_t))(uintptr_t)(sdk+0x158314),(uint32_t(*)(void*,uint64_t,uint32_t))(uintptr_t)(sdk+0x15845c),current,ps2_ro_clean,ps2_ro_invalidate},sleep_ns};
    return ps2_hbxl_code_domains_bind(c,&api);
}
