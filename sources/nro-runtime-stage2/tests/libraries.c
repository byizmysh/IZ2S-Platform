#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "switch2/runtime/dynlink.h"
static const char *names[]={"libounce.so.1","libstdc++.so.6","libouncevk.so.1","libouncevk-gl.so.1","libvulkan.so.1","libEGL.so.1","libGL.so.1","libGLESv2.so.2"};
static uint32_t word(const void *p){uint32_t x;memcpy(&x,p,4);return x;}
int main(int argc,char **argv){
 assert(argc==2);void *images[8];size_t sizes[8];DynlinkModule *mods[8];DynlinkProgram program;dynlinkProgramCreate(&program);
 // The runtime imports main from the application. Supply a minimal application
 // symbol scope; no AArch64 function or constructor executes on the host.
 const Elf64_Sym symbols_main[2]={{0},{.st_name=1,.st_info=ELF64_ST_INFO(STB_GLOBAL,STT_FUNC),.st_shndx=1,.st_value=256}};
 const uint32_t hash_main[]={1,2,1,0,0};
 const Elf64_Dyn end_dynamic={0};
 DynlinkModule application={.base=0x100000,.size=4096,.dynamic=&end_dynamic,.symtab=symbols_main,.strtab="\0main\0",.strtab_size=6,.symbol_count=2,.sysv_hash=hash_main};
 dynlinkProgramAddModule(&program,&application,true);
 for(unsigned i=0;i<8;i++){
  char path[1024];snprintf(path,sizeof path,"%s/%s",argv[1],names[i]);FILE *f=fopen(path,"rb");assert(f);unsigned char header[128];assert(fread(header,1,128,f)==128);assert(!memcmp(header+16,"NRO0",4));size_t image=word(header+24),bss=word(header+56);sizes[i]=image+bss;assert(!posix_memalign(&images[i],4096,(sizes[i]+4095)&~4095ul));memset(images[i],0,sizes[i]);rewind(f);assert(fread(images[i],1,image,f)==image);fclose(f);
  assert(!dynlinkModuleOpenInImage(images[i],sizes[i],&mods[i]));assert(mods[i]->soname&&!strcmp(mods[i]->soname,names[i]));dynlinkProgramAddModule(&program,mods[i],true);
 }
 for(unsigned i=0;i<8;i++)for(unsigned n=0;n<mods[i]->needed_count;n++){const char *need=dynlinkModuleGetNeeded(mods[i],n);assert(need);unsigned found=0;for(unsigned j=0;j<8;j++)found+=!strcmp(need,names[j]);assert(found==1);}
 const char *symbols[]={"dlopen","dlsym","dlclose","dlerror","auduOpenAudioInManager","vkCreateInstance","eglGetDisplay","glGetString"};
 for(unsigned i=0;i<sizeof symbols/sizeof *symbols;i++){const DynlinkModule *owner=0;assert(dynlinkProgramFindSymbol(&program,symbols[i],&owner));assert(owner);printf("export %s: %s\n",symbols[i],owner->soname);}
 dynlinkProgramLayoutTls(&program);assert(program.tls_size>0);const char *missing=0;Result rc=dynlinkProgramRelocate(&program,&missing);if(rc){fprintf(stderr,"Relocation failed: 0x%x, %s\n",rc,missing?missing:"(none)");for(unsigned i=0;i<8;i++)free(images[i]);return 1;}
 // Parser must reject truncated and corrupt MOD0 metadata without reading out of bounds.
 DynlinkModule invalid;assert(dynlinkModuleOpen(&invalid,images[0],3));unsigned char broken[256]={0};assert(dynlinkModuleOpen(&invalid,broken,sizeof broken));memcpy(broken+4,"\xff\xff\xff\xff",4);assert(dynlinkModuleOpen(&invalid,broken,sizeof broken));
 printf("PASS: 8 shipped libraries, dependencies, exports, TLS layout (%zu bytes) and AArch64 relocation; constructors not executed on host\n",program.tls_size);
 for(unsigned i=0;i<8;i++)free(images[i]);
}
