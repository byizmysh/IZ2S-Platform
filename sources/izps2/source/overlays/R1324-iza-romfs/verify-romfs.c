#include "ps2_resource_romfs.h"
#include "ps2_sb4_fs.h"
#include <sb4/native-app.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char izps2_builtin_resources[];

static unsigned sd_opened;
void izps2_boot_stage(IzNativeApp *app, const char *line, uint32_t length) {
    (void)app;
    fwrite(line,1,length-1,stdout);
}
static Sb4CFunction resolve_c(const char *n) { (void)n; return NULL; }
static IzServiceFunction service(uint32_t m,uint32_t o) { (void)m; (void)o; return NULL; }
static uint32_t allocate(void **out,size_t a,size_t n) { (void)out; (void)a; (void)n; return 1; }
static void release(void *p) { (void)p; }
static uint64_t ticks(void) { return 1; }
static void sleep_ns(uint64_t n) { (void)n; }
static uint32_t list_sd(IzNativeApp *app,const char *name,Sb4StorageEntry *entries,uint32_t cap,uint32_t *count) {
    (void)app; (void)name; (void)entries; (void)cap; *count=0; return 0x202;
}
static uint32_t path_size(IzNativeApp *app,const char *name,uint64_t *size) {
    (void)app; (void)name; *size=0; return 0x202;
}
static uint32_t open_sd(IzNativeApp *app,const char *name,uint32_t mode,uint32_t *token,uint64_t *size) {
    (void)app; (void)name; (void)mode; *token=0; *size=0; ++sd_opened; return 0x202;
}
static uint32_t read_sd(IzNativeApp *app,uint32_t token,uint64_t offset,void *buf,uint32_t bytes,uint32_t *actual) {
    (void)app; (void)token; (void)offset; (void)buf; (void)bytes; (void)actual; abort();
}
static uint32_t write_sd(IzNativeApp *app,uint32_t token,uint64_t offset,const void *buf,uint32_t bytes) {
    (void)app; (void)token; (void)offset; (void)buf; (void)bytes; abort();
}
static uint32_t get_size(IzNativeApp *app,uint32_t token,uint64_t *size) {
    (void)app; (void)token; (void)size; abort();
}
static uint32_t resize(IzNativeApp *app,uint32_t token,uint64_t size) {
    (void)app; (void)token; (void)size; abort();
}
static uint32_t token_op(IzNativeApp *app,uint32_t token) { (void)app; (void)token; abort(); }
static uint32_t create(IzNativeApp *app,const char *name,uint64_t size) { (void)app; (void)name; (void)size; abort(); }
static uint32_t mkdir(IzNativeApp *app,const char *name) { (void)app; (void)name; abort(); }
static uint32_t commit(IzNativeApp *app) { (void)app; abort(); }

