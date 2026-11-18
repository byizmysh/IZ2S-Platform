#define _POSIX_C_SOURCE 200809L
#include "../legacy/native/module-folder.h"
#include <switch.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include "platform.h"
#include "apps.h"
#include "import.h"
#include <sb4.h>
#include "font-assets.h"
#include "font.h"
#include "pc-link.h"
#include "ftp-connect.h"
#include "menu-update.h"
#include "menu-factory.h"

static IzOunceLaunch next_program;
static bool next_program_ready;
static const char *last_pc_import;
static IzPcStoreApp store_apps[IZ_PC_STORE_LIMIT];
static unsigned store_count,store_index;
static bool store_busy;
static uint64_t installed_versions;

static const char *store_version(const IzPcStoreApp *app){
    return app->name[48]==31?app->name+49:"";
}

static bool module_files(const char *id,char image[128],char receipt[128]){
    size_t bytes=strnlen(id,48);
    if(!bytes||bytes>=48||id[0]=='-'||strspn(id,"abcdefghijklmnopqrstuvwxyz0123456789-")!=bytes)return false;
    snprintf(image,128,"sdmc:" IZ_SD_ROOT "/%s/launcher.iza",iz_module_folder(id));
    snprintf(receipt,128,"sdmc:" IZ_SD_ROOT "/%s/.iz2s-version",iz_module_folder(id));
    return true;
}

typedef struct {char version[16];uint8_t digest[32];} IzVersionReceipt;

typedef struct {char name[78];u32 bytes;} IzInstalledAsset;

static bool installed_asset_name(const char *name){
    return strlen(name)==77&&!strncmp(name,"asset-",6)&&
        strspn(name+6,"0123456789abcdef")==64&&!strcmp(name+70,".izdata");
}

static int installed_asset_order(const void *a,const void *b){
    return strcmp(((const IzInstalledAsset*)a)->name,((const IzInstalledAsset*)b)->name);
}

static unsigned bound_assets(const char *folder,const IzAppHeader *header,IzInstalledAsset assets[16]){
    if(!(header->flags&IZ_APP_ASSETS))return 0;
    DIR *directory=opendir(folder);
    if(!directory)return 0;
    unsigned count=0;
    bool complete=false;
    for(;;){
        errno=0;
        struct dirent *entry=readdir(directory);
        if(!entry){complete=errno==0;break;}
        if(!installed_asset_name(entry->d_name))continue;
        char file[224];
        struct stat info;
        snprintf(file,sizeof file,"%s/%.77s",folder,entry->d_name);
        if(lstat(file,&info))break;
        if(!S_ISREG(info.st_mode))continue;
        if(count>=16||info.st_size<=0||(u64)info.st_size>UINT32_MAX)break;
        memcpy(assets[count].name,entry->d_name,sizeof assets[count].name);
        assets[count++].bytes=(u32)info.st_size;
    }
    if(closedir(directory)||!complete||count!=header->reserved[1])return 0;

    // The package binds the ordered list of names, sizes and SHA-256 values.
    // Leave the entire set alone when the directory cannot prove an exact match.
    qsort(assets,count,sizeof assets[0],installed_asset_order);
    Sha256Context hash;
    sha256ContextCreate(&hash);
    static const char prefix[]="iz2s.sd-assets.v1\n[";
    sha256ContextUpdate(&hash,prefix,sizeof prefix-1);
    u64 total=0;
    for(unsigned i=0;i<count;i++){
        char row[176];
        const IzInstalledAsset *asset=&assets[i];
        int bytes=snprintf(row,sizeof row,"%s[\"%s\",%u,\"%.64s\"]",i?",":"",asset->name,asset->bytes,asset->name+6);
        if(bytes<0||(size_t)bytes>=sizeof row)return 0;
        sha256ContextUpdate(&hash,row,(size_t)bytes);
        total+=asset->bytes;
    }
    sha256ContextUpdate(&hash,"]",1);
    u8 digest[32];
    sha256ContextGetHash(&hash,digest);
    return total==header->reserved[2]&&!memcmp(digest,header->padding,32)?count:0;
}

