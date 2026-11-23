#include "store.h"
#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static Store store;
static bool damage;
bool __wrap_github_download(const char *url,uint64_t size,DownloadSink sink,void *context,
                           const atomic_bool *cancel,char out[DOWNLOAD_MESSAGE]) {
    (void)out;
    const char prefix[] = "https://github.com/byizmysh/iz2s-cheese/releases/download/v0.8.2/";
    assert(!strncmp(url, prefix, sizeof prefix - 1));
    const char *file=strrchr(url,'/');assert(file);
    char path[1024];snprintf(path,sizeof path,"%s%s",getenv("STORE_TEST_RELEASE"),file);
    FILE *source=fopen(path,"rb");assert(source);
    unsigned char buffer[4096];size_t count;bool ok=true;
    while(ok&&(count=fread(buffer,1,sizeof buffer,source))) {
        if(damage)buffer[0]^=1;
        ok=(!cancel||!atomic_load(cancel))&&count<=size&&sink(context,buffer,count);
        if(ok)size-=count;
    }
    fclose(source);return ok&&!size;
}
int main(int argc,char **argv) {
    assert(argc==2);char message[STORE_MESSAGE];
    assert(store_load(&store,argv[1],message)&&store.count==1);
    App *app=&store.apps[0];assert(app==store_self_entry(&store));
#ifdef IZ_STORE_STANDALONE
    store_set_running_nro("sdmc:/switch/iz2s-cheese/iz2s-cheese.nro","0.8.3");
#else
    store_set_running_iza("0.8.3");
#endif
    store_refresh(&store,app);assert(app->state==SELF_ENTRY);
#ifdef IZ_STORE_STANDALONE
    store_set_running_nro("sdmc:/switch/iz2s-cheese/iz2s-cheese.nro","0.8.0");
#else
    store_set_running_iza("0.8.0");
#endif
    store_refresh(&store,app);assert(app->state==UPDATE_AVAILABLE);
    /* The settings updater uses its checked release, outside the catalog array. */
    store.update=*app;store.update_checked=true;app=&store.update;
    assert(store_self_entry(&store)==app);
    View view={.screen=SETTINGS,.settings_row=SETTINGS_CHEESE_UPDATE};assert(view_input(&view,&store,BTN_A)==CHECK_STORE_UPDATE);
    view.screen=SETTINGS_RESULT;view.update_result=true;assert(view_input(&view,&store,BTN_A)==UPDATE_STORE);
    assert(!store_remove(&store,app,message));
    damage=true;assert(!store_install(&store,app,(Progress){0},message));assert(strstr(message,"SHA-256"));
    damage=false;atomic_bool cancel=true;assert(!store_install(&store,app,(Progress){.cancel=&cancel},message));
    assert(store_install(&store,app,(Progress){0},message));assert(strstr(message,"Reopen IZ2S Cheese"));
    store_refresh(&store,app);assert(app->state==UPDATE_AVAILABLE);
#ifdef IZ_STORE_STANDALONE
    store_set_running_nro("sdmc:/switch/iz2s-cheese/iz2s-cheese.nro","0.8.2");
#else
    store_set_running_iza("0.8.2");
#endif
    store_refresh(&store,app);assert(app->state==SELF_ENTRY);
    assert(!store_install(&store,app,(Progress){0},message));
    puts("Store self-update: same version, checksum failure, cancellation, removal guard, downgrade guard PASS");
}
