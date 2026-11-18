#include "module-folder.h"
/* SD -> owned RW source -> RO RX alias -> native entry. No host callback. */
#include "abi.h"
#include "../relocatable/loader.h"
#include "../instructions/checkpoint.h"
#include <string.h>
#include <strings.h>
#include "sb4-storage-scope.h"
#include "sd-storage-policy.h"
#ifdef __SWITCH2__
#include <stdio.h>
#include <switch/crypto/sha256.h>
#include "iz-file-deck-publisher.h"
#include "monocypher-ed25519.h"
#endif
#ifdef __SWITCH2__
/* 0: not opened, 1: module folder, 2: whole SD, 3: root opened. */
static u32 foldrift_sd_status;
u32 iz_foldrift_sd_status(void){
    return __atomic_load_n(&foldrift_sd_status,__ATOMIC_RELAXED);
}

static int verify_ed25519(const uint8_t public_key[32], const uint8_t signature[64],
                          const uint8_t *message, size_t length){
    return crypto_ed25519_check(signature,public_key,message,length)==0;
}

int iz_foldrift_approve_image(const IzAppHeader *header, const uint8_t *image){
    uint8_t digest[32];
    sha256CalculateHash(digest,image,header->total_bytes);
    if(iz2sWholeSdApproved(header->id,digest,iz2s_file_deck_public_key,
                          iz2s_file_deck_bundled_signature,verify_ed25519))return 1;

    uint8_t signature[64];
    FILE *file=fopen("sdmc:" IZ_SD_ROOT "/foldrift/launcher.iza.sig","rb");
    if(!file)return 0;
    const size_t bytes=fread(signature,1,sizeof signature,file);
    const int tail=fgetc(file);
    const int error=ferror(file);
    const int closed=fclose(file);
    return bytes==sizeof signature && tail==EOF && !error && !closed &&
           iz2sWholeSdApproved(header->id,digest,iz2s_file_deck_public_key,
                               signature,verify_ed25519);
}
#endif

#ifdef IZ_OUNCE_PRELINKED
IzNativeEntry iz_prelinked_entry(u32 abi, const char *id, const void *code, u32 bytes);
IzNativeEntry iz_linked_engine_app(u32 abi,const char *id,const void *code,u32 bytes)
 __attribute__((weak));
u64 iz_linked_engine_entry(IzNativeEntry app,const char *name) __attribute__((weak));
static char prelinked_owner;
#endif
_Static_assert(sizeof(IzImage)<=4096,"Engine ownership state fits one page");
_Static_assert(IZ_GFX_COUNT==F_COUNT-F_CREATE_SHADER,"Public graphics function table order");
#define PTR(p) ((u64)(uintptr_t)(p))
static u32 retain(IzNative*n,u32 rc){n->unknown=1;n->fs->c->poisoned=1;n->fs->c->native_status=rc;return rc;}
static u32 ro(IzNative*n,u32 command,const u64 values[4],u64*out,unsigned count){
 IzContext*c=n->fs->c;_Alignas(4096) u8 message[4096];memset(message,0,sizeof message);u32*p=(void*)message;
 p[0]=4;p[1]=0x80000010u;p[2]=3;p[5]=0xffff8001u;p[8]=0x49434653;p[10]=command;memcpy(message+48,values,32);
 n->unknown=1;u32 rc=(u32)CALL(c,F_SEND,message,4096,c->ro_handle,0,0,0,0,0);if(rc)return retain(n,rc);
 if(!(p[1]&0x80000000u)||p[2]!=1||p[8]!=0x4f434653||((u64)p[3]|((u64)p[4]<<32))!=c->ro_pid||(p[1]&1023)<8+count*2)return retain(n,0xe401);
 if(p[10])return retain(n,p[10]);if(out)memcpy(out,message+48,count*8);n->unknown=0;return 0;
}
static u32 allocate(IzNative*n,u64 bytes,u64*out){
 *out=0;IzContext*c=n->fs->c;u32 rc=(u32)CALL(c,F_ALLOCATE,out,4096,bytes,0,0,0,0,0);if(rc)return *out?retain(n,0xe402):rc;
 if(!*out||*out%4096||*out>=(1ull<<39)||bytes>(1ull<<39)-*out)return retain(n,0xe402);memset((void*)(uintptr_t)*out,0,bytes);return 0;
}
static int path(IzNative*n,const char*name,char*out){
 if(!name||!name[0]||name[0]=='/')return 0;size_t k=0;while(k<512&&name[k])k++;if(k>=512)return 0;
 for(size_t i=0;i<k;){size_t start=i;while(i<k&&name[i]!='/'){unsigned ch=(u8)name[i++];if(ch<32||ch==127||ch=='\\'||ch==':')return 0;}size_t length=i-start;if(!length||(length==1&&name[start]=='.')||(length==2&&name[start]=='.'&&name[start+1]=='.'))return 0;i++;}
 size_t base=strlen(n->root);if(base+1+k>=769)return 0;memcpy(out,n->root,base);out[base++]='/';memcpy(out+base,name,k+1);return 1;
}
static const char *storage_path(IzNative *n, const char *name, char full[769], int file_only);
static int storage_change_path(IzNative *n, const char *name, char full[769], int file_only);

static int module_file_path(IzNativeApp *app, const char *name, char full[769], int writing) {
    IzNative *n = app->owner;
    if (!n) return 0;
    if (!sb4ModuleCompat(app)) return path(n, name, full);
    return writing ? storage_change_path(n, name, full, 1) :
        storage_path(n, name, full, 1) != NULL;
}

