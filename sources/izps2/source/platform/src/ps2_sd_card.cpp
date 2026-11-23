#include "ps2_sd_card.h"
#include "posix_file_io.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <sys/stat.h>
#include <type_traits>
#include <unistd.h>
#include <vector>

namespace ps2 {
namespace {
constexpr uint64_t CardBytes=8ull*1024*528*2;
constexpr uint64_t MaxGeneration=(1ull<<54)-1;
constexpr size_t Block=65536;
Ps2SdkFs filesystem{};
// Path guards remain callable after the CPU owner detaches emulated TLS.
// Inactive routing must answer without touching OwnerLock or thread-local state.
std::atomic<bool> routing_active{false};
// Wrapped POSIX open/stat can query the bound path while this thread owns
// the client lock. Reenter only on the owning thread, without a recursive
// pthread mutex whose static destructor could run after SDK thread teardown.
std::mutex owner;
static_assert(std::is_trivially_destructible<std::mutex>::value,
              "SD card owner must not destroy a pthread mutex after SDK teardown");
thread_local unsigned owner_depth=0;
class OwnerLock {
public:
    OwnerLock(){if(!owner_depth)owner.lock();++owner_depth;}
    ~OwnerLock(){if(!--owner_depth)owner.unlock();}
    OwnerLock(const OwnerLock&)=delete;
    OwnerLock& operator=(const OwnerLock&)=delete;
};
std::string directory,bound_path,bound_key;
uint32_t (*rename_file)(const char*,const char*)=nullptr;
bool healthy=false;
uint64_t generation=0,temporary_sequence=0;
struct Stream {Ps2SdkFile file{};uint64_t generation=0;bool live=false,readable=false;};
std::array<Stream,32> streams{};
bool Key(const std::string& key){
    if(key.empty()||key.size()>100||key=="."||key=="..")return false;
    for(unsigned char c:key)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_'))return false;
    return true;
}
bool Canonical(const std::string& path){
    if(path.empty()||path[0]!='/'||path.size()>768||path.back()=='/')return false;
    for(size_t start=1;start<path.size();){const size_t end=path.find('/',start);const auto part=path.substr(start,end==std::string::npos?end:end-start);
        if(part.empty()||part=="."||part=="..")return false;
        for(unsigned char c:part)if(c<32||c==127||c=='\\'||c==':')return false;
        if(end==std::string::npos)break;
        start=end+1;
    }return true;
}
bool Root(const std::string&path){const size_t colon=path.find(':');if(colon==std::string::npos)return Canonical(path);if(!colon)return false;for(size_t i=0;i<colon;i++){unsigned char c=path[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;}return Canonical(path.substr(colon+1));}
std::string Name(const std::string& key){return directory+"/"+key;}
uint32_t Crc(uint32_t crc,const void* data,size_t n){const auto*p=static_cast<const unsigned char*>(data);while(n--){crc^=*p++;for(unsigned i=0;i<8;i++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));}return crc;}
bool Same(const struct stat&a,const struct stat&b){return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&a.st_nlink==b.st_nlink&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;}
bool WriteAll(int fd,const void*data,size_t bytes){const auto*p=static_cast<const unsigned char*>(data);while(bytes){ssize_t n=::write(fd,p,bytes);if(n<0&&errno==EINTR)continue;if(n<=0||static_cast<size_t>(n)>bytes)return false;p+=n;bytes-=n;}return true;}
bool ReadAll(int fd,void*data,size_t bytes){auto*p=static_cast<unsigned char*>(data);while(bytes){ssize_t n=::read(fd,p,bytes);if(n<0&&errno==EINTR)continue;if(n<=0||static_cast<size_t>(n)>bytes)return false;p+=n;bytes-=n;}return true;}
bool SdRead(Ps2SdkFile&file,uint64_t offset,void*data,size_t bytes){size_t got=0;return !ps2_sdk_fs_read(&file,offset,data,bytes,&got)&&got==bytes;}
uint32_t SnapshotRead(void*opaque,uint64_t offset,void*data,size_t bytes,size_t*actual){
    auto*file=static_cast<Ps2SdkFile*>(opaque);
    const uint32_t rc=ps2_sdk_fs_read(file,offset,data,bytes,actual);
    if(rc||*actual!=bytes){healthy=false;return rc?rc:static_cast<uint32_t>(PS2_FS_BAD_REPLY);}
    if(offset+bytes==CardBytes){
        uint64_t size=0;const uint32_t sized=ps2_sdk_fs_size(file,&size);
        if(sized||size!=CardBytes){healthy=false;return sized?sized:static_cast<uint32_t>(PS2_FS_BAD_REPLY);}
    }
    return 0;
}
bool Missing(uint32_t rc){return filesystem.api.is_not_found&&filesystem.api.is_not_found(rc);}
void Error(SdSaveResult&r,const char*message,bool poison=true){r.success=false;r.error=message;if(poison)healthy=false;}
// -1 is missing, 0 is failure, 1 is an authoritative exact-size card.
int OpenCard(const std::string&key,Ps2SdkFile&file,SdSaveResult&r,bool snapshot=false){
    const auto path=Name(key);
    uint32_t rc=ps2_sdk_fs_open(&filesystem,path.c_str(),PS2_FS_READ,&file);
    // The SDK client rejects further calls itself if a transport result
    // made ownership uncertain. Only retry the read-only source open.
    if(snapshot&&rc&&!Missing(rc))
        rc=ps2_sdk_fs_open(&filesystem,path.c_str(),PS2_FS_READ,&file);
    if(rc){
        if(Missing(rc))return -1;
        std::fprintf(stderr,"PS2 raw SD card open failed (0x%08x)\n",rc);
        Error(r,"Cannot open authoritative raw SD card",!snapshot);
        return 0;
    }
    r.found=true;r.bytes=file.size;
    if(file.size!=CardBytes){ps2_sdk_fs_close(&file);Error(r,"Existing raw SD card has an invalid size; it was preserved");return 0;}
    return 1;
}
bool BackupLocal(const std::string&local){
    struct stat before{};if(::lstat(local.c_str(),&before))return errno==ENOENT;
    if(!S_ISREG(before.st_mode)||before.st_nlink!=1||before.st_size!=static_cast<off_t>(CardBytes))return false;
    const int in=::open(local.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(in<0)return false;
    struct stat opened{};bool ok=!::fstat(in,&opened)&&Same(before,opened);
    std::string pattern=local+".before-live-sd.XXXXXX";std::vector<char>name(pattern.begin(),pattern.end());name.push_back(0);
    const int out=ok?::mkstemp(name.data()):-1;ok=ok&&out>=0;std::array<unsigned char,Block> buffer;
    for(uint64_t off=0;ok&&off<CardBytes;){size_t n=std::min<uint64_t>(Block,CardBytes-off);ok=ReadAll(in,buffer.data(),n)&&WriteAll(out,buffer.data(),n);off+=n;}
    struct stat after{};if(::fstat(in,&after)||!Same(opened,after))ok=false;
    if(out>=0){if(posix::Sync(out))ok=false;if(::close(out))ok=false;}
    if(::close(in))ok=false;
    if(!ok&&out>=0)::unlink(name.data());
    return ok;
}
SdSaveResult Export(const std::string&local,const std::string&key,bool initial_restore=false){
    SdSaveResult r;if(!filesystem.ready||!healthy||!Key(key)||!Canonical(local)){Error(r,"Live SD card export is unavailable or path is invalid",false);return r;}
    for(auto&s:streams)if(s.live&&ps2_sdk_fs_flush(&s.file)){Error(r,"Cannot flush live SD card before export");return r;}
    Ps2SdkFile in{};const int status=OpenCard(key,in,r);
    if(status<0){
        // Absence is expected only before the first card import. Once bound,
        // the raw SD file is authoritative: an old local export is not a
        // replacement for a source that disappeared during the session.
        if(initial_restore&&bound_key.empty())r.success=true;
        else Error(r,"Authoritative raw SD card disappeared; stale local exports were preserved");
        return r;
    }
    if(!status)return r;
    std::string pattern=local+".live-sd.XXXXXX";std::vector<char>name(pattern.begin(),pattern.end());name.push_back(0);
    const int out=::mkstemp(name.data());bool ok=out>=0;std::array<unsigned char,Block> buffer;
    for(uint64_t off=0;ok&&off<CardBytes;){const size_t n=std::min<uint64_t>(Block,CardBytes-off);if(!SdRead(in,off,buffer.data(),n)){healthy=false;ok=false;break;}ok=WriteAll(out,buffer.data(),n);off+=n;}
    if(out>=0){if(posix::Sync(out))ok=false;if(::close(out))ok=false;}
    if(ps2_sdk_fs_close(&in)){healthy=false;ok=false;}
    if(ok)ok=BackupLocal(local);
    if(ok)ok=::rename(name.data(),local.c_str())==0;
    if(!ok){if(out>=0)::unlink(name.data());Error(r,"Cannot export raw SD card; existing local data was preserved",false);return r;}
    r.success=true;return r;
}
Stream* Lookup(uint64_t handle){
    if(!handle||(handle>>62))return nullptr;
    const unsigned index=(handle&255u);if(!index||index>streams.size())return nullptr;
    auto&s=streams[index-1];return s.live&&s.generation==(handle>>8)?&s:nullptr;
}
void Enter(uint64_t*previous){if(previous)*previous=0;}
void Restore(uint64_t){}
uint32_t Open(uint64_t*out,const char*path,int mode){
    OwnerLock lock;if(out)*out=0;
    if(!out||!path||bound_path!=path||!filesystem.ready||!healthy||mode<1||mode>3)return PS2_FS_INVALID;
    for(unsigned i=0;i<streams.size();i++)if(!streams[i].live){
        if(generation==MaxGeneration)return PS2_FS_UNSUPPORTED;
        auto&s=streams[i];const uint32_t rc=ps2_sdk_fs_open(&filesystem,Name(bound_key).c_str(),static_cast<unsigned>(mode)|PS2_FS_UPDATE_EXISTING,&s.file);
        if(rc){healthy=false;return rc;}
        if(s.file.size!=CardBytes){ps2_sdk_fs_close(&s.file);healthy=false;return PS2_FS_INVALID;}
        s.generation=++generation;s.live=true;s.readable=mode&PS2_FS_READ;*out=(s.generation<<8)|(i+1);return 0;
    }return PS2_FS_UNSUPPORTED;
}
void Close(uint64_t handle){OwnerLock lock;auto*s=Lookup(handle);if(!s){healthy=false;return;}if(ps2_sdk_fs_close(&s->file))healthy=false;s->live=false;}
uint32_t Size(int64_t*out,uint64_t handle){OwnerLock lock;auto*s=Lookup(handle);if(!s||!out||!healthy)return PS2_FS_INVALID;uint64_t size=0;const uint32_t rc=ps2_sdk_fs_size(&s->file,&size);if(rc||size!=CardBytes){healthy=false;return rc?rc:static_cast<uint32_t>(PS2_FS_INVALID);}*out=size;return 0;}
uint32_t Read(size_t*out,uint64_t handle,int64_t offset,void*data,size_t bytes){OwnerLock lock;if(out)*out=0;auto*s=Lookup(handle);if(!s||!out||!healthy||offset<0)return PS2_FS_INVALID;const size_t expected=static_cast<uint64_t>(offset)>=CardBytes?0:std::min<uint64_t>(bytes,CardBytes-static_cast<uint64_t>(offset));const uint32_t rc=ps2_sdk_fs_read(&s->file,offset,data,bytes,out);if(rc||*out!=expected){healthy=false;return rc?rc:static_cast<uint32_t>(PS2_FS_BAD_REPLY);}return 0;}
uint32_t Write(uint64_t handle,int64_t offset,const void*data,size_t bytes,const uint32_t*option){OwnerLock lock;auto*s=Lookup(handle);if(!s||!healthy||!option||*option!=1||offset<0||static_cast<uint64_t>(offset)>CardBytes||bytes>CardBytes-static_cast<uint64_t>(offset))return PS2_FS_INVALID;const uint32_t rc=ps2_sdk_fs_write(&s->file,offset,data,bytes);if(rc)healthy=false;return rc;}
uint32_t Flush(uint64_t handle){OwnerLock lock;auto*s=Lookup(handle);if(!s)return PS2_FS_INVALID;const uint32_t rc=ps2_sdk_fs_flush(&s->file);if(rc)healthy=false;return !healthy?(rc?rc:static_cast<uint32_t>(PS2_FS_BAD_REPLY)):0;}
uint32_t Resize(uint64_t handle,int64_t bytes){OwnerLock lock;if(!Lookup(handle)||!healthy)return PS2_FS_INVALID;return bytes==static_cast<int64_t>(CardBytes)?0:PS2_FS_UNSUPPORTED;}
uint32_t Create(const char*,int64_t,int){return PS2_FS_UNSUPPORTED;}
int IsMissing(uint32_t rc){return rc==0x202;}
const Ps2SdkFsApi bridge{Enter,Restore,Open,Close,Size,Read,Write,Flush,Resize,Create,IsMissing};
}

bool ConfigureLiveSdCard(const Ps2SdkFsApi&api,uint32_t(*rename_no_replace)(const char*,const char*),const std::string&root){
    OwnerLock lock;if(filesystem.ready||!rename_no_replace||root.size()>600||!Root(root)||!api.create||!api.write||!api.flush||!api.set_size||!api.is_not_found)return false;
    if(ps2_sdk_fs_initialize(&filesystem,&api))return false;
    routing_active.store(false,std::memory_order_release);
    directory=root;rename_file=rename_no_replace;bound_path.clear();bound_key.clear();healthy=true;return true;
}
SdSaveResult RestoreLiveSdCard(const std::string&local,const std::string&key){OwnerLock lock;return Export(local,key,true);}
SdSaveResult CreateLiveSdCard(const std::string&local,const std::string&key){
    OwnerLock lock;SdSaveResult r;
    if(!filesystem.ready||!healthy||!Key(key)||!Canonical(local)){Error(r,"Live SD import is unavailable or path is invalid",false);return r;}
    Ps2SdkFile existing{};const int status=OpenCard(key,existing,r);
    if(status>=0){if(status&&ps2_sdk_fs_close(&existing)){Error(r,"Cannot close existing raw SD card");return r;}r.success=status==1;return r;}
    const int in=::open(local.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat before{};
    if(in<0||::fstat(in,&before)||!S_ISREG(before.st_mode)||before.st_nlink!=1||before.st_size!=static_cast<off_t>(CardBytes)){if(in>=0)::close(in);Error(r,"Import requires a private complete original raw PS2 card",false);return r;}
    const std::string temp=Name(key)+".new."+std::to_string(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()))+"."+std::to_string(++temporary_sequence);
    uint64_t previous=0;filesystem.api.enter(&previous);uint32_t rc=filesystem.api.create(temp.c_str(),CardBytes,0);filesystem.api.restore(previous);
    Ps2SdkFile out{};if(!rc)rc=ps2_sdk_fs_open(&filesystem,temp.c_str(),PS2_FS_READ|PS2_FS_WRITE|PS2_FS_UPDATE_EXISTING,&out);
    std::array<unsigned char,Block>buffer;uint32_t crc=~0u;bool ok=!rc;
    for(uint64_t off=0;ok&&off<CardBytes;){const size_t n=std::min<uint64_t>(Block,CardBytes-off);if(!ReadAll(in,buffer.data(),n)){ok=false;break;}crc=Crc(crc,buffer.data(),n);rc=ps2_sdk_fs_write(&out,off,buffer.data(),n);ok=!rc;off+=n;}
    struct stat after{};if(::fstat(in,&after)||!Same(before,after))ok=false;if(::close(in))ok=false;
    if(out.opened&&ps2_sdk_fs_close(&out))ok=false;
    Ps2SdkFile verify{};if(ok){rc=ps2_sdk_fs_open(&filesystem,temp.c_str(),PS2_FS_READ,&verify);ok=!rc&&verify.size==CardBytes;}
    uint32_t read_crc=~0u;
    for(uint64_t off=0;ok&&off<CardBytes;){size_t n=std::min<uint64_t>(Block,CardBytes-off);if(!SdRead(verify,off,buffer.data(),n)){ok=false;break;}read_crc=Crc(read_crc,buffer.data(),n);off+=n;}
    if(verify.opened&&ps2_sdk_fs_close(&verify))ok=false;
    if(crc!=read_crc)ok=false;
    if(ok){filesystem.api.enter(&previous);rc=rename_file(temp.c_str(),Name(key).c_str());filesystem.api.restore(previous);ok=!rc;}
    if(!ok){Error(r,"Cannot publish verified raw SD card; existing SD files and temporary were preserved");return r;}
    r.success=true;r.found=true;r.bytes=CardBytes;return r;
}
bool BindLiveSdCard(const std::string&logical,const std::string&key){
    OwnerLock lock;if(!filesystem.ready||!healthy||!Canonical(logical)||!Key(key))return false;for(const auto&s:streams)if(s.live)return false;
    SdSaveResult r;Ps2SdkFile file{};const int status=OpenCard(key,file,r);if(status!=1)return false;if(ps2_sdk_fs_close(&file)){healthy=false;return false;}bound_path=logical;bound_key=key;routing_active.store(true,std::memory_order_release);return true;
}
bool IsLiveSdCardPath(const char*path){if(!routing_active.load(std::memory_order_acquire))return false;OwnerLock lock;return path&&!bound_path.empty()&&bound_path==path;}
const Ps2SdkFsApi& LiveSdCardBridgeApi(){return bridge;}
SdSaveResult ExportLiveSdCard(const std::string&local){OwnerLock lock;if(bound_key.empty()){SdSaveResult r;Error(r,"No live SD card is bound",false);return r;}return Export(local,bound_key);}
SdSaveResult SnapshotLiveSdCard(){
    OwnerLock lock;SdSaveResult r;
    if(!filesystem.ready||!healthy||bound_key.empty()){
        Error(r,"Live SD card snapshot is unavailable",false);return r;
    }
    Ps2SdkFile* source = nullptr;
    for (auto& stream : streams)
    {
        if (!stream.live)
            continue;
        if (ps2_sdk_fs_flush(&stream.file))
        {
            Error(r, "Cannot flush live SD card before snapshot");
            return r;
        }
        if (stream.readable)
            source = &stream.file;
    }
    if (source)
    {
        uint64_t bytes = 0;
        if (ps2_sdk_fs_size(source, &bytes) || bytes != CardBytes)
        {
            Error(r, "Cannot verify size of the open raw SD card");
            return r;
        }
        return StoreSdSaveFromReader(bound_key, CardBytes, SnapshotRead, source);
    }

    Ps2SdkFile input{};
    const int status = OpenCard(bound_key, input, r, true);
    if (status < 0) {
        Error(r, "Authoritative raw SD card disappeared; no snapshot was published");
        return r;
    }
    if (!status)
        return r;
    r = StoreSdSaveFromReader(bound_key, CardBytes, SnapshotRead, &input);
    if (ps2_sdk_fs_close(&input))
        Error(r, "Cannot close raw SD card after snapshot");
    return r;
}
bool LiveSdCardHealthy(){OwnerLock lock;return filesystem.ready&&healthy;}
bool ShutdownLiveSdCard(){OwnerLock lock;for(const auto&s:streams)if(s.live)return false;if(filesystem.ready&&filesystem.opened!=filesystem.closed)return false;routing_active.store(false,std::memory_order_release);filesystem={};directory.clear();bound_path.clear();bound_key.clear();rename_file=nullptr;healthy=false;return true;}
}
