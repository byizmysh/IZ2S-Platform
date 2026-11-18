#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sb4/native-app.h>
#include "fs-file.h"
#include "native-menu.h"
#include "sd-layout.h"

static IzNativeApp7Storage owner;
static unsigned host_calls;
static int list_full;
static unsigned allocations, releases, cursor, closed;
static int read_error;
enum { games = 1305, non_games = 18, rows_total = games + non_games + 2 };
static uint32_t list(IzNativeApp *app,const char *name,Sb4StorageEntry *entries,uint32_t cap,uint32_t *count) {
    assert(app==&owner.base.base.base.base.base.base.base&&!*name&&cap>=4);
    ++host_calls;
    if(list_full)return SB4_STORAGE_LIMIT;
    entries[0]=(Sb4StorageEntry){.name="Game.bin",.size=991253855};
    entries[1]=(Sb4StorageEntry){.name="Game.cue",.size=48};
    entries[2]=(Sb4StorageEntry){.name="Bios",.directory=1};
    entries[3]=(Sb4StorageEntry){.name="Other.pbp",.size=850000};
    *count=4;
    return 0;
}
static uint32_t size(IzNativeApp *app,const char *name,uint64_t *out) {
    assert(app==&owner.base.base.base.base.base.base.base);
    ++host_calls;
    if(strcmp(name,"bios/bios.bin"))return 0x202;
    *out=524288;
    return 0;
}
static Sb4CFunction c_import(const char *name){(void)name;return NULL;}
static IzServiceFunction service(uint32_t mod,uint32_t id){(void)mod;(void)id;return NULL;}
static uint32_t allocate(void **memory,size_t alignment,size_t bytes){
    assert(!posix_memalign(memory,alignment,bytes));
    ++allocations;
    return 0;
}
static void release(void *memory){assert(memory);++releases;free(memory);}
static uint64_t tick(void){return 1;}
static void sleep_ns(uint64_t ns){(void)ns;}

static uint32_t open_dir(IzNativeApp *app,const char *name,uint32_t *token){
    assert(app==(void*)&owner&&!strcmp(name,"games"));
    cursor=0;*token=1;return 0;
}
static uint32_t read_dir(IzNativeApp *app,uint32_t token,Sb4StorageEntry *entries,
                         uint32_t capacity,uint32_t *count){
    assert(app==(void*)&owner&&token==1&&capacity==SB4_STORAGE_PAGE_MAX);
    *count=0;
    if(read_error&&cursor>=160)return 0x1234;
    while(*count<capacity&&cursor<rows_total){
        const unsigned row=cursor++;
        Sb4StorageEntry *entry=&entries[(*count)++];
        *entry=(Sb4StorageEntry){0};
        if(row==0){strcpy(entry->name,"Discs");entry->directory=1;}
        else if(row==1)strcpy(entry->name,"Track.bin");
        else if(row<non_games+2)snprintf(entry->name,sizeof entry->name,"Readme-%03u.txt",row);
        else snprintf(entry->name,sizeof entry->name,"Game-%04u.cue",rows_total-1-row);
    }
    return 0;
}
static uint32_t close_dir(IzNativeApp *app,uint32_t token){
    assert(app==(void*)&owner&&token==1);
    ++closed;return 0;
}

int main(void) {
    static const Sb4Compat api={SB4_COMPAT_VERSION,sizeof(Sb4Compat),SB4_COMPAT_LIBOUNCE,0,
        c_import,service,allocate,release,tick,tick,sleep_ns};
    static const Sb4Storage storage={SB4_STORAGE_VERSION,offsetof(Sb4Storage,files),list,size,NULL};
    IzNativeApp *app=(void*)&owner;
    app->magic=0x414e5a49;app->version=SB4_NATIVE_ABI;app->bytes=sizeof owner;
    owner.base.sb4=&api;owner.storage=&storage;
    assert(sb4ModuleStorage(app)&&!sb4StorageFiles(sb4ModuleStorage(app)));
    Ps1SdDirectory sd={.app=app};
    Ps1SdEntry rows[4];
    size_t count=0;
    assert(!ps1_sd_list(&sd,IZPSX_SD_ROOT,IZPSX_SD_ROOT,rows,4,&count)&&count==4);
    assert(ps1_sd_filter_images(rows,count)==3);
    assert(!strcmp(rows[0].name,"Bios")&&!strcmp(rows[1].name,"Game.cue")&&!strcmp(rows[2].name,"Other.pbp"));
    uint64_t length=0;
    assert(!ps1_sd_statfile(&sd,IZPSX_SD_ROOT,IZPSX_SD_ROOT "/bios/bios.bin",&length)&&length==524288);
    assert(ps1_sd_statfile(&sd,IZPSX_SD_ROOT,IZPSX_SD_ROOT "/bios/missing.bin",&length)==0x202&&!length);
    assert(ps1_sd_statfile(&sd,"/","/private/other.bin",&length)==PS1_SD_INVALID&&!length);
    assert(host_calls==3);
    list_full=1;
    assert(ps1_sd_list(&sd,IZPSX_SD_ROOT,IZPSX_SD_ROOT,rows,4,&count)==PS1_SD_LIMIT&&!count);
    app->bytes=sizeof(IzNativeApp7);
    assert(ps1_sd_list(&sd,IZPSX_SD_ROOT,IZPSX_SD_ROOT,rows,4,&count)==PS1_SD_INVALID&&!count&&host_calls==4);
    app->bytes=sizeof owner;
    Ps1SdCatalog catalog={0};
    list_full=0;
    assert(!ps1_sd_catalog_load(&sd,IZPSX_SD_ROOT,IZPSX_SD_ROOT,&catalog,&count)&&count==4);
    ps1_sd_catalog_release(&sd,&catalog);

    static const Sb4StorageDirectories dirs={open_dir,read_dir,close_dir};
    static const Sb4Storage paged={SB4_STORAGE_VERSION,sizeof(Sb4Storage),list,size,
        .directories=&dirs};
    owner.storage=&paged;
    Ps1Menu menu;
    static uint32_t frame[PS1_MENU_WIDTH*PS1_MENU_HEIGHT];
    const Ps1MenuConfig config={.sd=&sd,.root=IZPSX_SD_GAMES,.rgba=frame,.catalog=&catalog};
    read_error=1;
    assert(ps1_menu_init(&menu,&config)==PS1_MENU_RUNNING&&menu.error==0x1234&&!menu.count&&closed==1);
    read_error=0;
    assert(ps1_menu_step(&menu,&config,PS1_MENU_A)==PS1_MENU_RUNNING&&!menu.error&&closed==2);
    assert(menu.count==games+1&&!strcmp(catalog.entries[0].name,"Discs")&&
        !strcmp(catalog.entries[1].name,"Game-0000.cue")&&
        !strcmp(catalog.entries[games].name,"Game-1304.cue"));
    assert(ps1_menu_step(&menu,&config,PS1_MENU_UP)==PS1_MENU_RUNNING&&menu.index==games);
    ps1_menu_draw(&menu,&config);
    assert(ps1_menu_step(&menu,&config,PS1_MENU_A)==PS1_MENU_SELECTED);
    assert(!strcmp(menu.selected,IZPSX_SD_GAMES "/Game-1304.cue"));
    ps1_sd_catalog_release(&sd,&catalog);
    assert(!catalog.entries&&!catalog.capacity&&allocations==releases);
    puts("PASS: IZPSX libsb4 file scope, older list provider, 1305 sorted games, failed page, retry, and selection");
}