static u32 read_file(IzNativeApp*app,const char*name,u64 offset,void*out,u32 bytes,u32*actual){
 if(actual)*actual=0;if(!app||!actual||(!out&&bytes))return 0xe403;IzNative*n=app->owner;char full[769];if(!module_file_path(app,name,full,0))return 0xe403;u32 file=0;u64 size=0;u32 rc=iz_fs_open(n->fs,full,&file,&size);if(rc)return rc;
 if(offset>size)rc=0xe403;else{u64 count=size-offset;if(count>bytes)count=bytes;rc=iz_fs_read(n->fs,file,offset,out,(u32)count);if(!rc)*actual=(u32)count;}
 if(!n->fs->c->poisoned){u32 close=iz_fs_close(n->fs,file);if(!rc)rc=close;}return rc;
}
static u32 write_file(IzNativeApp*app,const char*name,const void*data,u32 bytes){if(!app||!data||!bytes||bytes>16u*1024*1024)return 0xe403;IzNative*n=app->owner;char full[769];if(!module_file_path(app,name,full,1))return 0xe403;return iz_fs_write_file(n->fs,full,data,bytes);}
static IzNative *storage_owner(IzNativeApp *app, u32 capability) {
    const Sb4Storage *storage = sb4ModuleStorage(app);
    if (!storage || !app->owner) return NULL;
    IzNative *n = app->owner;
    if (app != &n->app || !n->fs) return NULL;
    IzContext *c = n->fs->c;
    const Sb4StorageProvider *provider = c->platform_files;
    if (provider && provider->set_scope && provider->get_scope) {
        enum Sb4StorageScope scope = provider->get_scope(n);
        if (scope == SB4_STORAGE_WORKER_SCOPE)
            return sb4StorageWorkerHas(storage, capability) ? n : NULL;
        if (scope == SB4_STORAGE_HOSTED_READ_SCOPE)
            return capability == SB4_STORAGE_WORKER_READ &&
                !__atomic_load_n(&n->worker_done, __ATOMIC_ACQUIRE) &&
                sb4StorageWorkerRead(storage) ? n : NULL;
        if (scope != SB4_STORAGE_ENTRY_SCOPE) return NULL;
    }
    return n->initialized && n->event_active && !n->unknown && !c->poisoned ? n : NULL;
}
static const char *storage_relative_name(const IzNative *n, const char *name) {
    if (!name) return NULL;
    if (!strncasecmp(name, "sdmc:", 5)) {
        name += 5;
        if (*name != '/') return NULL;
    } else if (!strncasecmp(name, "sd:", 3)) {
        name += 3;
        if (*name != '/') return NULL;
    } else if (!strncasecmp(name, "/sd/", 4)) {
        name += 3;
    }
    if (*name != '/') return name;

    const size_t bytes = strlen(n->root);
    if (strncasecmp(name, n->root, bytes)) return NULL;
    name += bytes;
    if (!*name) return name;
    return *name == '/' ? name + 1 : NULL;
}
static int directory_suffix(const char *name, size_t length) {
    return length && (name[length - 1] == '/' ||
        (name[length - 1] == '.' && (length == 1 || name[length - 2] == '/')));
}
static const char *storage_path(IzNative *n, const char *name, char full[769], int file_only) {
    if (iz2sStorageHasWholeSd(n->app7_storage.storage))
        return iz2sWholeSdPath(name, full, file_only);
    name = storage_relative_name(n, name);
    if (!name) return NULL;
    size_t length = 0;
    while (length < 512 && name[length]) ++length;
    if (length == 512 || (file_only && directory_suffix(name, length))) return NULL;

    size_t base = strlen(n->root), used = base;
    memcpy(full, n->root, base);
    size_t cursor = 0;
    while (cursor < length) {
        while (cursor < length && name[cursor] == '/') ++cursor;
        const size_t start = cursor;
        while (cursor < length && name[cursor] != '/') {
            unsigned ch = (u8)name[cursor++];
            if (ch < 32 || ch == 127 || ch == '\\' || ch == ':') return NULL;
        }
        const size_t bytes = cursor - start;
        if (!bytes || (bytes == 1 && name[start] == '.')) continue;
        if ((bytes == 2 && name[start] == '.' && name[start + 1] == '.') ||
            used + 1 + bytes >= 769) return NULL;
        full[used++] = '/';
        memcpy(full + used, name + start, bytes);
        used += bytes;
    }
    full[used] = 0;
    if (file_only && used == base) return NULL;
    return full + base + (used != base);
}
static const char *storage_file_path(IzNative *n, const char *name, char full[769]) {
    return storage_path(n, name, full, 1);
}
static u32 list_sd(IzNativeApp*app,const char*name,Sb4StorageEntry*entries,u32 cap,u32*count){
 if(count)*count=0;
 IzNative*n=storage_owner(app,SB4_STORAGE_WORKER_READ);char full[769];
 if(!n||!entries||!count||!cap||cap>SB4_STORAGE_LIST_MAX||!storage_path(n,name,full,0))return SB4_STORAGE_INVALID;
 return n->fs->c->platform_list_sd(full,entries,cap,count);
}
static u32 size_sd(IzNativeApp*app,const char*name,u64*size){
 if(size)*size=0;
 IzNative*n=storage_owner(app,SB4_STORAGE_WORKER_READ);char full[769];
 if(!n||!size||!storage_file_path(n,name,full))return SB4_STORAGE_INVALID;
 return n->fs->c->platform_sd_size(full,size);
}
static IzNative*files_owner(IzNativeApp*app,u32 capability){
 IzNative*n=storage_owner(app,capability);
 return n&&n->fs->c->platform_files?n:NULL;
}
static const char *relative_sd(IzNativeApp *app, const char *name) {
    IzNative *n = storage_owner(app, SB4_STORAGE_WORKER_READ);
    if (!n || !name) return NULL;
    char full[769];
    if (!storage_path(n, name, full, 0)) return NULL;
    return iz2sStorageHasWholeSd(n->app7_storage.storage) ? name : storage_relative_name(n, name);
}
static u32 stat_sd(IzNativeApp *app, const char *name, Sb4StorageInfo *info) {
    if (!info) return SB4_STORAGE_INVALID;
    *info = (Sb4StorageInfo){0};
    IzNative *n = files_owner(app, SB4_STORAGE_WORKER_READ);
    char full[769];
    if (!n || !storage_path(n, name, full, 0)) return SB4_STORAGE_INVALID;
    const Sb4StorageProvider *provider = n->fs->c->platform_files;
    if (!provider->stat_path) return SB4_STORAGE_INVALID;
    u32 rc = provider->stat_path(full, info);
    if (!rc && !info->directory && directory_suffix(name, strlen(name))) {
        *info = (Sb4StorageInfo){0};
        return SB4_STORAGE_INVALID;
    }
    return rc;
}
static u32 open_sd(IzNativeApp*app,const char*name,u32 mode,u32*token,u64*size){
 if(token)*token=0;if(size)*size=0;
 if(mode!=SB4_STORAGE_OPEN_READ&&mode!=SB4_STORAGE_OPEN_UPDATE)return SB4_STORAGE_INVALID;
 int reading=mode==SB4_STORAGE_OPEN_READ;
 IzNative*n=files_owner(app,reading?SB4_STORAGE_WORKER_READ:SB4_STORAGE_WORKER_WRITE);char full[769];
 if(!n||!(reading?storage_file_path(n,name,full)!=NULL:storage_change_path(n,name,full,1)))return SB4_STORAGE_INVALID;
 return n->fs->c->platform_files->open(n,full,mode,token,size);
}
static u32 read_sd(IzNativeApp*app,u32 token,u64 off,void*data,u32 bytes,u32*actual){
 if(actual)*actual=0;
 IzNative*n=files_owner(app,SB4_STORAGE_WORKER_READ);
 return n?n->fs->c->platform_files->read(n,token,off,data,bytes,actual):SB4_STORAGE_INVALID;
}
static u32 write_sd(IzNativeApp*app,u32 token,u64 off,const void*data,u32 bytes){
 IzNative*n=files_owner(app,SB4_STORAGE_WORKER_WRITE);
 return n?n->fs->c->platform_files->write(n,token,off,data,bytes):SB4_STORAGE_INVALID;
}
static u32 token_size_sd(IzNativeApp*app,u32 token,u64*size){
 if(size)*size=0;
 IzNative*n=files_owner(app,SB4_STORAGE_WORKER_READ);
 return n?n->fs->c->platform_files->size(n,token,size):SB4_STORAGE_INVALID;
}
static u32 resize_sd(IzNativeApp*app,u32 token,u64 size){
 IzNative*n=files_owner(app,SB4_STORAGE_WORKER_WRITE);
 return n?n->fs->c->platform_files->resize(n,token,size):SB4_STORAGE_INVALID;
}
static u32 flush_sd(IzNativeApp*app,u32 token){
 IzNative*n=files_owner(app,SB4_STORAGE_WORKER_WRITE);
 return n?n->fs->c->platform_files->flush(n,token):SB4_STORAGE_INVALID;
}
static u32 close_sd(IzNativeApp*app,u32 token){
 IzNative*n=files_owner(app,SB4_STORAGE_WORKER_READ);
 return n?n->fs->c->platform_files->close(n,token):SB4_STORAGE_INVALID;
}
static u32 create_sd(IzNativeApp*app,const char*name,u64 bytes){
 IzNative*n=files_owner(app,SB4_STORAGE_WORKER_WRITE);char full[769];
 return n&&storage_change_path(n,name,full,1)?n->fs->c->platform_files->create(full,bytes):SB4_STORAGE_INVALID;
}
static u32 mkdir_sd(IzNativeApp*app,const char*name){
 IzNative*n=files_owner(app,SB4_STORAGE_WORKER_WRITE);char full[769];
 return n&&storage_change_path(n,name,full,0)?n->fs->c->platform_files->mkdir(full):SB4_STORAGE_INVALID;
}
static u32 commit_sd(IzNativeApp*app){
 IzNative*n=files_owner(app,SB4_STORAGE_WORKER_WRITE);
 return n?n->fs->c->platform_files->commit():SB4_STORAGE_INVALID;
}
static int storage_change_path(IzNative *n, const char *name, char full[769], int file_only) {
    name = storage_path(n, name, full, file_only);
    if (!name || !*name) return 0;
    if (iz2sStorageHasWholeSd(n->app7_storage.storage)) {
        const char prefix[] = IZ_SD_ROOT "/";
        if (strncasecmp(full, prefix, sizeof prefix-1)) return 1;
        const char *subpath = strchr(full+sizeof prefix-1, '/');
        if (!subpath || !subpath[1]) return 1;
        name = subpath+1;
    }
    // These names at the module root belong to the installer.
    size_t length = strlen(name);
    int companion = length == 77 && !strncasecmp(name, "asset-", 6) &&
        !strcasecmp(name + 70, ".izdata") &&
        strspn(name + 6, "0123456789abcdefABCDEF") == 64;
    int bundle = length == 79 && !strncasecmp(name, "sd-bundle-", 10) &&
        !strcasecmp(name + 74, ".json") &&
        strspn(name + 10, "0123456789abcdefABCDEF") == 64;
    return strcasecmp(name, "launcher.iza") && strcasecmp(name, "launcher.iza.sig") &&
        strcasecmp(name, "sd-bundle.json") &&
        !companion && !bundle && strncasecmp(name, ".install", 8) &&
        strncasecmp(name, ".launcher-", 10) && strncasecmp(name, ".asset-", 7) &&
        strncasecmp(name, ".removed-", 9);
}
static u32 rename_sd(IzNativeApp *app, const char *from, const char *to) {
    IzNative *n = files_owner(app, SB4_STORAGE_WORKER_WRITE);
    char source[769], destination[769];
    if (!n || !storage_change_path(n, from, source, 1) ||
        !storage_change_path(n, to, destination, 1)) return SB4_STORAGE_INVALID;
    const Sb4StorageProvider *provider = n->fs->c->platform_files;
    return provider->rename_file ? provider->rename_file(source, destination) : SB4_STORAGE_INVALID;
}