static bool verified_asset(const char *path,const IzInstalledAsset *asset){
    FILE *file=fopen(path,"rb");
    if(!file)return false;
    Sha256Context hash;
    sha256ContextCreate(&hash);
    u8 buffer[16384],digest[32];
    size_t bytes;
    u64 total=0;
    while((bytes=fread(buffer,1,sizeof buffer,file))!=0){
        total+=bytes;
        if(total>asset->bytes)break;
        sha256ContextUpdate(&hash,buffer,bytes);
    }
    const bool complete=!ferror(file)&&total==asset->bytes;
    if(fclose(file)||!complete)return false;
    sha256ContextGetHash(&hash,digest);
    const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<32;i++){
        if(asset->name[6+i*2]!=hex[digest[i]>>4]||
           asset->name[7+i*2]!=hex[digest[i]&15])return false;
    }
    return true;
}

static bool read_installed_header(const char *path,const char *id,IzAppHeader *header){
    struct stat info;
    if(lstat(path,&info)||!S_ISREG(info.st_mode))return false;
    FILE *file=fopen(path,"rb");
    if(!file)return false;
    const bool read=fread(header,1,sizeof *header,file)==sizeof *header;
    return !fclose(file)&&read&&!iz_validate_header(header,info.st_size)&&iz_module_matches(header->id,id);
}

static bool installer_backup(const char *name){
    if(!strcmp(name,".launcher-update.bak"))return true;
    return strlen(name)==30&&!strncmp(name,".launcher-",10)&&
        strspn(name+10,"0123456789abcdef")==16&&!strcmp(name+26,".bak");
}

static u32 remove_recovery_backups(const char *folder,const char *id){
    DIR *directory=opendir(folder);
    if(!directory)return errno;
    u32 result=0;
    bool changed=false;
    while(!result){
        errno=0;
        struct dirent *entry=readdir(directory);
        if(!entry){result=errno;break;}
        if(!installer_backup(entry->d_name)&&strcmp(entry->d_name,".izstore-backup.iza"))continue;
        char path[160];
        snprintf(path,sizeof path,"%s/%.30s",folder,entry->d_name);
        IzAppHeader header;
        if(!read_installed_header(path,id,&header))continue;
        if(unlink(path)){result=errno;break;}
        changed=true;
    }
    if(closedir(directory)&&!result)result=errno;
    if(changed){
        u32 committed=fsdevCommitDevice("sdmc");
        if(!result)result=committed;
    }
    return result;
}

u32 iz_ounce_remove_module(const char *id){
    char image[128],receipt[128],folder[128];
    if(!module_files(id,image,receipt))return EINVAL;
    snprintf(folder,sizeof folder,"sdmc:" IZ_SD_ROOT "/%s",iz_module_folder(id));
    struct stat info;
    if(lstat(folder,&info)||!S_ISDIR(info.st_mode))return EINVAL;
    IzAppHeader header;
    if(!read_installed_header(image,id,&header))return EINVAL;
    IzInstalledAsset assets[16];
    unsigned count=bound_assets(folder,&header,assets);

    // Retire validated backups first so launch-time recovery cannot undo removal.
    u32 result=remove_recovery_backups(folder,id);
    if(result)return result;
    if(unlink(image))return errno;
    result=fsdevCommitDevice("sdmc");
    if(result)return result;

    bool changed=false;
    for(unsigned i=0;i<count;i++){
        char path[224];
        snprintf(path,sizeof path,"%s/%.77s",folder,assets[i].name);
        if(lstat(path,&info)||!S_ISREG(info.st_mode)||!verified_asset(path,&assets[i]))continue;
        if(unlink(path))return errno;
        changed=true;
    }
    if(!lstat(receipt,&info)&&S_ISREG(info.st_mode)&&info.st_size==sizeof(IzVersionReceipt)){
        FILE *file=fopen(receipt,"rb");
        if(file){
            u8 digest[32];
            IzVersionReceipt record;
            bool complete=fread(&record,1,sizeof record,file)==sizeof record;
            sha256CalculateHash(digest,&header,sizeof header);
            if(!fclose(file)&&complete&&!memcmp(digest,record.digest,sizeof digest)){
                if(unlink(receipt))return errno;
                changed=true;
            }
        }
    }
    return changed?fsdevCommitDevice("sdmc"):0;
}

