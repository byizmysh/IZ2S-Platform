#include "native-app.h"
__attribute__((section(".text.iz_app")))
uint32_t iz_app(IzNativeApp*app,uint32_t event){
 if(!app||app->magic!=0x414e5a49||app->bytes<sizeof *app)return 0xe420;
 if(event==IZ_NATIVE_INIT){iz_native_request_connection(app);return 0;}
 if(event==IZ_NATIVE_FRAME&&app->frames>=60)app->request_exit(app);
 return 0;
}
