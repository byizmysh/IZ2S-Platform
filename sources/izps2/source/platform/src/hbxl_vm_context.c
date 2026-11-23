#include "hbxl_vm_frontend.h"
#include "hbxl.h"
#include "hbx_sdk.h"
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(Ps2HbxlVmConfig)==1312,"VM config ABI");
static int string(const char *p,int absolute){
    size_t n=strnlen(p,256);if(!n || n==256 || (absolute && p[0]!='/'))return 0;
    if(strstr(p,"..") || strchr(p,'\\') || strchr(p,':'))return 0;
    for(size_t i=0;i<n;i++)if((unsigned char)p[i]<32 || (unsigned char)p[i]==127)return 0;
    return 1;
}
int ps2_hbxl_vm_validate_configuration(const Ps2HbxlVmConfig *c){
    return c && c->magic==PS2_HBXL_VM_MAGIC && c->version==1 && c->size==sizeof *c && c->flags==PS2_HBXL_VM_NO_AV &&
        c->data_bytes==PS2_HBXL_VM_DATA_BYTES && c->code_bytes==PS2_HBXL_VM_CODE_BYTES &&
        string(c->scratch,1) && string(c->resources,1) && string(c->bios_filename,0) && !strchr(c->bios_filename,'/') &&
        string(c->iso,1) && string(c->anchor,1);
}
const Ps2HbxlVmConfig *ps2_hbxl_vm_configuration(int argc,char **argv){
    (void)argc;(void)argv;
    if(!hbxl_ctx || hbxl_ctx->magic!=HBX_ABI_MAGIC || hbxl_ctx->abi_version!=3 || hbxl_ctx->size<sizeof *hbxl_ctx ||
       hbxl_ctx->flags!=4 || !hbxl_ctx->args[0] || (hbxl_ctx->args[0]&7) || hbxl_ctx->args[0]>(1ull<<39)-sizeof(Ps2HbxlVmConfig))return 0;
    const Ps2HbxlVmConfig *c=(const void *)(uintptr_t)hbxl_ctx->args[0];return ps2_hbxl_vm_validate_configuration(c)?c:0;
}
void ps2_hbxl_vm_stage(const char *name){fprintf(stderr,"PS2_HBXL_VM_STAGE %s\n",name);fflush(stderr);}
__attribute__((noreturn)) void ps2_hbxl_vm_fatal(const char *message,unsigned line){
    fprintf(stderr,"PS2_HBXL_VM_FATAL line=%u %s; native ownership retained\n",line,message);fflush(stderr);
    /* Do not return through HBXL exit/destructors with unproved live VM/code. */
    for(;;)hbxl_svc_sleep(10000000);
}
