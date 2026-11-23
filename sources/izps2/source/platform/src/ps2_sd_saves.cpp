#include "ps2_sb4_layout.h"
#include "ps2_sd_saves.h"
#include "posix_file_io.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace ps2 {
namespace {
constexpr size_t Header = 64, Block = 65536;
constexpr uint64_t Limit = 512ull * 1024 * 1024;
constexpr unsigned char Magic[8] = {'P','S','2','S','D','S','V','1'};
Ps2SdkFs filesystem{};
std::string directory;
std::mutex owner;
enum class Kind { Missing, Valid, Corrupt, Error };
struct Slot {Kind kind = Kind::Missing;uint64_t generation = 0, bytes = 0;uint32_t crc = 0;bool unpublished = false;};
uint32_t Crc(uint32_t c, const void* data, size_t n) {
    const auto* p = static_cast<const unsigned char*>(data);
    while (n--) {c ^= *p++;for (unsigned i=0;i<8;i++) c = (c>>1) ^ (0xedb88320u & (0u-(c&1)));}
    return c;
}
void Put(unsigned char* p, uint64_t v, unsigned n) {for(unsigned i=0;i<n;i++){p[i]=v&255;v>>=8;}}
uint64_t Get(const unsigned char* p, unsigned n) {uint64_t v=0;for(unsigned i=0;i<n;i++)v|=uint64_t(p[i])<<(8*i);return v;}
uint64_t KeyHash(const std::string& key) {uint64_t v=14695981039346656037ull;for(unsigned char c:key){v^=c;v*=1099511628211ull;}return v;}
bool Key(const std::string& key) {
    if(key.empty()||key.size()>100||key=="."||key=="..")return false;
    for(unsigned char c:key)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_'))return false;
    return true;
}
std::string Name(const std::string& key, unsigned index) {return directory+"/"+key+(index?".b.ps2save":".a.ps2save");}
bool Read(Ps2SdkFile& f, uint64_t off, void* p, size_t n) {size_t got=0;return !ps2_sdk_fs_read(&f,off,p,n,&got)&&got==n;}
bool Same(const struct stat& a,const struct stat& b) {
    return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&
        a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&
        a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
}
Slot Inspect(const std::string& key,unsigned index) {
    Slot s;Ps2SdkFile file{};
    const uint32_t rc=ps2_sdk_fs_open(&filesystem,Name(key,index).c_str(),PS2_FS_READ,&file);
    if(rc){s.kind=filesystem.api.is_not_found&&filesystem.api.is_not_found(rc)?Kind::Missing:Kind::Error;return s;}
    s.kind=Kind::Corrupt;
    unsigned char header[Header]{};
    if(file.size<Header){
        s.unpublished = file.size == 0;
        if(ps2_sdk_fs_close(&file))s.kind=Kind::Error;
        return s;
    }
    if(!Read(file,0,header,Header)){s.kind=Kind::Error;ps2_sdk_fs_close(&file);return s;}
    s.unpublished = std::all_of(header, header + Header, [](unsigned char byte) { return byte == 0; });
    const auto header_crc=Get(header+36,4);Put(header+36,0,4);
    bool zero=true;for(unsigned i=48;i<Header;i++)zero=zero&&!header[i];
    s.generation=Get(header+16,8);s.bytes=Get(header+24,8);s.crc=Get(header+32,4);
    if(!std::memcmp(header,Magic,8)&&Get(header+8,4)==1&&Get(header+12,4)==Header&&
       s.generation&&s.bytes&&s.bytes<=Limit&&file.size==Header+s.bytes&&
       Get(header+40,8)==KeyHash(key)&&zero&&(Crc(~0u,header,Header)^~0u)==header_crc){
        std::array<unsigned char,Block> buffer;uint32_t crc=~0u;bool ok=true;
        for(uint64_t off=0;off<s.bytes;){const size_t n=std::min<uint64_t>(Block,s.bytes-off);
            if(!Read(file,Header+off,buffer.data(),n)){ok=false;s.kind=Kind::Error;break;}
            crc=Crc(crc,buffer.data(),n);off+=n;}
        if(ok&&(crc^~0u)==s.crc)s.kind=Kind::Valid;
    }
    if(ps2_sdk_fs_close(&file))s.kind=Kind::Error;
    return s;
}
int Select(const Slot (&slots)[2],SdSaveResult& result) {
    if(slots[0].kind==Kind::Error||slots[1].kind==Kind::Error){result.error="SD save could not be read; existing slots were preserved";return -2;}
    if(slots[0].kind!=Kind::Valid&&slots[1].kind!=Kind::Valid){
        if(slots[0].kind==Kind::Corrupt||slots[1].kind==Kind::Corrupt){result.error="Both SD save generations are unavailable or corrupt; local data was preserved";return -2;}
        return -1;
    }
    if(slots[0].kind==Kind::Valid&&slots[1].kind==Kind::Valid&&slots[0].generation==slots[1].generation&&
       (slots[0].bytes!=slots[1].bytes||slots[0].crc!=slots[1].crc)){result.error="Conflicting SD save generations";return -2;}
    const int selected=slots[0].kind!=Kind::Valid?1:slots[1].kind!=Kind::Valid?0:slots[1].generation>slots[0].generation?1:0;
    result.recovered_previous=slots[1-selected].kind==Kind::Corrupt;
    return selected;
}
bool WriteAll(int fd,const void* bytes,size_t count) {
    const auto* p=static_cast<const unsigned char*>(bytes);
    while(count){const ssize_t n=::write(fd,p,count);if(n<0&&errno==EINTR)continue;if(n<=0)return false;p+=n;count-=n;}return true;
}
bool BackupLocal(const std::string& local,std::string& error) {
    struct stat st{};if(::lstat(local.c_str(),&st))return errno==ENOENT;
    if(!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_size<0||uint64_t(st.st_size)>Limit){error="Local save is not a private regular file of supported size";return false;}
    int input=::open(local.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(input<0)return false;
    std::string pattern=local+".before-sd.XXXXXX";std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);
    const int output=::mkstemp(name.data());if(output<0){::close(input);return false;}
    bool ok=true;std::array<unsigned char,Block> b;
    for(;;){const ssize_t n=::read(input,b.data(),b.size());if(n<0&&errno==EINTR)continue;if(n<0){ok=false;break;}if(!n)break;if(!WriteAll(output,b.data(),n)){ok=false;break;}}
    if(posix::Sync(output))ok=false;
    struct stat after{};if(::fstat(input,&after)||!Same(st,after))ok=false;
    if(::close(input))ok=false;
    if(::close(output))ok=false;
    if(!ok)error="Cannot preserve the existing local save before SD restore";
    return ok;
}
}