static void describe_add_hint(IzLocalAdd *message){
    const char *hint=iz_pc_online()?"App library on PC.":"Copy .iza to " IZ_INCOMING_DIRECTORY;
    iz_ounce_label(message->inbox,IZ_LOCAL_ADD_WIDTH,IZ_LOCAL_ADD_HEIGHT,hint,29);
}

static void describe_add(IzLocalAdd *message,const char *status) {
    message->magic=IZ_LOCAL_ADD_MAGIC;
    message->grid_magic=IZ_ADD_GRID_MAGIC;
    iz_ounce_label(message->status,IZ_LOCAL_ADD_WIDTH,IZ_LOCAL_ADD_HEIGHT,status,34);
    describe_add_hint(message);
}

static void render_store_grid(IzLocalAdd *message){
    message->count=store_count;
    message->selected=store_index;
    unsigned first=store_index/IZ_ADD_CARD_COUNT*IZ_ADD_CARD_COUNT;
    memset(message->cards,0,sizeof message->cards);
    memset(message->versions,0,sizeof message->versions);
    message->installed_versions=0;
    for(unsigned i=0;i<IZ_ADD_CARD_COUNT&&first+i<store_count;++i){
        const IzPcStoreApp *app=&store_apps[first+i];
        memcpy(message->cards[i].id,app->id,sizeof app->id);
        snprintf(message->cards[i].name,sizeof message->cards[i].name,"%s",app->name);
        snprintf(message->versions[i],sizeof message->versions[i],"%.15s",store_version(app));
        if(installed_versions&(UINT64_C(1)<<(first+i)))message->installed_versions|=1u<<i;
    }
}

static void describe_store(IzLocalAdd *message){
    describe_add(message,store_count?"":"No compatible applications in the PC library.");
    render_store_grid(message);
}

bool iz_ounce_add_store(IzLocalAdd *message,u64 pressed){
    unsigned count,status;
    bool selected;
    if(iz_pc_store_result(store_apps,&count,&status,&selected)){
        store_busy=false;
        if(status){
            describe_add(message,status==3?"An app is already queued. Wait for it to install.":
                selected?"The PC could not queue this app.":"PC library unavailable. Update the PC panel and reopen Add App.");
        }else if(selected){
            describe_add(message,"App queued. Waiting for the PC transfer...");
        }else{
            store_count=count;
            store_index=0;
            installed_versions=0;
            for(unsigned i=0;i<store_count;i++){
                if(store_apps[i].name[47]==1)installed_versions|=UINT64_C(1)<<i;
            }
            describe_store(message);
        }
        return true;
    }
    if(store_busy||!store_count)return false;
    if(pressed&(HidNpadButton_Left|HidNpadButton_StickLLeft)){
        store_index=(store_index+store_count-1)%store_count;
        describe_store(message);
        return true;
    }
    if(pressed&(HidNpadButton_Right|HidNpadButton_StickLRight)){
        store_index=(store_index+1)%store_count;
        describe_store(message);
        return true;
    }
    if((pressed&HidNpadButton_A)&&!(installed_versions&(UINT64_C(1)<<store_index))&&
       iz_pc_store_request(store_apps[store_index].id)){
        store_busy=true;
        describe_add(message,"Preparing the selected app on the PC...");
        return true;
    }
    return false;
}

// The activity atlas uses the same six rows and packed glyphs as the PC menu.
enum { ACTIVITY_LIMIT=6, ACTIVITY_WIDTH=1024, ACTIVITY_HEIGHT=512 };
typedef struct {
    char name[64];
    char action[64];
    char date[24];
    unsigned progress;
} IzActivity;
static IzActivity activity[ACTIVITY_LIMIT];
static unsigned activity_count;
static uint32_t activity_revision;
static uint32_t activity_raster_revision;
static uint32_t activity_text_revision;
static uint8_t activity_text[ACTIVITY_WIDTH*ACTIVITY_HEIGHT/4];

