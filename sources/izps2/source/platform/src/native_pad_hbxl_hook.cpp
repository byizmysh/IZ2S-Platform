#include "native_pad_hbxl_hook.h"
#include "native_pad_frontend.h"
#include "hbxl_vm_frontend.h"
#include "hbxl.h"
#include "hbx_sdk.h"
#include "SIO/Pad/Pad.h"
#include <pthread.h>
#include <cstring>
static_assert(sizeof(Ps2HbxlPadConfig)==48);
namespace {
uint32_t busy;Ps2HbxlPadStatistics state{};Ps2HbxlPadConfig saved{};const Ps2HbxlPadConfig *description;
bool lock(){uint32_t zero=0;return __atomic_compare_exchange_n(&busy,&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);}
uint32_t done(uint32_t e){if(e)state.error=e;__atomic_store_n(&busy,0,__ATOMIC_RELEASE);return e;}
uint32_t check_owner(){return state.owner_thread==(uintptr_t)pthread_self()?0:PS2_PAD_OWNER;}
uint32_t check_config(){
    if(!description||std::memcmp(description,&saved,sizeof saved))return PS2_PAD_INVALID;
    return 0;
}
void failed(uint32_t error){if(error)ps2_hbxl_vm_fatal("native PAD CPU-boundary or lifetime admission failed",__LINE__);}
}
extern "C" int ps2_hbxl_pad_validate_configuration(const Ps2HbxlPadConfig *c){
    if(!c||c->magic!=PS2_HBXL_PAD_MAGIC||c->version!=1||c->size!=sizeof *c||c->flags>1)return 0;
    if(!c->flags)return !c->sdk_rx&&!c->sdk_bytes&&!c->lease_token[0]&&!c->lease_token[1];
    return c->sdk_rx&&!(c->sdk_rx&4095)&&c->sdk_bytes==6164480&&c->sdk_rx<=((1ull<<39)-c->sdk_bytes)&&c->lease_token[0]&&c->lease_token[1];
}
extern "C" uint32_t ps2_hbxl_pad_initialize(){
    if(!lock())return PS2_PAD_BUSY;if(state.phase!=PS2_HBXL_PAD_NEW)return done(PS2_PAD_BUSY);
    if(!hbxl_ctx||hbxl_ctx->magic!=HBX_ABI_MAGIC||hbxl_ctx->abi_version!=3||hbxl_ctx->size<sizeof *hbxl_ctx)return done(PS2_PAD_INVALID);
    state.owner_thread=(uintptr_t)pthread_self();const uintptr_t p=hbxl_ctx->args[5];
    if(!p){state.phase=PS2_HBXL_PAD_DISABLED;return done(0);}
    if((p&7)||p>(1ull<<39)-sizeof(Ps2HbxlPadConfig))return done(PS2_PAD_INVALID);
    const auto *c=(const Ps2HbxlPadConfig*)p;if(!ps2_hbxl_pad_validate_configuration(c))return done(PS2_PAD_INVALID);
    saved=*c;description=c;if(!saved.flags){state.phase=PS2_HBXL_PAD_DISABLED;return done(0);}
    if(hbxl_ctx->audio.initialize_parameter<0x1c7f30||saved.sdk_rx!=hbxl_ctx->audio.initialize_parameter-0x1c7f30)return done(PS2_PAD_INVALID);
    const uint32_t e=ps2_native_pad_initialize_sdk(saved.sdk_rx,saved.sdk_bytes);if(e)return done(e);
    Ps2NativePadStatistics source{};const uint32_t result=ps2_native_pad_statistics(&source);if(result)return done(result);
    state.sdk_verified=source.sdk_verified;state.enabled=1;state.phase=PS2_HBXL_PAD_ACTIVE;return done(0);
}
extern "C" uint32_t ps2_hbxl_pad_statistics(Ps2HbxlPadStatistics *out){
    if(!out)return PS2_PAD_INVALID;if(!lock())return PS2_PAD_BUSY;*out=state;__atomic_store_n(&busy,0,__ATOMIC_RELEASE);return 0;
}
extern "C" bool __real__ZN3Pad10InitializeEv();
extern "C" bool __wrap__ZN3Pad10InitializeEv(){
    if(!__real__ZN3Pad10InitializeEv())return false;
    const uint32_t result=ps2_hbxl_pad_initialize();if(result)return false;
    if(!lock())return false;state.pad_initializations++;done(0);return true;
}
extern "C" void __real__ZN12InputManager11PollSourcesEv();
extern "C" void __wrap__ZN12InputManager11PollSourcesEv(){
    __real__ZN12InputManager11PollSourcesEv();
    if(!lock()){failed(PS2_PAD_BUSY);return;}state.original_polls++;
    if(state.phase==PS2_HBXL_PAD_NEW){state.pre_init_skips++;done(0);return;}
    if(state.phase==PS2_HBXL_PAD_STOPPED){state.post_shutdown_skips++;done(0);return;}
    if(state.phase==PS2_HBXL_PAD_DISABLED){done(0);return;}
    uint32_t e=check_owner();if(!e)e=check_config();
    if(!e&&!Pad::GetPad(0)){state.pre_init_skips++;done(0);return;}
    if(!e)e=ps2_native_pad_poll();if(!e)state.applied_polls++;done(e);failed(e);
}
extern "C" void __real__ZN3Pad8ShutdownEv();
extern "C" void __wrap__ZN3Pad8ShutdownEv(){
    if(!lock()){failed(PS2_PAD_BUSY);return;}
    uint32_t e=0;
    if(state.phase==PS2_HBXL_PAD_ACTIVE){e=check_owner();if(!e)e=check_config();if(!e)e=ps2_native_pad_shutdown();}
    if(!e){state.phase=PS2_HBXL_PAD_STOPPED;state.pad_shutdowns++;}done(e);failed(e);
    // Only after the actual input source has released all held controls and
    // is disabled may original PAD delete its controller/protocol objects.
    __real__ZN3Pad8ShutdownEv();
}