#ifdef __SWITCH2__
u32 iz_native_publish_ps2_cache(IzNativeApp *app, const char *from, const char *to) {
    static const char cache[] = "saves/armsx2/cache/";
    const Sb4Storage *storage = sb4ModuleStorage(app);
    if (!sb4StorageWorkerWrite(storage) || !app->owner) return SB4_STORAGE_INVALID;
    IzNative *n = app->owner;
    if (app != &n->app || !n->fs || strcmp(n->root, IZ_SD_ROOT "/izps2") ||
        !n->worker_thread || __atomic_load_n(&n->worker_done, __ATOMIC_ACQUIRE))
        return SB4_STORAGE_INVALID;

    char source[769], destination[769];
    const char *staged = storage_path(n, from, source, 1);
    const char *target = storage_path(n, to, destination, 1);
    if (!staged || !target || strncmp(target, cache, sizeof(cache) - 1) ||
        !target[sizeof(cache) - 1])
        return SB4_STORAGE_INVALID;
    const size_t length = strlen(target);
    if (strncmp(staged, target, length) || strncmp(staged + length, ".tmp", 4))
        return SB4_STORAGE_INVALID;
    const char *serial = staged + length + 4;
    if (*serial == '-') ++serial;
    if (!*serial || serial[strspn(serial, "0123456789")])
        return SB4_STORAGE_INVALID;

    return sb4SdReplacePreparedFile(source, destination);
}
#endif