void iz_ounce_activity_record(const char *name,const char *action){
    memmove(activity+1,activity,(ACTIVITY_LIMIT-1)*sizeof(*activity));
    activity[0]=(IzActivity){0};
    snprintf(activity[0].name,sizeof activity[0].name,"%.63s",name);
    snprintf(activity[0].action,sizeof activity[0].action,"%.63s",action);
    time_t now=time(NULL);
    struct tm local;
    if(localtime_r(&now,&local))
        strftime(activity[0].date,sizeof activity[0].date,"%Y-%m-%d %H:%M", &local);
    if(activity_count<ACTIVITY_LIMIT)++activity_count;
    ++activity_revision;
    ++activity_text_revision;
}

static void activity_label(unsigned row,unsigned x,unsigned y,unsigned width,
                           const char *text,float size){
    uint8_t bitmap[496*24]={0};
    iz_ounce_label(bitmap,width,24,text,size);
    for(unsigned line=0;line<24;line++){
        for(unsigned column=0;column<width;column++){
            unsigned at=(row*64+y+line)*ACTIVITY_WIDTH+x+column;
            unsigned shade=(bitmap[line*width+column]*3u+127u)/255u;
            activity_text[at/4]|=(uint8_t)(shade<<((at%4)*2));
        }
    }
}

static IzActivity *transfer_activity(void){
    for(unsigned i=0;i<activity_count;i++)if(activity[i].progress)return &activity[i];
    return NULL;
}

static void update_transfer_activity(IzActivity *entry,unsigned progress){
    if(entry->progress==progress)return;
    entry->progress=progress;
    ++activity_revision;
}

static void track_transfer(void){
    uint32_t bytes,total;
    if(!iz_pc_receive_progress(&bytes,&total))return;
    IzActivity *entry=transfer_activity();
    if(!entry){
        iz_ounce_activity_record("App installation","Receiving from PC");
        entry=&activity[0];
    }
    unsigned percent=(unsigned)((uint64_t)bytes*100/total);
    update_transfer_activity(entry,(percent>100?100:percent)+1);
}

static void finish_transfer(const char *name,const char *action){
    IzActivity *entry=transfer_activity();
    if(!entry){iz_ounce_activity_record(name,action);return;}
    snprintf(entry->name,sizeof entry->name,"%.63s",name);
    snprintf(entry->action,sizeof entry->action,"%.63s",action);
    ++activity_text_revision;
    update_transfer_activity(entry,0);
}

void iz_ounce_activity_publish(uint8_t *controls,uint8_t *snapshot){
    if(!activity_count)return;
    enum { HEADER=1024+0xf0000, TEXT=1024+0x100000 };
    if(activity_raster_revision!=activity_text_revision){
        memset(activity_text,0,sizeof activity_text);
        for(unsigned i=0;i<activity_count;i++){
            activity_label(i,0,0,496,activity[i].name,22);
            activity_label(i,0,28,496,activity[i].action,17);
            activity_label(i,580,3,208,activity[i].date,15);
        }
        activity_raster_revision=activity_text_revision;
    }
    uint8_t *destinations[]={controls,snapshot};
    for(unsigned i=0;i<2;i++){
        uint8_t *dest=destinations[i];
        uint32_t *header=(void*)(dest+HEADER);
        if(header[0]==0x50534143u&&header[2]==activity_revision)continue;
        memcpy(dest+TEXT,activity_text,sizeof activity_text);
        memset(header,0,128);
        header[0]=0x50534143u;
        header[1]=activity_count;
        header[2]=activity_revision;
        for(unsigned row=0;row<activity_count;row++){
            header[4+row*4]=UINT32_MAX;
            header[6+row*4]=activity[row].progress;
        }
    }
}

