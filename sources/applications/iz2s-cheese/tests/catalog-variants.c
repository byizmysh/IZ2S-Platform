#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool __wrap_github_latest_release(DownloadSink sink, void *context, char out[DOWNLOAD_MESSAGE]) {
    const char *name = getenv("CHEESE_TEST_RELEASE_JSON");
    FILE *f = name ? fopen(name,"rb") : NULL;
    if (!f) { snprintf(out,DOWNLOAD_MESSAGE,"Test network unavailable."); return false; }
    char bytes[1024]; size_t n; bool ok=true;
    while(ok && (n=fread(bytes,1,sizeof bytes,f))) ok=sink(context,bytes,n);
    fclose(f); return ok;
}

int main(int argc,char **argv) {
    assert(argc>=3); char message[STORE_MESSAGE]; Store store={0}; StoreSettings settings;
    if (!strcmp(argv[1],"save-url")) {
        assert(argc==4);settings=(StoreSettings){.count=1};
        assert(strlen(argv[3]) < RELEASE_URL_MAX);
        strcpy(settings.sources[0].url, argv[3]);
        settings.sources[0].enabled = true;
        bool ok=store_settings_write(argv[2],&settings,message);
        if(!ok) {puts(message);return 1;}
    }
    store_settings_read(argv[2],&settings);
    if(!strcmp(argv[1],"settings")||!strcmp(argv[1],"save-url")) {
        printf("%s\n%s\n",settings.sources[0].url,settings.sd_enabled?"sd":"online");return 0;
    }
    if(!store_load(&store,argv[2],message)) {puts(message);return 1;}
#ifdef IZ_STORE_STANDALONE
    PackageKind kind=PACKAGE_NRO;const char *format="nro";
    store_set_running_nro("sdmc:/switch/iz2s-cheese/iz2s-cheese.nro","0.8.2");
#else
    PackageKind kind=PACKAGE_IZM;const char *format="iza";
    store_set_running_iza("0.8.2");
#endif
    if(!strcmp(argv[1],"update")) {
        if(!store_check_update(&store,message)) {puts(message);store_clear(&store);return 1;}
        const App *app=store_self_entry(&store);assert(app&&app==&store.update&&app->package.kind==kind);
        printf("%s\t%s\t%u\t%s\n",format,app->package.version,app->package.count,app->package.release_url);
        View view={.screen=SETTINGS,.settings_row=SETTINGS_CHEESE_UPDATE};
        assert(view_input(&view,&store,BTN_A)==CHECK_STORE_UPDATE);
        view.screen=SETTINGS_RESULT;view.update_result=true;
        if(app->state==UPDATE_AVAILABLE) {
            assert(view_input(&view,&store,BTN_A)==UPDATE_STORE);
            assert(view_touch(&view,&store,620,530)==UPDATE_STORE);
        } else {assert(view_input(&view,&store,BTN_A)==NO_JOB&&view.screen==SETTINGS);}
        Store clone={0};assert(store_clone(&clone,&store));assert(store_self_entry(&clone)==&clone.update);
        assert(!store_remove(&store,app,message));store_clear(&clone);store_clear(&store);return 0;
    }
    View view={0};view_filter(&view,&store);
    for(unsigned i=0;i<view.count;++i) {
        const Package *p=&store.apps[view.visible[i]].package;
        printf("%s\t%s\t%s\n",p->id,p->kind==PACKAGE_NRO?"nro":"iza",p->version);
    }
    if(!strcmp(argv[1],"variants")) {
        View welcome = {.settings = {.count = 1, .sources = {
            {.url = STORE_COMMUNITY_CATALOG_URL, .community = true}
        }}};
        Store empty = {0};
        assert(view_input(&welcome, &empty, BTN_A) == ENABLE_COMMUNITY);
        assert(view_touch(&welcome, &empty, 320, 290) == ENABLE_COMMUNITY);
        assert(view_input(&welcome, &empty, BTN_ZL) == NO_JOB && !welcome.compact);
        assert(view_input(&welcome, &empty, BTN_ZR) == NO_JOB);
        assert(view_touch(&welcome, &empty, 690, 290) == NO_JOB && welcome.screen == CATALOG_SETTINGS);
        assert(view_input(&welcome, &empty, BTN_B) == NO_JOB && welcome.screen == CATALOG);
        assert(view_input(&welcome, &empty, BTN_X) == NO_JOB && welcome.screen == CATALOG_SETTINGS);
        assert(view_input(&welcome, &empty, BTN_DOWN) == NO_JOB && welcome.catalog_row == 1);
        assert(view_input(&welcome, &empty, BTN_A) == TOGGLE_CATALOG);
        assert(view_touch(&welcome, &empty, 1130, 187) == TOGGLE_CATALOG);
        assert(view_input(&welcome, &empty, BTN_Y) == NO_JOB);
        View browsing = {.settings = settings};
        assert(view_touch(&browsing, &store, 1130, 62) == NO_JOB && browsing.screen == CATALOG_SETTINGS);
        assert(view_input(&browsing, &store, BTN_B) == NO_JOB && browsing.screen == CATALOG);
        View settings_view = {.screen = SETTINGS, .settings = settings};
        assert(view_input(&settings_view, &store, BTN_A) == NO_JOB && settings_view.screen == CATALOG_SETTINGS);
        assert(view_touch(&settings_view, &store, 1130, 138) == TOGGLE_CATALOG);
        assert(view_input(&settings_view, &store, BTN_X) == ADD_CATALOG);
        assert(view_input(&settings_view, &store, BTN_Y) == EDIT_CATALOG);
        assert(view_input(&settings_view, &store, BTN_DOWN) == NO_JOB);
        assert(view_input(&settings_view, &store, BTN_Y) == NO_JOB);
        assert(view_input(&settings_view, &store, BTN_B) == NO_JOB && settings_view.screen == SETTINGS);
        char address[RELEASE_URL_MAX];
        assert(store_catalog_address(STORE_COMMUNITY_CATALOG_URL, address));
        assert(!strcmp(address, STORE_COMMUNITY_CATALOG_URL));
        assert(store.count==4);
        assert(store_self_entry(&store)&&store_self_entry(&store)->package.kind==kind);
        for(unsigned i=0;i<store.count;++i)
            if(!strcmp(store.apps[i].package.id,"dual"))assert(store.apps[i].package.kind==kind);
#ifdef IZ_STORE_STANDALONE
        assert(view.count==3);
#else
        assert(view.count==4);
#endif
        unsigned all = view.count;
        assert(view_input(&view,&store,BTN_ZR) == EDIT_SEARCH);
        assert(view_touch(&view,&store,900,60) == EDIT_SEARCH);
        assert(view_input(&view,&store,BTN_ZL) == NO_JOB && view.compact);
        assert(view_touch(&view,&store,300,685) == NO_JOB && !view.compact);
        assert(view_touch(&view,&store,300,685) == NO_JOB && view.compact);
        assert(view_touch(&view,&store,43,188) == NO_JOB && view.filter == 1);
        view_search(&view,&store,"  DuAl FiXTure EXamPle\t");
        assert(!strcmp(view.search,"DuAl FiXTure EXamPle") && view.count == 1);
        assert(!strcmp(store.apps[view.visible[0]].package.id,"dual"));
        assert(view_touch(&view,&store,43,188) == NO_JOB && view.count == 0);
        assert(view_touch(&view,&store,43,135) == NO_JOB && view.count == 1);
        view_search(&view,&store,"IZA-ONLY");
        assert(view.count == (kind == PACKAGE_NRO ? 0u : 1u));
        view_search(&view,&store,"no-such-app");
        assert(view.count == 0);
        assert(view_input(&view,&store,BTN_B) == NO_JOB && !view.search[0] && view.count == all);
        assert(view_input(&view,&store,BTN_B) == EXIT);
        uint32_t pixels[STORE_ICON_SIZE*STORE_ICON_SIZE];
        assert(store_read_artwork(&store,NULL)==4);
        assert(store_read_artwork_icon(&store,0,pixels)&&pixels[0]==0xff336699);
    }
    free(view.visible);store_clear(&store);return 0;
}
