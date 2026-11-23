#include "hbxl_vm_frontend.h"
#include "hbxl.h"
#include "hbx_sdk.h"
#include "hbx_vk.h"
#include "hbxl_vm_av.h"
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(Ps2HbxlVmConfig)==1312,"VM config ABI");
static int string(const char *p,int absolute){
    size_t n=strnlen(p,256);if(!n || n==256 || (absolute && p[0]!='/'))return 0;
    if(strstr(p,"..") || strchr(p,'\\') || strchr(p,':'))return 0;
    for(size_t i=0;i<n;i++)if((unsigned char)p[i]<32 || (unsigned char)p[i]==127)return 0;
    return 1;
}
static int empty_path(const char p[256]){
    for(size_t i=0;i<256;i++)if(p[i])return 0;
    return 1;
}
int ps2_hbxl_vm_validate_configuration(const Ps2HbxlVmConfig *c){
    if(!c || c->magic!=PS2_HBXL_VM_MAGIC || c->version!=1 || c->size!=sizeof *c ||
       (c->flags!=PS2_HBXL_VM_FLAG_FULL_AV &&
        c->flags!=(PS2_HBXL_VM_FLAG_FULL_AV|PS2_HBXL_VM_FLAG_BIOS_ONLY)) ||
       c->data_bytes!=PS2_HBXL_VM_DATA_BYTES || c->code_bytes!=PS2_HBXL_VM_CODE_BYTES ||
       !string(c->scratch,1) || !string(c->resources,1) ||
       !string(c->bios_filename,0) || strchr(c->bios_filename,'/'))return 0;
    return (c->flags&PS2_HBXL_VM_FLAG_BIOS_ONLY)?
        (empty_path(c->iso) && empty_path(c->anchor)):
        (string(c->iso,1) && (!c->anchor[0] || string(c->anchor,1)));
}
/* Only the memory device is borrowed here. Presentation is still admitted
 * through the separate AV lease; the generic HBXL renderer owns no objects. */
static int pointer(uint64_t p,size_t n){return p && !(p&7) && p<=(1ull<<39)-n;}
static int memory_gpu_valid(void){
    const struct hbx_gpu *g=&hbxl_ctx->gpu;
    const unsigned char *bytes=(const unsigned char *)g;
    for(size_t i=24;i<sizeof *g;i++)
        if((i<offsetof(struct hbx_gpu,dispatch) || i>=offsetof(struct hbx_gpu,dispatch)+8) && bytes[i])return 0;
    if(!g->instance && !g->physical && !g->device)return 1;
    if(!pointer(g->instance,8) || !pointer(g->physical,8) || !pointer(g->device,8) ||
       !pointer(g->dispatch,sizeof(struct hbx_vk_dispatch)) ||
       !pointer(hbxl_ctx->args[2],sizeof(Ps2HbxlVmAvConfig)))return 0;
    const Ps2HbxlVmAvConfig *av=(const void *)(uintptr_t)hbxl_ctx->args[2];
    if(av->magic!=PS2_HBXL_VM_AV_MAGIC || av->version!=1 || av->size!=sizeof *av || av->flags!=1 ||
       av->video.magic!=PS2_VIDEO_LEASE_MAGIC || av->video.version!=1 ||
       av->video.bytes!=sizeof av->video || av->video.flags!=PS2_VIDEO_LEASE_FLAGS ||
       av->video.device!=g->device)return 0;
    const struct hbx_vk_dispatch *d=(const void *)(uintptr_t)g->dispatch;
    return d->vkGetInstanceProcAddr && d->vkEnumeratePhysicalDevices &&
        d->vkGetPhysicalDeviceMemoryProperties && d->vkAllocateMemory &&
        d->vkFreeMemory && d->vkMapMemory && d->vkUnmapMemory;
}
const Ps2HbxlVmConfig *ps2_hbxl_vm_configuration(int argc,char **argv){
    (void)argc;(void)argv;
    if(!hbxl_ctx || hbxl_ctx->magic!=HBX_ABI_MAGIC || hbxl_ctx->abi_version!=3 || hbxl_ctx->size<sizeof *hbxl_ctx ||
       hbxl_ctx->flags!=0 || !memory_gpu_valid() || !hbxl_ctx->args[0] || (hbxl_ctx->args[0]&7) || hbxl_ctx->args[0]>(1ull<<39)-sizeof(Ps2HbxlVmConfig))return 0;
    const Ps2HbxlVmConfig *c=(const void *)(uintptr_t)hbxl_ctx->args[0];return ps2_hbxl_vm_validate_configuration(c)?c:0;
}
void ps2_hbxl_vm_stage(const char *name){fprintf(stderr,"PS2_HBXL_VM_STAGE %s\n",name);fflush(stderr);}
__attribute__((noreturn)) void ps2_hbxl_vm_fatal(const char *message,unsigned line){
    fprintf(stderr,"PS2_HBXL_VM_FATAL line=%u %s; native ownership retained\n",line,message);fflush(stderr);
    /* Do not return through HBXL exit/destructors with unproved live VM/code. */
    for(;;)hbxl_svc_sleep(10000000);
}