static void record_ftp_start(uint32_t result){
    char ip[INET_ADDRSTRLEN],activity[64];
    struct in_addr address={.s_addr=iz_ftp_address()};
    if(result)iz_ounce_activity_record("FTP","Could not start");
    else if(address.s_addr&&inet_ntop(AF_INET,&address,ip,sizeof ip)){
        snprintf(activity,sizeof activity,"Started · %s:%u",ip,IZ_FTP_PORT);
        iz_ounce_activity_record("FTP",activity);
    }else iz_ounce_activity_record("FTP","Started · waiting for IPv4");
}

bool iz_ounce_add_ftp_tick(IzLocalAdd *message,bool requested,bool can_open){
    static uint32_t displayed_address;
    bool capability_changed=message->ftp_toggle!=1u;
    message->ftp_toggle=1u;
    bool was_running=iz_ftp_listening();
    bool toggle=requested&&(was_running||can_open);
    uint32_t result=0;
    if(toggle){
        if(was_running){
            iz_ftp_close();
            iz_ounce_activity_record("FTP","Stopped");
        }else{
            result=iz_ftp_enable();
            record_ftp_start(result);
        }
    }
    bool running=iz_ftp_listening();
    struct in_addr address={.s_addr=running?iz_ftp_address():0};
    if(!toggle&&((message->ftp_running==1u)==running)&&displayed_address==address.s_addr)
        return capability_changed;
    displayed_address=address.s_addr;
    if(!toggle&&!running)iz_ounce_activity_record("FTP","Connection closed");
    char status[26]="";
    char ip[INET_ADDRSTRLEN];
    if(running){
        if(address.s_addr&&inet_ntop(AF_INET,&address,ip,sizeof ip))
            snprintf(status,sizeof status,"FTP %s:%u",ip,IZ_FTP_PORT);
        else snprintf(status,sizeof status,"FTP NO IPV4");
    }
    else if(result)snprintf(status,sizeof status,"FTP ERROR 0X%08X",result);
    message->ftp_running=running?1u:result?2u:0u;
    iz_ounce_label(message->ftp_status,IZ_LOCAL_ADD_WIDTH,IZ_LOCAL_ADD_HEIGHT,status,20);
    // Keep the bitmap for older menus. The last 32 bytes are below its text.
    uint8_t *label=message->ftp_status+sizeof message->ftp_status-32;
    memcpy(label,"FTP1",4);
    memcpy(label+4,status,sizeof status);
    return true;
}

static void start_connection(void){
    u32 rc=iz_pc_start();
    fprintf(stderr,"IZLAUNCHER_PC_START rc=0x%08x\n",rc);
}
static void request_connection(u32 request){
    if(request==IZ_NATIVE_FTP_REQUEST){
        if(!iz_ftp_listening()){
            uint32_t result=iz_ftp_enable();
            record_ftp_start(result);
        }
    }else start_connection();
}

static const char *pending_local_import;

static const char *pc_import_status(u32 code){
    if(code==EEXIST)return "This app version is already installed.";
    if(code==IZ_IMPORT_PC_UPDATED)return "App updated from PC. Press B to return.";
    if(code)return "Last PC import failed. See /iz2s/izlauncher-runtime.log.";
    return "App installed from PC. Press B to return.";
}

