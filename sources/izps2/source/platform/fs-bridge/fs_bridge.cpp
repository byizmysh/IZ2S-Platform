#include "fs_bridge.h"
#include "common/HostVFS.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <new>
#include <limits>
namespace {
struct Mount {char logical[PS2_FS_PATH_MAX],physical[PS2_FS_PATH_MAX];bool writable;};
static Mount mounts[5];static size_t mount_count;static Ps2SdkFs *fs;
struct Stream {Ps2SdkFile file{};uint64_t offset=0;std::mutex mutex;};
static bool canonical(const char *p){
    if(!p || *p!='/')return false;size_t n=strnlen(p,PS2_FS_PATH_MAX);
    if(!n || n==PS2_FS_PATH_MAX || (n>1 && p[n-1]=='/'))return false;
    for(size_t i=1;i<n;){size_t begin=i;while(i<n && p[i]!='/'){unsigned char c=p[i++];if(c<32 || c==127 || c=='\\' || c==':')return false;}
        size_t k=i-begin;if(!k || (k==1 && p[begin]=='.') || (k==2 && p[begin]=='.' && p[begin+1]=='.'))return false;i++;}
    return true;
}
static const Mount *translate(const char *path,char *out){
    if(!canonical(path))return nullptr;
    for(size_t i=0;i<mount_count;i++){
        size_t n=std::strlen(mounts[i].logical),m=std::strlen(mounts[i].physical);
        if(std::strncmp(path,mounts[i].logical,n) || (path[n] && path[n]!='/'))continue;
        size_t suffix=std::strlen(path+n);if(m+suffix>=PS2_FS_PATH_MAX)return nullptr;
        std::memcpy(out,mounts[i].physical,m);std::memcpy(out+m,path+n,suffix+1);return &mounts[i];
    }return nullptr;
}
static int fail(uint32_t){errno=EIO;return -1;}
static void *open(const char *path,unsigned mode,unsigned){
    char physical[PS2_FS_PATH_MAX];const Mount *mount=translate(path,physical);
    if(!mount){errno=EINVAL;return nullptr;}
    if((mode&PS2_FS_WRITE) && !mount->writable){errno=EROFS;return nullptr;}
    auto *s=new(std::nothrow) Stream;if(!s){errno=ENOMEM;return nullptr;}
    uint32_t rc=ps2_sdk_fs_open(fs,physical,mode,&s->file);if(rc){delete s;fail(rc);return nullptr;}return s;
}
static int close(void *p){auto *s=static_cast<Stream*>(p);uint32_t rc;
    {std::lock_guard<std::mutex> lock(s->mutex);rc=ps2_sdk_fs_close(&s->file);}delete s;return rc?fail(rc):0;}
static s64 size(void *p){auto *s=static_cast<Stream*>(p);std::lock_guard<std::mutex> lock(s->mutex);uint64_t n;
    return ps2_sdk_fs_size(&s->file,&n)?-1:static_cast<s64>(n);}
static s64 tell(void *p){auto *s=static_cast<Stream*>(p);std::lock_guard<std::mutex> lock(s->mutex);return static_cast<s64>(s->offset);}
static s64 seek(void *p,s64 off,int whence){auto *s=static_cast<Stream*>(p);std::lock_guard<std::mutex> lock(s->mutex);
    uint64_t base=whence==0?0:whence==1?s->offset:whence==2?s->file.size:UINT64_MAX;
    if(base>INT64_MAX || (off>=0 && static_cast<uint64_t>(off)>INT64_MAX-base) || (off<0 && static_cast<uint64_t>(-(off+1))+1>base)){errno=EINVAL;return -1;}
    s->offset=off>=0?base+static_cast<uint64_t>(off):base-(static_cast<uint64_t>(-(off+1))+1);return static_cast<s64>(s->offset);
}
static s64 read(void *p,void *buffer,u64 length){auto *s=static_cast<Stream*>(p);std::lock_guard<std::mutex> lock(s->mutex);
    if(length>SIZE_MAX || length>INT64_MAX)return fail(PS2_FS_INVALID);size_t got=0;
    uint32_t rc=ps2_sdk_fs_read(&s->file,s->offset,buffer,static_cast<size_t>(length),&got);if(rc)return fail(rc);s->offset+=got;return static_cast<s64>(got);
}
static s64 write(void *p,const void *buffer,u64 length){auto *s=static_cast<Stream*>(p);std::lock_guard<std::mutex> lock(s->mutex);
    if(length>SIZE_MAX || length>INT64_MAX)return fail(PS2_FS_INVALID);
    uint32_t rc=ps2_sdk_fs_write(&s->file,s->offset,buffer,static_cast<size_t>(length));if(rc)return fail(rc);s->offset+=length;return static_cast<s64>(length);
}
static int flush(void *p){auto *s=static_cast<Stream*>(p);std::lock_guard<std::mutex> lock(s->mutex);uint32_t rc=ps2_sdk_fs_flush(&s->file);return rc?fail(rc):0;}
static int truncate(void *p,s64 n){auto *s=static_cast<Stream*>(p);std::lock_guard<std::mutex> lock(s->mutex);uint32_t rc=n<0?PS2_FS_INVALID:ps2_sdk_fs_resize(&s->file,n);return rc?fail(rc):0;}
}
bool ps2_install_file_bridge(Ps2SdkFs *state,const Ps2FsMount *items,size_t count){
    if(!state || !state->ready || !items || !count || count>sizeof mounts/sizeof *mounts || HostVFS::IsInstalled())return false;
    for(size_t i=0;i<count;i++){
        if(!canonical(items[i].logical) || !items[i].logical[1] || !items[i].physical || !*items[i].physical || std::strlen(items[i].physical)>=PS2_FS_PATH_MAX)return false;
        const char *body=std::strchr(items[i].physical,':');
        if(body){if(body==items[i].physical)return false;for(const char *c=items[i].physical;c<body;c++)if(!((*c>='a'&&*c<='z')||(*c>='A'&&*c<='Z')||(*c>='0'&&*c<='9')||*c=='_'||*c=='-'))return false;body++;}
        else body=items[i].physical;
        if(!canonical(body))return false;
        for(size_t j=0;j<i;j++){
            size_t n=std::min(std::strlen(items[j].logical),std::strlen(items[i].logical));
            if(!std::strncmp(items[j].logical,items[i].logical,n) &&
               (!items[j].logical[n] || items[j].logical[n]=='/') && (!items[i].logical[n] || items[i].logical[n]=='/'))return false;
        }
    }
    for(size_t i=0;i<count;i++){std::strcpy(mounts[i].logical,items[i].logical);std::strcpy(mounts[i].physical,items[i].physical);mounts[i].writable=items[i].writable;}
    fs=state;mount_count=count;HostVFS::Ops ops{};ops.open=open;ops.close=close;ops.size=size;ops.tell=tell;ops.seek=seek;ops.read=read;ops.write=write;ops.flush=flush;ops.truncate=truncate;HostVFS::Install(ops);return HostVFS::IsInstalled();
}
