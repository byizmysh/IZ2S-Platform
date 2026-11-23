#include <sb4.h>
const Sb4Profile *sb4GetProfile(void){
    static const Sb4Profile p={SB4_SDK_VERSION,sizeof(Sb4Profile),46,SB4_AUDIO_FUNCTIONS,28054,27657,0,"libounce"};
    return &p;
}
IzServiceFunction sb4ResolveService(uint32_t module,uint32_t offset){
    return module==5?iz_service_target(sb4GetServiceBridge(),offset):NULL;
}