static int poll_connection(IzLocalAdd *message){
    track_transfer();
    u32 code=0;
    enum IzPcDelivery result=iz_pc_delivery(&code);
    if(result==IZ_PC_IDLE)return 0;
    const char *local=pending_local_import;
    pending_local_import=NULL;
    if(result==IZ_PC_ERROR){
        if(transfer_activity())finish_transfer("App installation","Transfer interrupted");
        bool online=iz_pc_online();
        const char *error=online?"PC transfer failed. Open Add App to retry.":"PC unavailable. Open Add App to retry.";
        describe_add(message,!online&&local?local:error);
        return 1;
    }
    if(result==IZ_PC_UPDATE_REQUIRED){describe_add(message,"App queued. Update IZlauncher to install it.");return 1;}
    if(result==IZ_PC_EMPTY||result==IZ_PC_IMPORT_REPORTED||result==IZ_PC_TRANSFER_REPORTED){
        if(result==IZ_PC_EMPTY&&!local){
            if(store_busy)return 0;
            if(store_count){describe_store(message);return 1;}
        }
        if(result==IZ_PC_IMPORT_REPORTED)last_pc_import=pc_import_status(code);
        if(result==IZ_PC_TRANSFER_REPORTED)last_pc_import="Last package reached Switch. Import unconfirmed.";
        describe_add(message,local?local:last_pc_import?last_pc_import:"Connected to PC. Queue is empty.");
        return 1;
    }
    char id[48]={0};
    int updated=0;
    u32 rc=iz_import_pc_package(IZ_PC_LINK_PACKAGE,"sdmc:" IZ_SD_ROOT,"romfs:/menu/pad-normalize.comp.spv",id,&updated);
    fprintf(stderr,"IZLAUNCHER_PC_IMPORT id=%s rc=0x%08x updated=%d\n",id,rc,updated);
    unlink(IZ_PC_LINK_PACKAGE);
    code=!rc&&updated?IZ_IMPORT_PC_UPDATED:rc;
    finish_transfer(id[0]?id:"App installation",!rc?(updated?"Updated from PC":"Installed from PC"):
        rc==EEXIST?"Already installed":"Installation failed");
    render_store_grid(message);
    iz_pc_import_result(code);
    last_pc_import=pc_import_status(code);
    describe_add(message,last_pc_import);
    return rc?1:2;
}

static void record_sd_import(IzImportResult result){
    if(result==IZ_IMPORT_ADDED)
        iz_ounce_activity_record("SD modules","Installed from SD");
    else if(result==IZ_IMPORT_UPDATED)
        iz_ounce_activity_record("SD modules","Updated from SD");
    else if(result==IZ_IMPORT_PARTIAL)
        iz_ounce_activity_record("SD modules","Partially imported from SD");
}

static int add_packages(IzLocalAdd *message) {
    store_count=0;
    store_index=0;
    message->count=0;
    message->selected=0;
    IzImportResult result=iz_import_incoming(1);
    record_sd_import(result);
    static const char *const status[]={
        [IZ_IMPORT_EMPTY]="No app packages found on the SD card.",
        [IZ_IMPORT_CURRENT]="No new apps. A matching module folder already exists.",
        [IZ_IMPORT_ADDED]="App library updated. Press B to return.",
        [IZ_IMPORT_UPDATED]="Apps upgraded from SD. Press B to return.",
        [IZ_IMPORT_FAILED]="Import failed. See /iz2s/izlauncher-runtime.log.",
        [IZ_IMPORT_PARTIAL]="Library partially updated. See /iz2s/izlauncher-runtime.log.",
    };
    describe_add(message,status[result]);
    bool installed=result==IZ_IMPORT_ADDED||result==IZ_IMPORT_UPDATED||result==IZ_IMPORT_PARTIAL;
    if(iz_pc_start()==0){
        if(iz_pc_store_request(NULL)){
            store_busy=true;
            if(!installed)describe_add(message,"Loading the App Store catalog from the PC...");
        }else if(iz_pc_fetch()){
            pending_local_import=result==IZ_IMPORT_EMPTY?NULL:status[result];
        }
    }
    return installed;
}
static u32 select_program(u32 token){
    if(!envHasNextLoad())return 0xe85d;
    next_program_ready=false;
    u32 rc=iz_ounce_apps_prepare(token,&next_program);if(rc)return rc;
    NroOunceInfo platform;
    if(nroGetOunceInfo(next_program.header,next_program.header_bytes,&platform)&&platform.platform!=NroPlatform_Switch2)return 0xe85f;
    next_program_ready=true;
    fprintf(stderr,"IZLAUNCHER_NEXT_PREPARED path=%s\n",next_program.path);
    return 0;
}