static u32 remove_sd(IzNativeApp *app, const char *name) {
    IzNative *n = files_owner(app, SB4_STORAGE_WORKER_WRITE);
    char full[769];
    if (!n || !storage_change_path(n, name, full, 1)) return SB4_STORAGE_INVALID;
    const Sb4StorageProvider *provider = n->fs->c->platform_files;
    return provider->remove_file ? provider->remove_file(full) : SB4_STORAGE_INVALID;
}
static u32 open_dir_sd(IzNativeApp *app, const char *name, u32 *token) {
    if (token) *token = 0;
    IzNative *n = files_owner(app, SB4_STORAGE_WORKER_READ);
    char full[769];
    if (!n || !token || !storage_path(n, name, full, 0)) return SB4_STORAGE_INVALID;
    const Sb4StorageProvider *provider = n->fs->c->platform_files;
    u32 rc=provider->open_dir ? provider->open_dir(n,full,token) : SB4_STORAGE_INVALID;
#ifdef __SWITCH2__
    if(!rc && !strcmp(full,"/") && iz2sStorageHasWholeSd(sb4ModuleStorage(app)))
        __atomic_store_n(&foldrift_sd_status,3u,__ATOMIC_RELAXED);
#endif
    return rc;
}
static u32 read_dir_sd(IzNativeApp *app, u32 token, Sb4StorageEntry *entries, u32 cap, u32 *count) {
    if (count) *count = 0;
    IzNative *n = files_owner(app, SB4_STORAGE_WORKER_READ);
    if (!n) return SB4_STORAGE_INVALID;
    const Sb4StorageProvider *provider = n->fs->c->platform_files;
    return provider->read_dir ? provider->read_dir(n, token, entries, cap, count) : SB4_STORAGE_INVALID;
}
static u32 close_dir_sd(IzNativeApp *app, u32 token) {
    IzNative *n = files_owner(app, SB4_STORAGE_WORKER_READ);
    if (!n) return SB4_STORAGE_INVALID;
    const Sb4StorageProvider *provider = n->fs->c->platform_files;
    return provider->close_dir ? provider->close_dir(n, token) : SB4_STORAGE_INVALID;
}
static const Sb4StorageDirectories sd_directories={open_dir_sd,read_dir_sd,close_dir_sd};
static const Sb4StorageFiles sd_files={
 SB4_STORAGE_VERSION,sizeof(Sb4StorageFiles),open_sd,read_sd,write_sd,token_size_sd,resize_sd,
 flush_sd,close_sd,create_sd,mkdir_sd,commit_sd,rename_sd,remove_sd,
};
static const Sb4Storage sd_metadata={
 SB4_STORAGE_VERSION,sizeof(Sb4Storage),list_sd,size_sd,NULL,0,NULL,NULL,relative_sd,
};
static const Sb4Storage sd_storage={
 SB4_STORAGE_VERSION,sizeof(Sb4Storage),list_sd,size_sd,&sd_files,0,stat_sd,&sd_directories,relative_sd,
};
#ifdef __SWITCH2__
static const Sb4Storage sd_whole_storage={
 SB4_STORAGE_VERSION,sizeof(Sb4Storage),list_sd,size_sd,&sd_files,
 IZ2S_STORAGE_WHOLE_SD,stat_sd,&sd_directories,relative_sd,
};
#endif
static const Sb4Storage sd_worker_storage={
 SB4_STORAGE_VERSION,sizeof(Sb4Storage),list_sd,size_sd,&sd_files,
 SB4_STORAGE_WORKER_READ|SB4_STORAGE_WORKER_WRITE,stat_sd,&sd_directories,relative_sd,
};
static const Sb4StorageProvider *scoped_storage(IzNative *n) {
    if (!sb4ModuleStorage(&n->app)) return NULL;
    const Sb4StorageProvider *provider = n->fs->c->platform_files;
    return provider && provider->set_scope && provider->get_scope ? provider : NULL;
}
static void request_exit(IzNativeApp*app){if(app&&app->controls)*(u32*)((u8*)app->controls+1016)=1;}
static u64 buttons(IzNativeApp*app){return app&&app->controls?*(u64*)((u8*)app->controls+8):0;}
static IzNative*image_owner(IzNativeApp*app){
 if(!app||!app->owner||!((app->version==3&&app->bytes==sizeof(IzNativeApp3))||(app->version==4&&app->bytes==sizeof(IzNativeApp4))||(app->version==5&&app->bytes==sizeof(IzNativeApp5))||(app->version==6&&app->bytes==sizeof(IzNativeApp6))||(sb4ModuleCompat(app)&&(app->flags&IZ_APP_ENGINE_IMAGE))))return 0;IzNative*n=app->owner;
 if(app!=&n->app||!n->initialized||!n->event_active||n->unknown||n->fs->c->poisoned)return 0;return n;
}
static u32 image_lock(IzNative*n){u32 expected=0;return __atomic_compare_exchange_n(&n->image_busy,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)?0:0xe619;}
static u32 load_image(IzNativeApp*app,const char*name,IzNativeImage*out){
 if(!out)return 0xe40b;IzNative*n=image_owner(app);char full[769];if(!n||!path(n,name,full))return 0xe40b;
 u32 rc=image_lock(n);if(rc)return rc;
 if(n->worker_state||n->image_token||n->image_generation==0xffffffffu||n->fs->c->native_launches>0xffffffffu){rc=0xe619;goto done;}
 memset(out,0,sizeof *out);
 if(!n->image_state){rc=allocate(n,4096,&n->image_state);if(rc)goto done;}
 IzImage*image=(void*)(uintptr_t)n->image_state;rc=iz_image_load(n->fs,image,full,0,0,0);if(rc)goto done;
 n->image_token=(n->fs->c->native_launches<<32)|++n->image_generation;
 out->version=1;out->bytes=sizeof *out;out->code=(void*)(uintptr_t)image->destination;out->code_bytes=image->header.memory_bytes[0];out->data=(void*)(uintptr_t)image->data;out->data_bytes=image->header.memory_bytes[1];out->entry=(void*)(uintptr_t)image->entry;out->spawn=(void*)(uintptr_t)image->spawn;out->token=n->image_token;
done:__atomic_store_n(&n->image_busy,0,__ATOMIC_RELEASE);return rc;
}
static u32 unload_image(IzNativeApp*app,u64 token){
 IzNative*n=image_owner(app);if(!n||!token||!n->image_state||n->image_async||n->worker_state||n->presentation_owned||token!=n->image_token)return 0xe40d;
 u32 rc=image_lock(n);if(rc)return rc;rc=iz_image_unload((void*)(uintptr_t)n->image_state);if(!rc)n->image_token=0;__atomic_store_n(&n->image_busy,0,__ATOMIC_RELEASE);return rc;
}
static void image_receipt(IzNative*n,IzNativeImage*out){
 IzImage*image=(void*)(uintptr_t)n->image_state;memset(out,0,sizeof *out);if(!image->ready)return;
#ifdef IZ_OUNCE_PRELINKED
 if(image->prelinked){
  *out=(IzNativeImage){.version=1,.bytes=sizeof *out,.code=(void*)(uintptr_t)image->entry,
    .code_bytes=4,.data=(void*)(uintptr_t)n->data,.data_bytes=n->data_bytes,
    .entry=(void*)(uintptr_t)image->entry,.token=n->image_token};
  return;
 }
#endif
 out->version=1;out->bytes=sizeof *out;out->code=(void*)(uintptr_t)image->destination;out->code_bytes=image->header.memory_bytes[0];out->data=(void*)(uintptr_t)image->data;out->data_bytes=image->header.memory_bytes[1];out->entry=(void*)(uintptr_t)image->entry;out->spawn=(void*)(uintptr_t)image->spawn;out->token=n->image_token;
}
static u32 image_begin_async(IzNativeApp*app,const char*name,u64*ticket){
 IzNative*n=image_owner(app);char full[769];if(!n||n->stopping||!(app->flags&IZ_APP_COOPERATIVE)||!ticket||!path(n,name,full))return 0xe40b;u32 rc=image_lock(n);if(rc)return rc;
 if(n->worker_state||n->image_token||n->image_generation==0xffffffffu||n->fs->c->native_launches>0xffffffffu){rc=0xe619;goto done;}
 if(!n->image_state){rc=allocate(n,4096,&n->image_state);if(rc)goto done;}
 IzImage*image=(void*)(uintptr_t)n->image_state;
#ifdef IZ_OUNCE_PRELINKED
 if(n->platform_code==&prelinked_owner&&iz_linked_engine_entry){
  u64 entry=iz_linked_engine_entry((IzNativeEntry)(uintptr_t)n->destination,name);
  if(entry){
   memset(image,0,sizeof *image);image->prelinked=1;image->ready=1;image->entry=entry;
  }else{
   rc=0xe40b;goto done;
  }
 }else
#endif
 {rc=iz_image_begin(n->fs,image,full,0);if(rc)goto done;}
 n->image_token=(n->fs->c->native_launches<<32)|++n->image_generation;n->image_async=1;*ticket=n->image_token;
 done:__atomic_store_n(&n->image_busy,0,__ATOMIC_RELEASE);return rc;
}
static u32 image_step_async(IzNativeApp*app,u64 ticket,u32 budget,IzNativeImageProgress*out,IzNativeImage*receipt){
 IzNative*n=image_owner(app);if(!n||!(app->flags&IZ_APP_COOPERATIVE)||!out||!receipt||!ticket||!n->image_async||ticket!=n->image_token)return 0xe40d;if(!budget||budget>64)return 0xe618;u32 rc=image_lock(n);if(rc)return rc;
 IzImage*image=(void*)(uintptr_t)n->image_state;u32 done=0;memset(out,0,sizeof *out);
#ifdef IZ_OUNCE_PRELINKED
 if(image->prelinked){
  out->version=1;out->bytes=sizeof *out;out->done=1;
  out->state=image->ready?IZ_NATIVE_IMAGE_READY:IZ_NATIVE_IMAGE_CLOSED;
  image_receipt(n,receipt);
  if(!image->ready){memset(image,0,sizeof *image);n->image_token=0;n->image_async=0;}
  __atomic_store_n(&n->image_busy,0,__ATOMIC_RELEASE);return 0;
 }
#endif
 rc=iz_image_step(image,budget,&done);out->version=1;out->bytes=sizeof *out;out->stage=image->stage;out->error=image->error;out->done=done;
 out->state=image->pending?IZ_NATIVE_IMAGE_RETAINED:image->operation==1?IZ_NATIVE_IMAGE_LOADING:image->operation==2?IZ_NATIVE_IMAGE_CLOSING:image->ready?IZ_NATIVE_IMAGE_READY:image->error?IZ_NATIVE_IMAGE_FAILED:IZ_NATIVE_IMAGE_CLOSED;
 out->read_bytes=image->read_bytes;out->total_bytes=sizeof(IzRelocHeader)+(u64)image->header.file_bytes[0]+image->header.file_bytes[1]+2*(u64)image->header.record_count*sizeof(IzRelocRecord);out->cleared_bytes=image->cleared_bytes;out->mapped_pages=image->mapped;out->total_pages=image->pages;out->work_units=image->units;image_receipt(n,receipt);
 if(done&&(out->state==IZ_NATIVE_IMAGE_CLOSED||out->state==IZ_NATIVE_IMAGE_FAILED)){n->image_token=0;n->image_async=0;}if(rc)n->fs->c->native_status=rc;__atomic_store_n(&n->image_busy,0,__ATOMIC_RELEASE);return rc;
}
static u32 image_release_async(IzNativeApp*app,u64 ticket){
 IzNative*n=image_owner(app);if(!n||!(app->flags&IZ_APP_COOPERATIVE)||!ticket||!n->image_async||ticket!=n->image_token||n->worker_state||n->presentation_owned)return 0xe40d;u32 rc=image_lock(n);if(rc)return rc;
 IzImage*image=(void*)(uintptr_t)n->image_state;
#ifdef IZ_OUNCE_PRELINKED
 if(image->prelinked){image->ready=0;image->entry=0;}
 else
#endif
 rc=iz_image_close_begin(image,0);
 __atomic_store_n(&n->image_busy,0,__ATOMIC_RELEASE);return rc;
}
/* Workers use only the current admitted engine entry. The supervisor retains
 * code, context and borrowed capabilities until a nonblocking wait and JOIN. */