bool ConfigureSdSaves(const Ps2SdkFsApi& api,const std::string& root) {
    std::lock_guard<std::mutex> lock(owner);
    if(filesystem.ready||root.empty()||root.size()>128||root.back()=='/'||(root!=PS2_SB4_SAVES_ROOT&&root.find("/izmysh/ps2/saves")==std::string::npos)||
       root.find("..")!=std::string::npos||root.find('\\')!=std::string::npos||!api.write||!api.flush||!api.set_size||!api.create||!api.is_not_found)return false;
    for(unsigned char c:root)if(c<32||c==127)return false;
    if(ps2_sdk_fs_initialize(&filesystem,&api))return false;
    directory=root;return true;
}
bool SdSavesEnabled() {std::lock_guard<std::mutex> lock(owner);return filesystem.ready;}
bool ShutdownSdSaves() {
    std::lock_guard<std::mutex> lock(owner);
    if(filesystem.opened!=filesystem.closed)return false;
    filesystem={};directory.clear();return true;
}

namespace {
uint32_t ReadExactly(SdSaveReadAt read_at,void* opaque,uint64_t offset,void* buffer,size_t bytes) {
    size_t actual=0;
#if defined(__cpp_exceptions)
    try {
#endif
        const uint32_t rc=read_at(opaque,offset,buffer,bytes,&actual);
        return rc?rc:(actual==bytes?0:static_cast<uint32_t>(PS2_FS_BAD_REPLY));
#if defined(__cpp_exceptions)
    } catch(...) {return PS2_FS_BAD_REPLY;}
#endif
}
using FinishSource = uint32_t (*)(void*);
SdSaveResult StoreReaderLocked(const std::string& key,uint64_t bytes,SdSaveReadAt read_at,
                              void* opaque,FinishSource finish=nullptr) {
    SdSaveResult result;
    if(!filesystem.ready||!Key(key)||!read_at||!bytes||bytes>Limit){
        result.error="SD save reader is not configured, key is invalid, or size is outside 1..512MiB";return result;}
    const Slot slots[]{Inspect(key,0),Inspect(key,1)};
    // An empty file or a zero header is the unpublished state used by the
    // writer. Allow another first save while preserving other corrupt data.
    const auto unused = [](const Slot& s) { return s.kind == Kind::Missing || s.unpublished; };
    const bool new_save = unused(slots[0]) && unused(slots[1]) &&
        slots[0].kind != Kind::Error && slots[1].kind != Kind::Error;
    const int current = new_save ? -1 : Select(slots, result);
    if(current==-2)return result;
    if(current>=0&&slots[current].generation==UINT64_MAX){result.error="SD save generation exhausted";return result;}
    result.bytes=bytes;result.generation=current<0?1:slots[current].generation+1;
    const unsigned destination = current >= 0 ? 1 - current :
        (slots[0].kind != Kind::Missing && slots[1].kind == Kind::Missing ? 1 : 0);
    Ps2SdkFile output{};
    uint32_t rc=ps2_sdk_fs_open(&filesystem,Name(key,destination).c_str(),PS2_FS_READ|PS2_FS_WRITE,&output);
    std::array<unsigned char,Block> buffer;unsigned char header[Header]{};uint32_t crc=~0u;
    if(!rc)rc=ps2_sdk_fs_resize(&output,Header+result.bytes);
    if(!rc)rc=ps2_sdk_fs_write(&output,0,header,Header); // invalid until the final header is durable
    for(uint64_t off=0;!rc&&off<result.bytes;){const size_t n=std::min<uint64_t>(Block,result.bytes-off);
        rc=ReadExactly(read_at,opaque,off,buffer.data(),n);if(rc)break;
        crc=Crc(crc,buffer.data(),n);rc=ps2_sdk_fs_write(&output,Header+off,buffer.data(),n);off+=n;}
    // The local-file adapter must validate and close the source before a valid
    // generation is published. Direct readers keep their pinned source owned
    // by the caller through this function and its complete SD readback.
    if(finish){const uint32_t final_rc=finish(opaque);if(!rc)rc=final_rc;}
    if(!rc){std::memcpy(header,Magic,8);Put(header+8,1,4);Put(header+12,Header,4);Put(header+16,result.generation,8);
        Put(header+24,result.bytes,8);Put(header+32,crc^~0u,4);Put(header+40,KeyHash(key),8);
        Put(header+36,Crc(~0u,header,Header)^~0u,4);rc=ps2_sdk_fs_write(&output,0,header,Header);}
    if(output.opened){const uint32_t closed=ps2_sdk_fs_close(&output);if(!rc)rc=closed;}
    if(rc){result.error="SD save source read/write/flush failed; the previous valid generation was retained";return result;}
    const Slot verified=Inspect(key,destination);
    if(verified.kind!=Kind::Valid||verified.generation!=result.generation||verified.bytes!=result.bytes||verified.crc!=(crc^~0u)){
        result.error="SD save readback failed; the previous valid generation was retained";return result;}
    result.success=result.found=true;return result;
}
struct LocalReader {
    int fd=-1;struct stat before{};
    ~LocalReader(){if(fd>=0)::close(fd);}
};
uint32_t ReadLocal(void* opaque,uint64_t offset,void* buffer,size_t bytes,size_t* actual) {
    auto& source=*static_cast<LocalReader*>(opaque);auto* output=static_cast<unsigned char*>(buffer);*actual=0;
    while(*actual<bytes){const ssize_t n=posix::ReadAt(source.fd,output+*actual,bytes-*actual,offset+*actual);
        if(n<=0||static_cast<size_t>(n)>bytes-*actual)return PS2_FS_BAD_REPLY;
        *actual+=static_cast<size_t>(n);}
    return 0;
}
uint32_t FinishLocal(void* opaque) {
    auto& source=*static_cast<LocalReader*>(opaque);struct stat after{};
    bool ok=!::fstat(source.fd,&after)&&Same(source.before,after);
    const int fd=source.fd;source.fd=-1;if(::close(fd))ok=false;
    return ok?0:PS2_FS_BAD_REPLY;
}
}
SdSaveResult StoreSdSaveFromReader(const std::string& key,uint64_t bytes,SdSaveReadAt read_at,void* opaque) {
    std::lock_guard<std::mutex> lock(owner);return StoreReaderLocked(key,bytes,read_at,opaque);
}
SdSaveResult StoreSdSave(const std::string& local,const std::string& key) {
    std::lock_guard<std::mutex> lock(owner);SdSaveResult result;
    if(!filesystem.ready||!Key(key)){result.error="SD save storage is not configured or key is invalid";return result;}
    LocalReader source;source.fd=::open(local.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(source.fd<0||::fstat(source.fd,&source.before)||!S_ISREG(source.before.st_mode)||source.before.st_nlink!=1||source.before.st_size<=0||uint64_t(source.before.st_size)>Limit){
        result.error="Cannot read a stable private save for SD";return result;}
    return StoreReaderLocked(key,source.before.st_size,ReadLocal,&source,FinishLocal);
}

SdSaveResult RestoreSdSave(const std::string& local,const std::string& key) {
    std::lock_guard<std::mutex> lock(owner);SdSaveResult result;
    if(!filesystem.ready||!Key(key)){result.error="SD save storage is not configured or key is invalid";return result;}
    const Slot slots[]={Inspect(key,0),Inspect(key,1)};const int selected=Select(slots,result);
    if(selected==-2)return result;
    if(selected<0){result.success=true;return result;}
    result.found=true;result.bytes=slots[selected].bytes;result.generation=slots[selected].generation;
    Ps2SdkFile input{};if(ps2_sdk_fs_open(&filesystem,Name(key,selected).c_str(),PS2_FS_READ,&input)){result.error="Cannot reopen SD save";return result;}
    std::string pattern=local+".sd-restore.XXXXXX";std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);
    const int output=::mkstemp(name.data());bool ok=output>=0;std::array<unsigned char,Block> b;uint32_t crc=~0u;
    for(uint64_t off=0;ok&&off<result.bytes;){const size_t n=std::min<uint64_t>(Block,result.bytes-off);
        if(!Read(input,Header+off,b.data(),n)||!WriteAll(output,b.data(),n)){ok=false;break;}crc=Crc(crc,b.data(),n);off+=n;}
    if((crc^~0u)!=slots[selected].crc)ok=false;
    if(output>=0){if(posix::Sync(output))ok=false;if(::close(output))ok=false;}
    if(ps2_sdk_fs_close(&input))ok=false;
    if(ok)ok=BackupLocal(local,result.error);
    if(ok)ok=::rename(name.data(),local.c_str())==0;
    if(!ok){if(output>=0)::unlink(name.data());if(result.error.empty())result.error="Cannot restore SD save; local data was preserved";return result;}
    result.success=true;return result;
}
SdSaveResult RestoreRequiredSdSave(const std::string& local,const std::string& key) {
    auto result=RestoreSdSave(local,key);
    if(result.success&&!result.found){result.success=false;result.error="Required SD save is missing; local save was preserved and must not be loaded";}
    return result;
}
}
