#include "application.h"
__attribute__((section(".text.iz_app")))
uint32_t iz_app(IzNativeApp*app,uint32_t event){return iz_ps2_application_event(app,event);}