static u64 worker_call(IzNative*n,unsigned function,u64 a,u64 b,u64 d,u64 e){
 IzFunction f=(IzFunction)(uintptr_t)n->fs->c->worker_functions[function];return f(a,b,d,e,0,0,0,0);
}
static u64 worker_entry(void*opaque){
 IzNative*n=opaque;
 const Sb4StorageProvider *provider=sb4StorageWorkerRead(sb4ModuleStorage(&n->app))?scoped_storage(n):NULL;
 if(provider)provider->set_scope(n,SB4_STORAGE_WORKER_SCOPE);
 u64 result=((u64(*)(void*,u64))(uintptr_t)n->worker_entry)((void*)(uintptr_t)n->worker_context,0);
 if(provider)provider->set_scope(NULL,SB4_STORAGE_NO_SCOPE);
 n->worker_result=result;__atomic_store_n(&n->worker_done,1,__ATOMIC_RELEASE);return 0;
}
static int worker_context(IzNative*n,void*context,u64 bytes){
 u64 p=PTR(context);if(!p||p%8||!bytes||bytes>128u*1024*1024)return 0;
 if(p>=n->data&&p-n->data<n->data_bytes&&bytes<=n->data_bytes-(p-n->data))return 1;
 IzImage*image=(void*)(uintptr_t)n->image_state;return image&&p>=image->data&&p-image->data<image->header.memory_bytes[1]&&bytes<=image->header.memory_bytes[1]-(p-image->data);
}
static u32 worker_begin(IzNativeApp*app,u64 image_token,void*context,u64 bytes,u64*ticket){
 IzNative*n=image_owner(app);if(!n||!(app->flags&IZ_APP_WORKER)||!ticket||!image_token||image_token!=n->image_token||!n->image_state||n->stopping)return 0xe620;
 if((app->flags&IZ_APP_PRESENTATION)&&!n->presentation_owned)return 0xe646;
 if(n->worker_state||n->worker_thread||n->worker_generation==0xffffffffu)return 0xe621;
 IzImage*image=(void*)(uintptr_t)n->image_state;if(!image->ready||image->operation||image->pending||image->busy||!image->entry||!worker_context(n,context,bytes))return 0xe620;
 n->worker_entry=image->entry;n->worker_context=PTR(context);n->worker_context_bytes=bytes;n->worker_result=n->worker_return=0;__atomic_store_n(&n->worker_done,0,__ATOMIC_RELEASE);
 /* create may start the child before returning. Only a known rejection with
  * a canonical absent output is safe to unwind; never retry an uncertain one. */
 n->worker_state=IZ_WORKER_RUNNING;n->worker_thread=0;
 u64 rc=worker_call(n,0,PTR(&n->worker_thread),0,PTR(worker_entry),PTR(n));
 if(rc){if(n->worker_thread)return retain(n,IZ_NATIVE_WORKER_RETAINED);n->worker_state=0;n->worker_entry=n->worker_context=n->worker_context_bytes=0;return 0xe623;}
 if(!n->worker_thread)return retain(n,IZ_NATIVE_WORKER_RETAINED);
 n->worker_token=(n->fs->c->native_launches<<32)|++n->worker_generation;*ticket=n->worker_token;return 0;
}
static u32 worker_poll(IzNativeApp*app,u64 ticket,IzNativeWorkerProgress*out){
 IzNative*n=image_owner(app);if(!n||!(app->flags&IZ_APP_WORKER)||!out||!ticket||ticket!=n->worker_token||!n->worker_state||!n->worker_thread)return 0xe624;
 if(n->worker_state==IZ_WORKER_RUNNING){u64 rc=worker_call(n,1,n->worker_thread,0,0,0);if(rc>1)return retain(n,0xe625);if(rc){if(!__atomic_load_n(&n->worker_done,__ATOMIC_ACQUIRE))return retain(n,0xe626);n->worker_state=IZ_WORKER_FINISHED;}}
 memset(out,0,sizeof *out);out->version=1;out->bytes=sizeof *out;out->state=n->worker_state;if(n->worker_state==IZ_WORKER_FINISHED)out->result=n->worker_result;return 0;
}
static u32 worker_join(IzNativeApp*app,u64 ticket,u64*result){
 IzNative*n=image_owner(app);if(!n||!(app->flags&IZ_APP_WORKER)||!result||!ticket||ticket!=n->worker_token||!n->worker_state||!n->worker_thread)return 0xe624;
 if(n->worker_state!=IZ_WORKER_FINISHED)return 0xe627;
 if(worker_call(n,2,n->worker_thread,PTR(&n->worker_return),0,0)||n->worker_return)return retain(n,0xe628);
 *result=n->worker_result;n->worker_thread=n->worker_token=n->worker_entry=n->worker_context=n->worker_context_bytes=0;n->worker_state=0;return 0;
}

