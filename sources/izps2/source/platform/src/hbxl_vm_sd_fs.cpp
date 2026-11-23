/* Explicit mixed filesystem: provided read-only SD IFile capabilities for
 * BIOS/disc, unchanged private HBFS POSIX personality for resources/scratch. */
#include "hbxl_vm_sd_fs.h"
#include "hbxl_vm_frontend.h"
#include "posix_file_io.h"
#ifdef PS2_NATIVE_SD_LIVE_CARD
#include "ps2_sd_card.h"
#endif
#include "fs_bridge.h"
#include "common/HostVFS.h"
#include "hbxl.h"
#include "hbx_sdk.h"
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(x) do{if(!(x))ps2_hbxl_vm_fatal(#x,__LINE__);}while(0)
namespace {
constexpr uint64_t tag=1ull<<63;
#ifdef PS2_NATIVE_SD_LIVE_CARD
constexpr uint64_t live_tag=1ull<<62;
std::atomic<uint64_t> live_opened{0},live_closed{0},live_read_bytes{0},live_written_bytes{0};
bool live_path(const char* p){return ps2::IsLiveSdCardPath(p);}
#else
bool live_path(const char*){return false;}
#endif
Ps2SdkFs fs;Ps2IFileMount sd;std::mutex sd_mutex;
struct Logical {uint64_t generation;unsigned file,live;} streams[64];uint64_t generation=0;
std::atomic<uint64_t> entered{0},restored{0};thread_local uint64_t context=0;
bool initialized=false,disc_configured=false;
void enter(uint64_t*out){CHECK(!context);*out=0;context=1;entered++;}
void restore(uint64_t old){CHECK(context==1&&!old);context=0;restored++;}
int lookup(const char*p){return ps2_ifile_find(&sd,p);}
bool protected_path(const char*p){return initialized&&p&&(!std::strncmp(p,"/sd/izmysh/ps2/",15)||lookup(p)>=0);}
int slot(uint64_t h){unsigned at=(h&255)-1;CHECK((h&tag)&&at<64&&streams[at].live&&streams[at].generation==((h&~tag)>>8));return at;}
int descriptor(uint64_t h){CHECK(context==1&&h&&!(h&tag)&&h<=1024);return static_cast<int>(h-1);}
uint32_t open(uint64_t*out,const char*path,int mode){
    CHECK(context==1);
#ifdef PS2_NATIVE_SD_LIVE_CARD
    if(live_path(path)){
        uint64_t handle=0;const uint32_t rc=ps2::LiveSdCardBridgeApi().open(&handle,path,mode);
        if(rc)return rc;
        CHECK(handle&&!(handle&(tag|live_tag)));*out=live_tag|handle;live_opened++;return 0;
    }
#endif
    std::lock_guard<std::mutex> lock(sd_mutex);const int index=lookup(path);
    if(index>=0){
        if(mode!=1)return PS2_FS_READ_ONLY;uint64_t actual=0;uint32_t rc=ps2_ifile_size(&sd,index,&actual);if(rc)return rc;
        for(unsigned i=0;i<64;i++)if(!streams[i].live){CHECK(generation<(tag>>8)-1);streams[i]={++generation,static_cast<unsigned>(index),1};*out=tag|(generation<<8)|(i+1);return 0;}return PS2_FS_UNSUPPORTED;
    }
    if(protected_path(path))return 0x202;
    const int fd=::open(path,mode==1?O_RDONLY:mode==2?O_WRONLY:O_RDWR);if(fd<0)return errno==ENOENT?0x202:0xe001;if(fd>=1024){::close(fd);return PS2_FS_UNSUPPORTED;}*out=fd+1;return 0;
}
void close(uint64_t h){
#ifdef PS2_NATIVE_SD_LIVE_CARD
    if((h&(tag|live_tag))==live_tag){ps2::LiveSdCardBridgeApi().close(h&~live_tag);live_closed++;return;}
#endif
    if(h&tag){std::lock_guard<std::mutex> lock(sd_mutex);streams[slot(h)].live=0;return;}CHECK(!::close(descriptor(h)));}
uint32_t size(int64_t*out,uint64_t h){
#ifdef PS2_NATIVE_SD_LIVE_CARD
    if((h&(tag|live_tag))==live_tag)return ps2::LiveSdCardBridgeApi().get_size(out,h&~live_tag);
#endif
    if(h&tag){std::lock_guard<std::mutex> lock(sd_mutex);uint64_t n=0;uint32_t rc=ps2_ifile_size(&sd,streams[slot(h)].file,&n);if(!rc)*out=n;return rc;}
    struct stat st{};if(::fstat(descriptor(h),&st))return 0xe002;*out=st.st_size;return 0;
}
uint32_t read(size_t*out,uint64_t h,int64_t off,void*data,size_t n){
#ifdef PS2_NATIVE_SD_LIVE_CARD
    if((h&(tag|live_tag))==live_tag){const auto rc=ps2::LiveSdCardBridgeApi().read(out,h&~live_tag,off,data,n);if(!rc)live_read_bytes+=*out;return rc;}
#endif
    if(h&tag){std::lock_guard<std::mutex> lock(sd_mutex);return ps2_ifile_read(&sd,streams[slot(h)].file,off,data,n,out);}
    const ssize_t got=ps2::posix::ReadAt(descriptor(h),data,n,off);if(got<0)return 0xe003;*out=got;return 0;
}
uint32_t write(uint64_t h,int64_t off,const void*data,size_t n,const uint32_t*option){
#ifdef PS2_NATIVE_SD_LIVE_CARD
    if((h&(tag|live_tag))==live_tag){const auto rc=ps2::LiveSdCardBridgeApi().write(h&~live_tag,off,data,n,option);if(!rc)live_written_bytes+=n;return rc;}
#endif
    if(h&tag)return PS2_FS_READ_ONLY;CHECK(option&&*option==1);return ps2::posix::WriteAtAndSync(descriptor(h),data,n,off)?0:0xe004;}
uint32_t flush(uint64_t h){
#ifdef PS2_NATIVE_SD_LIVE_CARD
    if((h&(tag|live_tag))==live_tag)return ps2::LiveSdCardBridgeApi().flush(h&~live_tag);
#endif
    return h&tag?0:(ps2::posix::Sync(descriptor(h))?0xe005:0);}
uint32_t resize(uint64_t h,int64_t n){
#ifdef PS2_NATIVE_SD_LIVE_CARD
    if((h&(tag|live_tag))==live_tag)return ps2::LiveSdCardBridgeApi().set_size(h&~live_tag,n);
#endif
    return h&tag?PS2_FS_READ_ONLY:(ps2::posix::Resize(descriptor(h),n)?0xe006:0);}
uint32_t create(const char*p,int64_t n,int option){CHECK(context==1&&!option);if(protected_path(p)||live_path(p))return PS2_FS_READ_ONLY;const int fd=::open(p,O_CREAT|O_EXCL|O_RDWR,0600);if(fd<0)return 0xe007;const int r=ps2::posix::Resize(fd,n);CHECK(!::close(fd));return r?0xe008:0;}
int missing(uint32_t r){return r==0x202;}
int sd_stat(const char*p,struct stat*s){
#ifdef PS2_NATIVE_SD_LIVE_CARD
    if(live_path(p)){
        const auto&api=ps2::LiveSdCardBridgeApi();uint64_t handle=0;int64_t n=-1;
        uint32_t rc=api.open(&handle,p,1);if(!rc){rc=api.get_size(&n,handle);api.close(handle);}
        if(rc||n<0){errno=EIO;return -1;}
        std::memset(s,0,sizeof(*s));s->st_mode=S_IFREG|0600;s->st_size=n;s->st_nlink=1;s->st_blksize=4096;s->st_blocks=(n+511)/512;return 0;
    }
#endif
    std::lock_guard<std::mutex> lock(sd_mutex);const int index=lookup(p);
    if(index>=0){uint64_t n=0;const uint32_t r=ps2_ifile_size(&sd,index,&n);if(r){errno=EIO;return -1;}std::memset(s,0,sizeof(*s));s->st_mode=S_IFREG|0444;s->st_size=n;s->st_nlink=1;s->st_blksize=4096;s->st_blocks=(n+511)/512;return 0;}
    const size_t n=std::strlen(p);for(unsigned i=0;i<sd.file_count;i++)if(n&&n<std::strlen(sd.files[i].logical)&&!std::strncmp(p,sd.files[i].logical,n)&&sd.files[i].logical[n]=='/'){
        std::memset(s,0,sizeof(*s));s->st_mode=S_IFDIR|0555;s->st_nlink=1;return 0;
    }errno=ENOENT;return -1;
}
int stat_path(const char*p,s32*size){struct stat s{};if(::stat(p,&s))return 0;*size=static_cast<s32>(s.st_size);return HostVFS::STAT_IS_VALID|(S_ISDIR(s.st_mode)?HostVFS::STAT_IS_DIRECTORY:0);}
}
extern "C" int __real_stat(const char*,struct stat*);
extern "C" int __wrap_stat(const char*p,struct stat*s){if(initialized&&(live_path(p)||protected_path(p)||(!std::strncmp(p,"/sd/izmysh/ps2",14)&&(!p[14]||p[14]=='/'))))return sd_stat(p,s);return __real_stat(p,s);}
extern "C" int __real_open(const char*,int,...);
extern "C" int __wrap_open(const char*p,int flags,...){
    if(protected_path(p)||live_path(p)){errno=live_path(p)?ENOTSUP:(flags&O_ACCMODE)?EROFS:ENOTSUP;return -1;}
    mode_t mode=0;if(flags&O_CREAT){va_list a;va_start(a,flags);mode=va_arg(a,int);va_end(a);}return __real_open(p,flags,mode);
}
extern "C" FILE*__real_fopen(const char*,const char*);
extern "C" FILE*__wrap_fopen(const char*p,const char*mode){if(protected_path(p)||live_path(p)){errno=live_path(p)?ENOTSUP:mode&&mode[0]=='r'&&!std::strchr(mode,'+')?ENOTSUP:EROFS;return nullptr;}return __real_fopen(p,mode);}
extern "C" void ps2_hbxl_vm_fs_initialize(const char*scratch,const char*resources,const char*image){
    CHECK(!initialized&&hbxl_ctx&&hbxl_ctx->magic==HBX_ABI_MAGIC&&hbxl_ctx->abi_version==3&&hbxl_ctx->size>=sizeof(*hbxl_ctx));
    CHECK(hbxl_ctx->args[3]&&!(hbxl_ctx->args[3]&7)&&hbxl_ctx->args[3]<=(1ull<<39)-sizeof(Ps2IFileMountDescription));
    CHECK(hbxl_ctx->audio.initialize_parameter>=0x1c7f30);
    const auto*c=(const Ps2IFileMountDescription*)(uintptr_t)hbxl_ctx->args[3];
    CHECK(!ps2_ifile_sdk_initialize(&sd,c,hbxl_ctx->audio.initialize_parameter-0x1c7f30,6164480));
    CHECK(image);disc_configured=image[0]!=0;
    if(disc_configured)CHECK(lookup(image)>=0&&sd.files[lookup(image)].role==PS2_IFILE_ROLE_ISO);
    unsigned bios=0;for(unsigned i=0;i<sd.file_count;i++){
        // A NoDisc launch must not even query an unrelated disc capability.
        if(!disc_configured)CHECK(sd.files[i].role==PS2_IFILE_ROLE_BIOS);
        uint64_t n=0;CHECK(!ps2_ifile_size(&sd,i,&n));bios+=sd.files[i].role==PS2_IFILE_ROLE_BIOS;
    }CHECK(bios);
    Ps2SdkFsApi api{enter,restore,open,close,size,read,write,flush,resize,create,missing};CHECK(!ps2_sdk_fs_initialize(&fs,&api));
    const std::string image_path=image,games=image_path.substr(0,image_path.rfind('/'));
    const Ps2FsMount mounts[]={{scratch,scratch,true},{resources,resources,false},{games.c_str(),games.c_str(),false}};
    CHECK(ps2_install_file_bridge(&fs,mounts,disc_configured?3:2));HostVFS::Ops ops=*HostVFS::GetOps();ops.stat=stat_path;HostVFS::Install(ops);initialized=true;
}
extern "C" void ps2_hbxl_vm_sd_fs_statistics(Ps2HbxlVmSdFsStatistics*s){
    if(!s)return;std::lock_guard<std::mutex> lock(sd_mutex);*s={};s->opened=fs.opened;s->closed=fs.closed;
    s->poisoned=sd.poisoned;s->error=sd.error;s->transport=sd.transport;s->unexpected_handles=sd.unexpected_count;
    for(auto&stream:streams)s->active_streams+=stream.live;
    for(unsigned i=0;i<sd.file_count;i++){const auto&c=sd.counters[i];s->physical_reads+=c.physical_reads;
        if(sd.files[i].role==PS2_IFILE_ROLE_ISO){s->game_bytes+=c.physical_bytes;if(c.max_logical_end>s->game_max_end)s->game_max_end=c.max_logical_end;}
        else if(sd.files[i].role==PS2_IFILE_ROLE_BIOS)s->bios_bytes+=c.physical_bytes;
    }
}
extern "C" void ps2_hbxl_vm_fs_shutdown(){
    Ps2HbxlVmSdFsStatistics s{};ps2_hbxl_vm_sd_fs_statistics(&s);
    CHECK(initialized&&entered==restored&&s.opened==s.closed&&!s.active_streams&&!s.poisoned&&!s.error&&!sd.in_flight);
#ifdef PS2_NATIVE_SD_LIVE_CARD
    CHECK(live_opened==live_closed&&ps2::LiveSdCardHealthy());
    FILE* live_report=std::fopen("hbxl-vm-live-card-route.json","wb");CHECK(live_report);
    std::fprintf(live_report,"{\"passed\":true,\"originalMixedFsRoute\":true,\"opened\":%llu,\"closed\":%llu,\"readBytes\":%llu,\"writtenBytes\":%llu,\"posixCardFallbackAllowed\":false}\n",
        (unsigned long long)live_opened.load(),(unsigned long long)live_closed.load(),
        (unsigned long long)live_read_bytes.load(),(unsigned long long)live_written_bytes.load());
    CHECK(!std::fclose(live_report));
#endif
    FILE*f=std::fopen("hbxl-vm-sd-fs.json","wb");CHECK(f);
    std::fprintf(f,"{\"opened\":%llu,\"closed\":%llu,\"gameBytesRead\":%llu,\"biosBytesRead\":%llu,\"gameMaxReadEnd\":%llu,\"physicalIFileReads\":%llu,\"readOnlyGameMount\":%s,\"mountCount\":%u,\"readCacheBytes\":65536,\"segmentedLogicalFiles\":true,\"borrowedCapabilitiesClosed\":false,\"hostGameReads\":0,\"hostBiosReads\":0,\"resourcesAndScratch\":\"HBFS\"}\n",(unsigned long long)s.opened,(unsigned long long)s.closed,(unsigned long long)s.game_bytes,(unsigned long long)s.bios_bytes,(unsigned long long)s.game_max_end,(unsigned long long)s.physical_reads,disc_configured?"true":"false",disc_configured?3u:2u);CHECK(!std::fclose(f));
}
