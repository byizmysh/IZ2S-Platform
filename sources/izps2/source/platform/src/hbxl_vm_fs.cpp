/* HostVFS scalar callbacks over this image's own POSIX libc. In native HBXL,
 * open/pread/stat/directory calls reach the real HBFS TCP VFS personality;
 * the SAME callbacks use Linux files only in the separate QEMU process.
 * No SDK FILE, pthread or FsContext object crosses this adapter. */
#include "fs_bridge.h"
#include "hbxl_vm_frontend.h"
#include "posix_file_io.h"
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(x) do{if(!(x))ps2_hbxl_vm_fatal(#x,__LINE__);}while(0)
namespace {
Ps2SdkFs fs;
std::string game,bios_prefix;
thread_local uint64_t context=0;
std::atomic<uint64_t> entered{0},restored{0},game_bytes{0},bios_bytes{0},game_max_end{0};
std::atomic<unsigned> kind[1024];
void enter(uint64_t*out){CHECK(!context);*out=context;context=1;entered++;}
void restore(uint64_t old){CHECK(context==1 && !old);context=old;restored++;}
int fd(uint64_t h){CHECK(context==1 && h && h<=1024);return static_cast<int>(h-1);}
uint32_t open(uint64_t*out,const char*path,int mode){
    CHECK(context==1);int descriptor=::open(path,mode==1?O_RDONLY:mode==2?O_WRONLY:O_RDWR);
    if(descriptor<0)return errno==ENOENT?0x202:0xe001;
    if(descriptor>=1024){::close(descriptor);return PS2_FS_UNSUPPORTED;}
    kind[descriptor]=(game==path)?1:!std::strncmp(path,bios_prefix.c_str(),bios_prefix.size())?2:0;
    *out=static_cast<uint64_t>(descriptor)+1;return 0;
}
void close(uint64_t h){const int descriptor=fd(h);CHECK(!::close(descriptor));kind[descriptor]=0;}
uint32_t size(int64_t*out,uint64_t h){struct stat st{};if(::fstat(fd(h),&st))return 0xe002;*out=st.st_size;return 0;}
uint32_t read(size_t*out,uint64_t h,int64_t offset,void*data,size_t n){
    const int descriptor=fd(h);const ssize_t got=ps2::posix::ReadAt(descriptor,data,n,offset);if(got<0)return 0xe003;
    *out=static_cast<size_t>(got);
    if(kind[descriptor]==1){
        game_bytes+=got;const uint64_t end=offset+got;uint64_t old=game_max_end;
        while(end>old && !game_max_end.compare_exchange_weak(old,end)){}
    }else if(kind[descriptor]==2)bios_bytes+=got;
    return 0;
}
uint32_t write(uint64_t h,int64_t off,const void*data,size_t n,const uint32_t*option){CHECK(option && *option==1);return ps2::posix::WriteAtAndSync(fd(h),data,n,off)?0:0xe004;}
uint32_t flush(uint64_t h){return ps2::posix::Sync(fd(h))?0xe005:0;}
uint32_t resize(uint64_t h,int64_t n){return ps2::posix::Resize(fd(h),n)?0xe006:0;}
uint32_t create(const char*path,int64_t n,int option){CHECK(context==1 && !option);const int descriptor=::open(path,O_CREAT|O_EXCL|O_RDWR,0600);if(descriptor<0)return 0xe007;const int r=ps2::posix::Resize(descriptor,n);CHECK(!::close(descriptor));return r?0xe008:0;}
int missing(uint32_t rc){return rc==0x202;}
}
void ps2_hbxl_vm_fs_initialize(const char*scratch,const char*resources,const char*image){
    game=image;bios_prefix=std::string(scratch)+"/bios/";
    const std::string games=game.substr(0,game.rfind('/'));
    Ps2SdkFsApi api{enter,restore,open,close,size,read,write,flush,resize,create,missing};CHECK(!ps2_sdk_fs_initialize(&fs,&api));
    const Ps2FsMount mounts[]={{scratch,scratch,true},{resources,resources,false},{games.c_str(),games.c_str(),false}};
    CHECK(ps2_install_file_bridge(&fs,mounts,3));
}
void ps2_hbxl_vm_fs_shutdown(){
    CHECK(entered==restored && fs.opened==fs.closed);
    FILE*f=std::fopen("hbxl-vm-fs.json","wb");CHECK(f);
    std::fprintf(f,"{\"opened\":%llu,\"closed\":%llu,\"scopePairs\":%llu,\"gameBytesRead\":%llu,\"biosBytesRead\":%llu,\"gameMaxReadEnd\":%llu,\"readOnlyGameMount\":true,\"directoryAndStatProvider\":\"own POSIX personality; native HBXL/HBFS, offline Linux\"}\n",(unsigned long long)fs.opened,(unsigned long long)fs.closed,(unsigned long long)entered.load(),(unsigned long long)game_bytes.load(),(unsigned long long)bios_bytes.load(),(unsigned long long)game_max_end.load());
    CHECK(!std::fclose(f));
}