static u32 presentation_acquire(IzNativeApp*app,u64*ticket){
 IzNative*n=image_owner(app);if(!n||!(app->flags&IZ_APP_PRESENTATION)||!ticket||n->stopping||n->presentation_owned||n->worker_state||n->worker_thread||!n->image_state||!n->image_token||n->presentation_generation==0xffffffffu||n->fs->c->native_launches>0x7fffffffu)return 0xe640;
 IzImage*image=(void*)(uintptr_t)n->image_state;if(!image->ready||image->operation||image->pending||image->busy)return 0xe640;
 n->presentation_token=0x8000000000000000ull|(n->fs->c->native_launches<<32)|++n->presentation_generation;n->presentation_owned=1;*ticket=n->presentation_token;return 0;
}
static u32 presentation_release(IzNativeApp*app,u64 ticket){
 IzNative*n=image_owner(app);if(!n||!(app->flags&IZ_APP_PRESENTATION)||!ticket||ticket!=n->presentation_token||!n->presentation_owned||n->worker_state||n->worker_thread)return 0xe642;
 IzContext*c=n->fs->c;u32 rc=(u32)CALL(c,F_QUEUE_IDLE,c->queue,0,0,0,0,0,0,0);if(rc)return retain(n,0xe643);
 n->presentation_owned=0;n->presentation_token=0;return 0;
}
static IzNative *presentation_setup_owner(IzNativeApp *app,u64 ticket){
 IzNative*n=sb4ModuleCompat(app)?image_owner(app):NULL;
 if(!n||!(app->flags&IZ_APP_PRESENTATION)||!ticket||!n->presentation_owned||
    ticket!=n->presentation_token||n->worker_thread||n->worker_state||
    n!=&n->fs->c->owner.native)
  return NULL;
 return n;
}
bool iz_native_can_setup_presentation(IzNativeApp *app,u64 ticket,const IzContext *context){
 if(!context||app!=&context->owner.native.app)return false;
 IzNative*n=presentation_setup_owner(app,ticket);
 return n&&n->fs->c==context;
}
u32 iz_native_get_presentation_target(IzNativeApp *app,u64 ticket,const u64 *draw,Sb4PresentationTarget *target){
 if(!target||!draw)return SB4_PRESENTATION_UNAVAILABLE;
 IzNative*n=presentation_setup_owner(app,ticket);
 if(!n||!n->app2.private_memory||!n->app2.private_bytes)
  return SB4_PRESENTATION_UNAVAILABLE;
 IzContext*c=n->fs->c;
 if(!c->acquired||
    !c->image_count||c->image_count>SB4_PRESENTATION_IMAGES)
  return SB4_PRESENTATION_UNAVAILABLE;
 for(unsigned i=0;i<c->image_count;i++)
  if(!c->semaphores[i]||!draw[i])return SB4_PRESENTATION_UNAVAILABLE;
 *target=(Sb4PresentationTarget){.version=1,.bytes=sizeof(*target),.image_count=(u32)c->image_count,
      .acquire=c->acquired,.mapped=n->app2.private_memory,.mapped_bytes=n->app2.private_bytes};
 memcpy(target->commands,draw,c->image_count*sizeof(u64));
 memcpy(target->complete,c->semaphores,c->image_count*sizeof(u64));
 return 0;
}

u32 iz_native_event(IzNative*n,u32 event){if(!n->destination||n->mapped!=n->pages||n->unknown||n->event_active)return 0xe404;
 IzContext*c=n->fs->c;
 n->event_active=event;
 const Sb4StorageProvider *provider=scoped_storage(n);
 if(provider)provider->set_scope(n,SB4_STORAGE_ENTRY_SCOPE);
 u32 result=((IzNativeEntry)(uintptr_t)n->destination)(&n->app,event);
 if(provider)provider->set_scope(NULL,SB4_STORAGE_NO_SCOPE);
 n->event_active=0;n->app.result=result;if(c->poisoned)return retain(n,0xe405);return result;}
