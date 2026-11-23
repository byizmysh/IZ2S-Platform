#include "hbxl_vm_frontend.h"
#include "VMManager.h"
#include <cstdio>
extern "C" void __wrap__Z14pxOnAssertFailPKciS0_S0_(const char *file,int line,const char *function,const char *message){
    std::fprintf(stderr,"PS2_HBXL_VM_ASSERT file=%s line=%d function=%s message=%s\n",file?file:"(null)",line,function?function:"(null)",message?message:"(null)");
    ps2_hbxl_vm_fatal("Original core assertion; VM/code ownership retained",line);
}
extern "C" void __real__ZN9VMManager8SetStateE7VMState(VMState);
extern "C" void __wrap__ZN9VMManager8SetStateE7VMState(VMState state){
    __real__ZN9VMManager8SetStateE7VMState(state);
    if(state==VMState::Running)ps2_hbxl_vm_stage("vm-running");
}
