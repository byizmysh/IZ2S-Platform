#include "native-app.h"
typedef struct {uint32_t seed,frames,result,read_bytes;uint8_t file_magic[8];uint32_t initialized,stopped;} Data;
__attribute__((section(".text.iz_app"))) uint32_t iz_app(IzNativeApp*app,uint32_t event){
 if(!app||app->magic!=0x414e5a49||app->version!=1||app->bytes!=sizeof *app||app->data_bytes<sizeof(Data))return 1;
 Data*d=app->data;uint32_t*words=app->controls;
 if(event==IZ_NATIVE_INIT){d->frames=0;d->result=d->seed^0x9e3779b9u;d->initialized=1;d->stopped=0;uint32_t rc=app->read_file(app,"launcher.izapp",0,d->file_magic,8,&d->read_bytes);if(rc||d->read_bytes!=8)return rc?rc:2;const uint8_t expected[]={73,90,83,68,65,80,80,49};for(unsigned i=0;i<8;i++)if(d->file_magic[i]!=expected[i])return 3;return 0;}
 if(event==IZ_NATIVE_FRAME){
  if(!d->initialized)return 4;d->frames++;for(unsigned i=0;i<257;i++)d->result=(d->result^((d->result<<13)|(d->result>>19)))*1664525u+1013904223u;
  words[196]=d->frames;words[197]=d->result;words[198]=d->seed;words[199]=0x53444350;
  if(d->frames>=120||(app->buttons(app)&2))app->request_exit(app);return 0;
 }
 if(event==IZ_NATIVE_STOP){d->stopped=1;return app->write_file(app,"native-receipt.bin",d,sizeof *d);}
 return 5;
}