u32 iz_native_unload(IzNative*n){
 if(!n->fs)return 0;IzContext*c=n->fs->c;if(n->unknown||c->poisoned)return 0xe406;
 if(n->initialized){n->stopping=1;u32 rc=iz_native_event(n,IZ_NATIVE_STOP);if(rc==IZ_NATIVE_STOP_PENDING&&n->app.version>=4)return rc;if(rc)return retain(n,rc);n->initialized=0;}
 if(n->worker_state||n->worker_thread)return retain(n,0xe62a);
 if(n->presentation_owned||n->presentation_token)return retain(n,0xe645);
 if(n->app.version==SB4_NATIVE_ABI&&c->platform_restore_hid){u32 rc=c->platform_restore_hid();if(rc)return retain(n,rc);}
 if(c->platform_files&&sb4StorageFiles(sb4ModuleStorage(&n->app)))c->platform_files->retire(n);
 if(n->image_state){IzImage*image=(void*)(uintptr_t)n->image_state;if(n->image_busy||n->image_token||image->busy||image->pending||image->ready||image->source||image->data||image->mapped||image->file||image->operation)return retain(n,0xe40e);CALL(c,F_FREE,n->image_state,0,0,0,0,0,0,0);n->image_state=0;}
 if(n->platform_code){
  u32 rc=0;
#ifdef IZ_OUNCE_PRELINKED
  if(n->platform_code!=&prelinked_owner)
#endif
   rc=c->platform_unmap?c->platform_unmap(n->platform_code):0xe406;
  if(rc)return retain(n,rc);n->platform_code=0;n->mapped=0;
 }
 while(n->mapped){u64 page=n->mapped-1,values[]={c->pid,n->source+page*4096,n->destination+page*4096,4096};u32 rc=ro(n,0x524f0013,values,0,0);if(rc)return rc;n->mapped--;}
 if(n->source)CALL(c,F_FREE,n->source,0,0,0,0,0,0,0);if(n->data)CALL(c,F_FREE,n->data,0,0,0,0,0,0,0);c->native_data=0;c->native_data_bytes=0;memset(n,0,sizeof *n);return 0;
}
static u64 ticks(IzNativeApp*app){IzNative*n=app->owner;IzContext*c=n->fs->c;return CALL(c,F_TICK,0,0,0,0,0,0,0,0)-c->checkpoint_time_bias;}
static void sleep_ns(IzNativeApp*app,u64 ns){IzNative*n=app->owner;CALL(n->fs->c,F_SLEEP,ns,0,0,0,0,0,0,0);}
u32 iz_native_compatible(IzContext*c,const IzAppHeader*h){
 const int compat=(h->flags&IZ_APP_SB4_COMPAT)!=0;
 if(compat&&(!c->platform_map||!sb4CompatValid(c->sb4_compat)))return 0xe64b;
 if(!(h->flags&IZ_APP_NATIVE))return 0xe407;
 if(c->platform_map){
  if(!c->platform_unmap || ((h->flags&IZ_APP_ENGINE_IMAGE)&&(!c->platform_reserve||!c->platform_publish)))return 0xe64a;
 }else if(!c->ro_handle||!c->pid||!c->ro_pid)return 0xe407;
 if(h->flags&IZ_APP_WORKER){
  if(!compat&&(!c->pid||!c->filesystem||!c->sdk||!c->ro_pid||!c->ro_handle))return 0xe62b;
  if(!c->pid||!c->filesystem)return compat?0xe64c:0xe62b;
  for(unsigned i=0;i<3;i++)if(!c->worker_functions[i])return compat?0xe64c:0xe62b;
 }
 if(h->flags&IZ_APP_PRESENTATION){
  if((!c->platform_map&&!c->graphics_vulkan)||!c->graphics_instance||!c->graphics_physical||!c->device||!c->queue||!c->swapchain||!c->command_pool||!c->image_count||c->image_count>8)return 0xe644;
  for(unsigned i=F_CREATE_SHADER;i<F_COUNT;i++)if(!c->functions[i])return 0xe644;
  for(unsigned i=0;i<c->image_count;i++)if(!c->images[i]||!c->views[i])return 0xe644;
  u32 types=0,heaps=0;memcpy(&types,c->graphics_memory_properties,4);memcpy(&heaps,c->graphics_memory_properties+260,4);if(!types||types>32||!heaps||heaps>16)return 0xe644;
 }
 if(h->flags&IZ_APP_AUDIO_OUT)for(unsigned i=0;i<IZ_AUDIO_FUNCTIONS;i++)if(!c->audio_functions[i])return 0xe40a;
 return 0;
}
u32 iz_native_load_saved_with_storage(IzFs*fs,const IzAppHeader*h,const u8*image,IzNative*n,void*private_memory,u64 private_bytes,const void*saved,int whole_sd){
#ifndef __SWITCH2__
 (void)whole_sd;
#endif
 if(n->source||n->data||n->mapped||n->image_state)return 0xe407;
 IzContext*c=fs->c;u32 rc=iz_native_compatible(c,h);if(rc)return rc;
 const int compat=(h->flags&IZ_APP_SB4_COMPAT)!=0;
 const u32 abi=compat?SB4_NATIVE_ABI:(h->flags&IZ_APP_PRESENTATION)?6:(h->flags&IZ_APP_WORKER)?5:
               (h->flags&IZ_APP_COOPERATIVE)?4:(h->flags&IZ_APP_ENGINE_IMAGE)?3:h->version==2?2:1;
 unsigned code=h->version==2?SEC_NATIVE_CODE:SEC_BEGIN,initial=h->version==2?SEC_NATIVE_DATA:SEC_AFTER;
 memset(n,0,sizeof *n);n->fs=fs;
 n->pages=((u64)h->section[code].bytes+4095)/4096;n->data_bytes=h->reserved[0];
 rc=allocate(n,n->pages*4096,&n->source);if(rc)return rc;rc=allocate(n,n->data_bytes,&n->data);if(rc){iz_native_unload(n);return rc;}
 memcpy((void*)(uintptr_t)n->source,image+h->section[code].offset,h->section[code].bytes);if(h->section[initial].bytes)memcpy((void*)(uintptr_t)n->data,image+h->section[initial].offset,h->section[initial].bytes);
 if(saved){u32*program=(void*)(uintptr_t)n->data;u64 at=(u64)program[3]+IZ_INSTRUCTION_MAILBOX;if(h->version!=2||program[0]!=0x434c5a49u||at>n->data_bytes||sizeof(IzInstructionSaved)>n->data_bytes-at)return 0xe732;memcpy((u8*)(uintptr_t)n->data+at,saved,sizeof(IzInstructionSaved));}
#ifdef IZ_OUNCE_PRELINKED
 IzNativeEntry prelinked=NULL;
 if(c->platform_map){
  const void*payload=image+h->section[code].offset;
  if(!(h->flags&IZ_APP_ENGINE_IMAGE))
   prelinked=iz_prelinked_entry(abi,h->id,payload,h->section[code].bytes);
  else if((h->flags&IZ_APP_COOPERATIVE)&&abi==SB4_NATIVE_ABI&&iz_linked_engine_app&&iz_linked_engine_entry)
   prelinked=iz_linked_engine_app(abi,h->id,payload,h->section[code].bytes);
 }
 if(prelinked){
  n->destination=PTR(prelinked);
  n->platform_code=&prelinked_owner;
  n->mapped=n->pages;
 }else
#endif
 if(c->platform_map){
  rc=c->platform_map((const void*)(uintptr_t)n->source,n->pages*4096,&n->destination,&n->platform_code);
  if(rc){if(n->platform_code)return retain(n,rc);iz_native_unload(n);return rc;}
  if(!n->platform_code||!n->destination||n->destination%4096)return retain(n,0xe408);
  n->mapped=n->pages;
 }else{
 u64 values[]={c->pid,0,0,0},info[13];rc=ro(n,0x524f0010,values,info,13);if(rc)return rc;n->destination=info[11];
 if(!n->destination||n->destination%4096||n->destination>=(1ull<<39)||n->pages*4096>(1ull<<39)-n->destination)return retain(n,0xe408);
 values[1]=n->destination;rc=ro(n,0x524f0010,values,info,13);if(rc)return rc;
 if(info[3]||info[6]||(u32)info[7]||info[4]>n->destination||info[5]<n->pages*4096||n->destination-info[4]>info[5]-n->pages*4096)return retain(n,0xe409);
 for(;n->mapped<n->pages;n->mapped++){u64 map[]={c->pid,n->source+n->mapped*4096,n->destination+n->mapped*4096,4096};rc=ro(n,0x524f0012,map,0,0);if(rc)return rc;}
 }
 IzNativeApp*app=&n->app;app->magic=0x414e5a49;app->version=abi;app->bytes=compat?sizeof n->app7:abi==6?sizeof n->app6:abi==5?sizeof n->app5:abi==4?sizeof n->app4:abi==3?sizeof n->app3:abi==2?sizeof n->app2:sizeof *app;app->flags=(abi>=3?h->flags:0)|(c->ram_only?IZ_NATIVE_SOURCE_RAM:0);app->data=(void*)(uintptr_t)n->data;app->data_bytes=n->data_bytes;app->controls=(void*)(uintptr_t)c->controls;app->controls_bytes=c->controls_bytes;app->owner=n;app->read_file=read_file;app->write_file=write_file;app->request_exit=request_exit;app->buttons=buttons;app->tick_hz=c->tick_hz;n->app2.private_memory=private_memory;n->app2.private_bytes=private_bytes;n->app2.ticks=ticks;n->app2.sleep=sleep_ns;
 if(h->flags&IZ_APP_ENGINE_IMAGE){n->app3.load_image=load_image;n->app3.unload_image=unload_image;}if(h->flags&IZ_APP_COOPERATIVE){n->app4.image_begin=image_begin_async;n->app4.image_step=image_step_async;n->app4.image_release=image_release_async;}
 if(compat||(h->flags&IZ_APP_WORKER)){
  n->environment=(IzNativeEnvironment){1,sizeof n->environment,compat?0:c->sdk,(!compat&&c->sdk)?6164480:0,c->pid,compat?0:c->ro_pid,compat?0:c->ro_handle,c->filesystem,c->tick_hz};
  n->app5.environment=&n->environment;
 }
 if(h->flags&IZ_APP_WORKER){n->app5.worker_begin=worker_begin;n->app5.worker_poll=worker_poll;n->app5.worker_join=worker_join;}
 if(compat){
  n->app7.sb4=c->sb4_compat;
  if(!c->ram_only&&c->platform_list_sd&&c->platform_sd_size){
   const Sb4StorageProvider *provider=c->platform_files;
   n->app7_storage.storage=!provider?&sd_metadata:
    provider->set_scope&&provider->get_scope&&(h->flags&IZ_APP_WORKER)?&sd_worker_storage:&sd_storage;
#ifdef __SWITCH2__
   if(provider && !strcmp(h->id,"iz-file-deck") && !(h->flags&IZ_APP_ASSETS)){
    __atomic_store_n(&foldrift_sd_status,whole_sd?2u:1u,__ATOMIC_RELAXED);
    if(whole_sd)n->app7_storage.storage=&sd_whole_storage;
   }
#endif
   app->bytes=sizeof n->app7_storage;
  }
 }
 if(h->flags&IZ_APP_PRESENTATION){
  IzNativeGraphics*g=&n->graphics;g->version=1;g->bytes=sizeof *g;g->function_count=IZ_GFX_COUNT;g->image_count=(u32)c->image_count;g->vulkan=compat?0:c->graphics_vulkan;g->instance=c->graphics_instance;g->physical=c->graphics_physical;g->device=c->device;g->queue=c->queue;g->swapchain=c->swapchain;g->command_pool=c->command_pool;
  memcpy(g->images,c->images,sizeof g->images);memcpy(g->views,c->views,sizeof g->views);memcpy(g->functions,c->functions+F_CREATE_SHADER,sizeof g->functions);memcpy(g->memory_properties,c->graphics_memory_properties,sizeof g->memory_properties);
  n->app6.graphics=g;n->app6.presentation_acquire=presentation_acquire;n->app6.presentation_release=presentation_release;
 }
 if(h->flags&IZ_APP_AUDIO_OUT){n->audio.version=1;n->audio.bytes=sizeof n->audio;for(unsigned i=0;i<IZ_AUDIO_FUNCTIONS;i++)n->audio.functions[i]=(IzNativeAudioFunction)(uintptr_t)c->audio_functions[i];n->app2.audio=&n->audio;}
 size_t root=sizeof(IZ_SD_ROOT)-1;memcpy(n->root,IZ_SD_ROOT,root);n->root[root++]='/';const char *folder=iz_module_folder(h->id);memcpy(n->root+root,folder,strlen(folder)+1);
 c->native_data=n->data;c->native_data_bytes=n->data_bytes;c->native_code_reads+=h->section[code].bytes;c->native_launches++;n->initialized=1;rc=iz_native_event(n,IZ_NATIVE_INIT);c->native_status=rc;return rc;
}
u32 iz_native_load_saved(IzFs*fs,const IzAppHeader*h,const u8*image,IzNative*n,void*p,u64 bytes,const void*saved){
 return iz_native_load_saved_with_storage(fs,h,image,n,p,bytes,saved,0);
}
u32 iz_native_load(IzFs*fs,const IzAppHeader*h,const u8*image,IzNative*n,void*p,u64 bytes){return iz_native_load_saved(fs,h,image,n,p,bytes,0);}
