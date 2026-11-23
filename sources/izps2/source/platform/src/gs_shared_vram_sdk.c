/* Verified SDK leaf syscalls keep Horizon results observable. In particular,
 * do not use the higher SDK Create helper, which aborts on unexpected errors. */
#include "gs_shared_vram.h"
#include "gs_shared_vram_sdk_bytes.h"
#include "hbxl.h"
#include "hbx_sdk.h"
#include <string.h>
#include <stdio.h>
uint32_t ps2_gs_mirror_initialize_sdk(uintptr_t sdk,size_t bytes){
    if(ps2_gs_mirror_stats.configured)return 0;
    if(!sdk||(sdk&4095)||bytes!=6164480||sdk>=(1ull<<39)-bytes)return PS2_GS_MIRROR_SDK;
    for(unsigned i=0;i<6;i++)if(memcmp((void*)(sdk+ps2_gs_sdk_cuts[i].offset),ps2_gs_sdk_cuts[i].bytes,ps2_gs_sdk_cuts[i].count))return PS2_GS_MIRROR_SDK;
    Ps2GsMirrorApi api={
        (uint32_t(*)(uint32_t*,uint64_t,uint32_t,uint32_t))(sdk+0x158554),
        (uint32_t(*)(uint32_t,uintptr_t,uint64_t,uint32_t))(sdk+0x1583c4),
        (uint32_t(*)(uint32_t,uintptr_t,uint64_t))(sdk+0x1583cc),
        (uint32_t(*)(uint32_t))(sdk+0x1583ec),
        (uint32_t(*)(Ps2GsMemoryInfo*,uint32_t*,uintptr_t))(sdk+0x158314),
        (uint32_t(*)(uint64_t*,uint32_t,uint32_t,uint64_t))(sdk+0x1584ac)};
    return ps2_gs_mirror_bind(&api);
}
void *__wrap__Z23GSAllocateWrappedMemorymm(size_t size,size_t repeat){
    if(!ps2_gs_mirror_stats.configured){
        if(!hbxl_ctx||hbxl_ctx->magic!=HBX_ABI_MAGIC||hbxl_ctx->abi_version!=3||hbxl_ctx->size<sizeof(*hbxl_ctx)||hbxl_ctx->audio.initialize_parameter<0x1c7f30)return 0;
        if(ps2_gs_mirror_initialize_sdk(hbxl_ctx->audio.initialize_parameter-0x1c7f30,6164480))return 0;
    }
    void*p=ps2_gs_mirror_allocate(size,repeat);
    fprintf(stderr,"PS2_GS_SHARED_VRAM allocate size=%zu repeat=%zu ptr=%p error=%x poisoned=%u\n",size,repeat,p,ps2_gs_mirror_stats.last_error,ps2_gs_mirror_stats.poisoned);
    return p;
}
extern void ps2_hbxl_vm_fatal(const char*,unsigned) __attribute__((noreturn));
void __wrap__Z19GSFreeWrappedMemoryPvmm(void*p,size_t size,size_t repeat){
    if(ps2_gs_mirror_release(p,size,repeat))ps2_hbxl_vm_fatal("GS shared VRAM mappings retained",__LINE__);
    fprintf(stderr,"PS2_GS_SHARED_VRAM released ptr=%p size=%zu repeat=%zu\n",p,size,repeat);
}
