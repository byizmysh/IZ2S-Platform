/* Execute the production binder with an opaque, deliberately unreadable owner.
 * ABI suffixes vary; only the public environment prefix can supply services. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "../src/sd-runtime/engine.c"
uintptr_t ps1_imports[PS1_IMPORT_COUNT];
int main(void){
 const size_t bytes=6164480;
 void*sdk=mmap((void*)0x18000000,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);assert(sdk!=MAP_FAILED);
 for(unsigned i=0;i<PS1_IMPORT_COUNT;i++)memcpy((char*)sdk+izpsx_imports[i].offset,izpsx_imports[i].bytes,32);
 for(unsigned i=0;i<F_COUNT;i++)memcpy((char*)sdk+izpsx_services[i].offset,izpsx_services[i].bytes,izpsx_services[i].length);
 IzNativeEnvironment env={.version=1,.bytes=sizeof env,.sdk=(uintptr_t)sdk,.sdk_bytes=bytes,.application_pid=23,.ro_pid=24,.ro_handle=25,.filesystem=26,.tick_hz=19200000};
 struct {IzNativeApp6 public;char future[1024];} a;memset(&a,0xa7,sizeof a);
 IzNativeApp*app=&a.public.base.base.base.base.base;
 memset(app,0,sizeof *app);app->magic=0x414e5a49;app->data=calloc(1,IZPSX_APP_DATA_BYTES);app->data_bytes=IZPSX_APP_DATA_BYTES;app->controls=calloc(1,1024);app->controls_bytes=1024;app->tick_hz=env.tick_hz;app->owner=(void*)1;
 a.public.base.environment=&env;
 for(unsigned version=5;version<=7;version++){
  app->version=version;app->bytes=version==5?sizeof(IzNativeApp5):version==6?sizeof(IzNativeApp6):sizeof a;
  State state;assert(bind(app,&state)==0);assert(state.owner==&state.services);assert(state.owner->pid==23&&state.owner->filesystem==26);assert(state.owner->functions[F_SEND]==env.sdk+izpsx_services[F_SEND].offset);
  assert(state.owner->ram_only==0);app->flags|=IZ_NATIVE_SOURCE_RAM;assert(bind(app,&state)==0&&state.owner->ram_only==1);app->flags=0;
 }
 State state;app->bytes=sizeof(IzNativeApp5)-1;assert(bind(app,&state)==0xf202);app->bytes=sizeof a;
 env.bytes=sizeof env-1;assert(bind(app,&state)==0xf203);env.bytes=sizeof env;
 env.sdk_bytes=1;assert(bind(app,&state)==0xf204);env.sdk_bytes=bytes;
 unsigned char*signature=(void*)(env.sdk+izpsx_services[F_SEND].offset);*signature^=1;assert(bind(app,&state)==0xf204);*signature^=1;
 free(app->data);free(app->controls);munmap(sdk,bytes);
 puts("{\"passed\":true,\"publicAbi5And6AndExtendedPrefix\":true,\"opaqueOwnerNotRead\":true,\"signatureAndBoundsChecked\":true,\"consoleExecuted\":false}");
}