static void *read_resource(const char *path,size_t *size){
    *size=0;FILE *file=fopen(path,"rb");if(!file)return NULL;
    if(fseek(file,0,SEEK_END)){fclose(file);return NULL;}
    long length=ftell(file);if(length<1||length>16*1024*1024||fseek(file,0,SEEK_SET)){fclose(file);return NULL;}
    void *data=malloc((size_t)length);
    if(!data){fclose(file);return NULL;}
    if(fread(data,1,(size_t)length,file)!=(size_t)length){free(data);fclose(file);return NULL;}
    fclose(file);*size=(size_t)length;return data;
}
static u32 buffer(u64 *address,size_t bytes){
    void *p=NULL;int result=posix_memalign(&p,4096,bytes);if(result)return (u32)result;
    memset(p,0,bytes);*address=(u64)(uintptr_t)p;return 0;
}
static bool menu_reload;

bool iz_ounce_menu_frame(bool idle){
    if(iz_menu_trial()&&!iz_menu_accept()){
        menu_reload=true;return true;
    }
    if(!idle||store_busy||pending_local_import||!iz_pc_menu_idle()||!iz_menu_begin_trial())return false;
    iz_pc_allow_auto_fetch(false);
    menu_reload=true;
    return true;
}

static u32 run_menu(bool import){
    bool romfs_owned=false;
    u32 result=romfsMountFromFsdev(iz_menu_romfs(),0,"romfs");
    if(R_FAILED(result))return result;
    romfs_owned=true;
    if(import)record_sd_import(iz_import_incoming(0));
    IzContext *c=calloc(1,sizeof(*c));void *menu=NULL,*image=NULL,*atlas=NULL,*legacy_atlas=NULL;size_t image_bytes=0,atlas_bytes=0,legacy_bytes=0;
    if(!c){result=0xe851;goto finished;}
    c->magic=0x44535a49;c->version=1;
    result=iz_ounce_font_open("romfs:/menu/catalog-font.ttf");if(result)goto cleanup;
    c->platform_title=iz_ounce_title;
    c->platform_add_apps=add_packages;
    c->platform_add_poll=poll_connection;
    c->platform_connect=request_connection;
    c->platform_apps=iz_ounce_apps_page;c->platform_select=select_program;
    image=read_resource(iz_menu_image(),&image_bytes);
    atlas=read_resource("romfs:/menu/type-atlas.bin",&atlas_bytes);
    legacy_atlas=read_resource("romfs:/menu/legacy-atlas.bin",&legacy_bytes);
    if(!image||image_bytes<512||!atlas||atlas_bytes!=IZ_FOCUS_FONT_BYTES||!legacy_atlas||legacy_bytes!=IZ_LEGACY_FONT_BYTES||
       (iz_menu_factory()&&(iz_crc(atlas,atlas_bytes)!=IZ_FOCUS_FONT_CRC||
                          iz_crc(legacy_atlas,legacy_bytes)!=IZ_LEGACY_FONT_CRC))){result=0xe85a;goto cleanup;}
    IzAppHeader *header=image;result=iz_validate_header(header,image_bytes);if(result)goto cleanup;
    result=buffer(&c->scratch,65536);if(result)goto cleanup;
    result=buffer(&c->snapshot,IZ_SHARED_BYTES);if(result)goto cleanup;
    c->arena_bytes=512*1024;result=buffer(&c->arena,c->arena_bytes);if(result)goto cleanup;
    c->library_capacity=IZ_LIBRARY_CAPACITY;result=buffer(&c->library_staging,c->library_capacity);if(result)goto cleanup;
    result=iz_ounce_platform_open(c);if(result)goto cleanup;
    fprintf(stderr,"IZLAUNCHER_PLATFORM_READY\n");
    unsigned char *controls=(void*)(uintptr_t)c->controls;
    memcpy(controls,(unsigned char*)image+header->section[SEC_STATE].offset,header->section[SEC_STATE].bytes);
    memcpy(controls+1024,(unsigned char*)image+header->section[SEC_DATA].offset,header->section[SEC_DATA].bytes);
    memcpy(controls+1024,legacy_atlas,legacy_bytes);
    memcpy(controls+1024+65536,atlas,atlas_bytes);
    fprintf(stderr,"IZLAUNCHER_FONTS legacy=0x%08x focus=0x%08x\n",iz_crc(controls+1024,legacy_bytes),iz_crc(controls+1024+65536,atlas_bytes));
    *(u32*)(controls+0x70001c)=0x495a4335;
    IzLocalAdd *add=(void*)(controls+IZ_LOCAL_ADD_AT);
    add->magic=IZ_LOCAL_ADD_MAGIC;
    add->grid_magic=IZ_ADD_GRID_MAGIC;
    add->ftp_toggle=1;
    // A menu replacement can share the running server with the old menu.
    iz_ounce_add_ftp_tick(add,false,false);
    result=iz_native_menu_build(c,header,image,&menu);if(result)goto cleanup;
    fprintf(stderr,"IZLAUNCHER_MENU_READY services=%u\n",c->service_bridge->count);
    start_connection();
    result=(u32)iz_run(c);
    fprintf(stderr,"IZLAUNCHER_RUN_RETURN rc=0x%08x native=0x%08x state=%u poisoned=%u frames=%llu\n",result,c->native_status,c->state,c->poisoned,(unsigned long long)c->frames);
cleanup:
    iz_pc_allow_auto_fetch(false);
    {
        u32 close=iz_native_menu_destroy(c,menu);
        if(!close)close=iz_ounce_platform_close(c);
        fprintf(stderr,"IZLAUNCHER_CLOSE rc=0x%08x\n",close);
        if(close){
            /* No return to hbloader with an executing child or unknown GPU
             * ownership. The process, not a guessed receipt, retires it. */
            if(iz_menu_trial())iz_menu_reject(result?result:close);
            fflush(stderr);svcExitProcess();
        }
    }
    free((void*)(uintptr_t)c->library_staging);free((void*)(uintptr_t)c->arena);
    free((void*)(uintptr_t)c->snapshot);free((void*)(uintptr_t)c->scratch);
    iz_ounce_font_close();
    free(legacy_atlas);free(atlas);free(image);free(c);
finished:
    if(romfs_owned)romfsExit();
    return result;
}

