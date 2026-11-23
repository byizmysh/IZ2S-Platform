#include "ps2_resource_romfs.h"
#include <string.h>
#include <limits.h>

static uint32_t u32(const unsigned char*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t u64(const unsigned char*p){return (uint64_t)u32(p)|(uint64_t)u32(p+4)<<32;}
static uint64_t fnv(const unsigned char*p,size_t n){uint64_t h=UINT64_C(14695981039346656037);while(n--){h^=*p++;h*=UINT64_C(1099511628211);}return h;}
static int canonical(const char*p,size_t*n){
    if(!p)return 0;
    size_t k=0;while(k<PS2_ROMFS_MAX_PATH&&p[k])++k;
    if(k<10||k==PS2_ROMFS_MAX_PATH||memcmp(p,"/resources",10)||(p[10]&&p[10]!='/'))return 0;
    if(k>10&&p[k-1]=='/')return 0;
    for(size_t i=1;i<k;){size_t start=i;
        while(i<k&&p[i]!='/'){unsigned char c=(unsigned char)p[i++];if(c<32||c==127||c=='\\'||c==':')return 0;}
        size_t length=i-start;if(!length||(length==1&&p[start]=='.')||(length==2&&p[start]=='.'&&p[start+1]=='.'))return 0;
        if(i<k)++i;
    }
    *n=k;return 1;
}
static const unsigned char*index_at(const Ps2ResourceRomfs*r,uint32_t i){return r->blob+64+(size_t)i*40;}
int ps2_resource_romfs_entry(const Ps2ResourceRomfs*r,uint32_t i,Ps2ResourceRomfsEntry*out){
    if(!r||!r->mounted||!out||i>=r->entries)return -22;
    const unsigned char*e=index_at(r,i);
    *out=(Ps2ResourceRomfsEntry){(const char*)r->blob+r->paths_offset+u32(e),
        u32(e+8)==PS2_ROMFS_FILE?r->blob+u64(e+16):0,u64(e+24),u32(e+8),i};return 0;
}
int ps2_resource_romfs_mount(Ps2ResourceRomfs*r,const void*blob,size_t bytes){
    if(!r||r->mounted||!blob||bytes<64||(uintptr_t)blob>UINTPTR_MAX-bytes)return -22;
    if((uintptr_t)r<(uintptr_t)blob+bytes&&(uintptr_t)blob<(uintptr_t)r+sizeof(*r))return -22;
    const unsigned char*b=blob;
    if(memcmp(b,"PS2ROMF1",8)||u32(b+8)!=1||u32(b+12)!=64||u64(b+16)!=bytes||u64(b+24)!=64||u32(b+36)!=40)return -22;
    uint32_t count=u32(b+32);uint64_t paths=u64(b+40),data=u64(b+48);
    if(!count||count>PS2_ROMFS_MAX_ENTRIES||paths!=64+(uint64_t)count*40||
       paths>data||data>bytes||(data&7)||data-paths>(uint64_t)count*PS2_ROMFS_MAX_PATH+7||
       fnv(b+64,(size_t)data-64)!=u64(b+56))return -22;
    Ps2ResourceRomfs candidate={b,bytes,paths,data,count,1};
    uint64_t next_path=0,next_data=data;
    const char*previous=0;
    for(uint32_t i=0;i<count;i++){
        const unsigned char*e=b+64+(size_t)i*40;uint32_t off=u32(e),length=u32(e+4),kind=u32(e+8);
        if(off!=next_path||!length||length>PS2_ROMFS_MAX_PATH||length>data-paths-off||u32(e+12)||
           (kind!=PS2_ROMFS_FILE&&kind!=PS2_ROMFS_DIRECTORY))return -22;
        const char*path=(const char*)b+paths+off;size_t n=0;
        if(path[length-1]||memchr(path,0,length-1)||!canonical(path,&n)||n+1!=length||
           (previous&&strcmp(previous,path)>=0))return -22;
        if(!i){if(strcmp(path,"/resources")||kind!=PS2_ROMFS_DIRECTORY)return -22;}
        else{
            const char*slash=strrchr(path,'/');int parent=0;
            for(uint32_t j=0;j<i;j++){
                const unsigned char*p=index_at(&candidate,j);const char*name=(const char*)b+paths+u32(p);
                if(u32(p+8)==PS2_ROMFS_DIRECTORY&&strlen(name)==(size_t)(slash-path)&&!memcmp(name,path,(size_t)(slash-path))){parent=1;break;}
            }
            if(!parent)return -22;
        }
        const uint64_t at=u64(e+16),size=u64(e+24),hash=u64(e+32);
        if(kind==PS2_ROMFS_DIRECTORY){if(at||size||hash)return -22;}
        else{
            if(at!=next_data||at>bytes||size>INT64_MAX||size>bytes-at||fnv(b+at,(size_t)size)!=hash)return -22;
            next_data=at+size;while(next_data&7){if(next_data>=bytes||b[next_data])return -22;++next_data;}
        }
        next_path+=length;previous=path;
    }
    for(uint64_t i=paths+next_path;i<data;i++)if(b[i])return -22;
    if(next_data!=bytes)return -22;
    *r=candidate;return 0;
}
int ps2_resource_romfs_lookup(const Ps2ResourceRomfs*r,const char*path,Ps2ResourceRomfsEntry*out){
    size_t n;if(!r||!r->mounted||!out||!canonical(path,&n))return -22;
    uint32_t lo=0,hi=r->entries;
    while(lo<hi){uint32_t at=lo+(hi-lo)/2;const char*p=(const char*)r->blob+r->paths_offset+u32(index_at(r,at));int c=strcmp(path,p);
        if(!c)return ps2_resource_romfs_entry(r,at,out);if(c<0)hi=at;else lo=at+1;}
    return -2;
}
int ps2_resource_romfs_read(const Ps2ResourceRomfs*r,uint32_t index,uint64_t offset,void*out,size_t n,size_t*actual){
    if(!actual)return -22;*actual=0;Ps2ResourceRomfsEntry e;int rc=ps2_resource_romfs_entry(r,index,&e);if(rc)return rc;
    if(e.kind!=PS2_ROMFS_FILE)return -21;
    if(!out&&n)return -14;
    if(n&&(uintptr_t)out>UINTPTR_MAX-n)return -14;
    if(offset>=e.bytes)return 0;uint64_t available=e.bytes-offset;if(available<n)n=(size_t)available;
    if(n)memcpy(out,e.data+(size_t)offset,n);*actual=n;return 0;
}
int ps2_resource_romfs_child(const Ps2ResourceRomfs*r,uint32_t directory,uint64_t position,Ps2ResourceRomfsEntry*out){
    Ps2ResourceRomfsEntry dir;int rc=ps2_resource_romfs_entry(r,directory,&dir);if(rc||!out)return rc?rc:-22;
    if(dir.kind!=PS2_ROMFS_DIRECTORY)return -20;
    const size_t n=strlen(dir.path);uint64_t child=0;
    for(uint32_t i=directory+1;i<r->entries;i++){
        Ps2ResourceRomfsEntry e;ps2_resource_romfs_entry(r,i,&e);
        if(strncmp(dir.path,e.path,n)||e.path[n]!='/')continue;
        if(!strchr(e.path+n+1,'/')&&child++==position){*out=e;return 1;}
    }return 0;
}
