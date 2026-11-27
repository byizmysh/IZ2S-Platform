#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cycle.h"
#include "menu-nro-result.h"
static const char menu[]="sdmc:/iz2s/izlauncher.iza";
typedef struct {unsigned maps,unloads,enters;uint32_t fail,unload_fail,app_result,last_result;int retain,menu_fail;char path[512];} Fixture;
static uint32_t map(void *ctx,const char *path,HbloaderMappedImage *out){
 Fixture *f=ctx;f->maps++;snprintf(f->path,sizeof f->path,"%s",path);
 if(f->fail && (strcmp(path,menu)||f->menu_fail)){if(f->retain)out->owner=f;return f->fail;}
 out->entry=4096;out->owner=f;return 0;
}
static uint32_t unload(void *ctx,const HbloaderMappedImage *image){Fixture *f=ctx;assert(image->owner==f);f->unloads++;return f->unload_fail;}
static uint32_t config(void *ctx,HbloaderConfigInput *out){(void)ctx;out->main_thread_handle=1;out->process_handle=2;out->heap_address=0x100000;out->heap_size=0x10000;out->notice="IZ2S";out->notice_size=5;return 0;}
static uint32_t enter(void *ctx,uint64_t entry,const HbloaderConfigEntry *entries,uint64_t ret,uint32_t *result){
 Fixture *f=ctx;assert(entry==4096&&ret==UINT64_MAX);f->enters++;
 for(unsigned i=0;i<HBLOADER_CONFIG_ENTRY_COUNT;i++)if(entries[i].key==HBLOADER_ENTRY_LAST_RESULT)f->last_result=entries[i].value[0];
 *result=f->app_result;return 0;
}
static HbloaderCycleResult step(HbloaderCycle *c,Fixture *f){HbloaderCyclePlatform p={f,map,unload,config,enter};return hbloaderSwitch2Step(c,&p,menu);}
int main(void){
 for(unsigned d=200;d<=206;d++){
  Fixture f={0};HbloaderCycle c={0};assert(step(&c,&f).stage==HBLOADER_CYCLE_OK);
  assert(hbloaderSwitch2Select(&c,"sdmc:/apps/test.nro","test")==0);f.fail=(d<<9)|346;
  assert(step(&c,&f).stage==HBLOADER_CYCLE_OK);assert(f.enters==1&&f.unloads==1&&!c.has_mapping);
  assert(step(&c,&f).stage==HBLOADER_CYCLE_OK);assert(f.enters==2&&f.last_result==f.fail);assert(iz2_nro_result_message(f.last_result)==d-195);
 }
 Fixture f={0};HbloaderCycle c={0};assert(step(&c,&f).stage==HBLOADER_CYCLE_OK);
 for(unsigned i=0;i<300;i++){assert(hbloaderSwitch2Select(&c,"sdmc:/apps/test.nro","test")==0);f.app_result=0;assert(step(&c,&f).stage==0);assert(step(&c,&f).stage==0);}
 assert(f.maps==601&&f.unloads==600&&f.enters==601);
 assert(hbloaderSwitch2Select(&c,"sdmc:/apps/bad.nro","bad")==0);f.fail=123;f.retain=1;
 HbloaderCycleResult r=step(&c,&f);assert(r.stage==HBLOADER_CYCLE_UNLOAD&&r.result==123&&f.enters==601);
 c=(HbloaderCycle){0};f=(Fixture){.fail=321,.menu_fail=1};r=step(&c,&f);assert(r.stage==HBLOADER_CYCLE_MAP&&f.enters==0);
 c=(HbloaderCycle){0};f=(Fixture){0};assert(step(&c,&f).stage==0);f.unload_fail=99;r=step(&c,&f);assert(r.stage==HBLOADER_CYCLE_UNLOAD&&c.has_mapping&&f.maps==1&&f.enters==1);
 c=(HbloaderCycle){0};f=(Fixture){.app_result=(203u<<9)|346u};assert(hbloaderSwitch2Select(&c,"sdmc:/apps/test.nro","test")==0);assert(step(&c,&f).stage==0);f.app_result=0;assert(step(&c,&f).stage==0);assert(f.last_result==((203u<<9)|346u));
 assert(iz2_nro_result_message(0)==0);assert(iz2_nro_result_message(0xe123)==1);
 char long_path[513];memset(long_path,'x',512);long_path[512]=0;assert(hbloaderSwitch2Select(&c,long_path,"")==-1);
 HbloaderConfigInput in={.main_thread_handle=1,.process_handle=2,.heap_address=4096,.heap_size=4096,.argv="x",.next_path=long_path,.next_argv=long_path,.notice="IZ2S",.notice_size=5};HbloaderConfigEntry entries[HBLOADER_CONFIG_ENTRY_COUNT];
 assert(!hbloaderSwitch2BuildConfig(&in,entries));for(unsigned i=0;i<HBLOADER_CONFIG_ENTRY_COUNT;i++)assert(entries[i].key!=HBLOADER_ENTRY_MODULES);
 in.modules_info=0x12340000;assert(!hbloaderSwitch2BuildConfig(&in,entries));unsigned found=0;for(unsigned i=0;i<HBLOADER_CONFIG_ENTRY_COUNT;i++)if(entries[i].key==HBLOADER_ENTRY_MODULES){assert(entries[i].value[0]==in.modules_info);found++;}assert(found==1);
 puts("PASS: 7 load-error returns; 300 app/menu cycles; cleanup refusal; menu failure; runtime link failure; ABI config; bounded paths");
}