int main(int argc,char **argv){
    next_program_ready=false;
    iz_ounce_apps_init(argc>0&&argv?argv[0]:NULL);
    bool sd_owned=false;
    Result result=0;
    if(!fsdevGetDeviceFileSystem("sdmc")){
        result=fsdevMountSdmc();if(R_FAILED(result))return (int)result;sd_owned=true;
    }
    freopen("sdmc:/iz2s/izlauncher-runtime.log","a",stderr);setvbuf(stderr,NULL,_IONBF,0);
    fprintf(stderr,"IZLAUNCHER_RUNTIME_ENTRY\n");
    extern void iz_ounce_jit_session(void);
    iz_ounce_jit_session();
    fprintf(stderr,"IZLAUNCHER_ADD_APP_RAIL version=run25-02\n");
    const Sb4Profile *profile=sb4GetProfile();
    fprintf(stderr,"IZLAUNCHER_SB4_SDK version=%u backend=%s services=%u audio=%u originalImage=%u\n",profile->version,profile->backend,profile->services,profile->audioFunctions,profile->originalSdkImageLoaded);
    iz_menu_update_init(iz_menu_factory_version);
    bool import=true;
    do {
        menu_reload=false;
        result=run_menu(import);import=false;
        if(iz_menu_trial()&&!menu_reload){
            iz_menu_reject(result?result:0xea12);
            menu_reload=true;
        }
    }while(menu_reload);
    iz_ftp_close();
    iz_pc_close();
    /* The loader request is published only after all module, service and GPU
     * owners have actually closed. Failures above never chainload a child. */
    if(!result&&next_program_ready){
        result=envSetNextLoad(next_program.path,next_program.arguments);
        fprintf(stderr,"IZLAUNCHER_NEXT_COMMITTED rc=0x%08x path=%s\n",result,next_program.path);
    }
    fprintf(stderr,"IZLAUNCHER_RUNTIME_EXIT rc=0x%08x\n",result);
    if(sd_owned)fsdevUnmountDevice("sdmc");
    return (int)result;
}