int main(int argc,char **argv) {
    assert(argc==3);
    const size_t rom_size=strtoull(argv[2],NULL,10);
    const Sb4Compat compat={.version=SB4_COMPAT_VERSION,.bytes=sizeof compat,.backend=SB4_COMPAT_LIBOUNCE,
        .resolve_c=resolve_c,.resolve_service=service,.allocate_aligned=allocate,.release=release,
        .ticks=ticks,.tick_hz=ticks,.sleep_ns=sleep_ns};
    const Sb4StorageFiles files={.version=SB4_STORAGE_VERSION,.bytes=sizeof files,.open=open_sd,.read=read_sd,
        .write=write_sd,.size=get_size,.resize=resize,.flush=token_op,.close=token_op,.create=create,
        .mkdir=mkdir,.commit=commit};
    const Sb4Storage storage={.version=SB4_STORAGE_VERSION,.bytes=sizeof storage,
        .list=list_sd,.file_size=path_size,.files=&files};
    IzNativeApp7Storage env={.storage=&storage};
    env.base.sb4=&compat;
    IzNativeApp *app=(IzNativeApp*)&env;
    app->magic=0x414e5a49;
    app->version=SB4_NATIVE_ABI;
    app->bytes=sizeof env;
    const Ps2SdkFsApi *api=ps2_sb4_fs_bind(app);
    assert(api);
    Ps2SdkFs sdk={0};
    assert(!ps2_sdk_fs_initialize(&sdk,api));

    Ps2ResourceRomfs rom={0};
    assert(!ps2_resource_romfs_mount(&rom,izps2_builtin_resources,rom_size));
    uint32_t count=0, directories=0;
    uint64_t total=0;
    for(uint32_t i=0;i<rom.entries;++i) {
        Ps2ResourceRomfsEntry entry={0};
        assert(!ps2_resource_romfs_entry(&rom,i,&entry));
        Sb4StorageInfo info={0};
        assert(!ps2_sb4_fs_stat(entry.path,&info));
        if(entry.kind==PS2_ROMFS_DIRECTORY) {
            assert(info.directory);
            Sb4StorageEntry entries[128];
            uint32_t n=0;
            assert(!ps2_sb4_fs_list(entry.path,entries,128,&n));
            for(uint32_t j=0;j<n;++j) {
                Ps2ResourceRomfsEntry child={0};
                assert(ps2_resource_romfs_child(&rom,i,j,&child)==1);
                assert(!strcmp(entries[j].name,strrchr(child.path,'/')+1));
                assert(entries[j].size==child.bytes);
            }
            Ps2ResourceRomfsEntry end={0};
            assert(!ps2_resource_romfs_child(&rom,i,n,&end));
            ++directories;
            continue;
        }
        assert(!info.directory && info.size==entry.bytes);
        Ps2SdkFile f={0};
        assert(!ps2_sdk_fs_open(&sdk,entry.path,PS2_FS_READ,&f));
        char disk_path[1024];
        int n=snprintf(disk_path,sizeof disk_path,"%s%s",argv[1],entry.path+strlen("/resources"));
        assert(n>0 && (size_t)n<sizeof disk_path);
        FILE *reference=fopen(disk_path,"rb");
        assert(reference);
        unsigned char actual[16384], expected[16384];
        for(uint64_t offset=0;offset<entry.bytes;) {
            size_t remaining=entry.bytes-offset > sizeof actual ? sizeof actual : (size_t)(entry.bytes-offset);
            size_t read=0;
            assert(!ps2_sdk_fs_read(&f,offset,actual,remaining,&read) && read==remaining);
            assert(fread(expected,1,remaining,reference)==remaining && !memcmp(actual,expected,remaining));
            offset+=read;
        }
        size_t eof=123;
        assert(!ps2_sdk_fs_read(&f,entry.bytes,actual,sizeof actual,&eof) && !eof);
        assert(fgetc(reference)==EOF && !fclose(reference));
        assert(!ps2_sdk_fs_close(&f));
        total+=entry.bytes;
        ++count;
    }

    assert(count==123 && total==12661001 && !sd_opened);
    Ps2SdkFile missing={0};
    assert(ps2_sdk_fs_open(&sdk,"/resources/absent",PS2_FS_READ,&missing)!=0 && !sd_opened);
    assert(ps2_sdk_fs_open(&sdk,"/resources/GameIndex.yaml",PS2_FS_WRITE|PS2_FS_UPDATE_EXISTING,&missing)==PS2_FS_READ_ONLY);
    assert(ps2_sdk_fs_open(&sdk,"/sd/iz2s/modules/izps2/bios/absent.bin",PS2_FS_READ,&missing)!=0 && sd_opened==1);
    assert(sdk.opened==count && sdk.closed==count);
    assert(!ps2_sb4_fs_unbind());
    assert(ps2_sb4_fs_bind(app)==api && !ps2_sb4_fs_unbind());
    printf("ROMFS files=%u directories=%u resource_bytes=%llu sd_reads=0 resource_handles_closed=%llu\n",
        count,directories,(unsigned long long)total,(unsigned long long)sdk.closed);
    return 0;
}
