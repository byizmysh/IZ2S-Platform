#include "native_ifile.h"
#include <string.h>
#include <limits.h>
_Static_assert(sizeof(Ps2IFileMountDescription)==72,"IFile mount ABI");
_Static_assert(sizeof(Ps2IFileDescription)==280,"logical file ABI");
_Static_assert(sizeof(Ps2IFileSegment)==288,"segment ABI");
static int range(uint64_t a,uint64_t n,unsigned alignment){return a&&!(a&(alignment-1))&&n&&a<(1ull<<39)&&n<=(1ull<<39)-a;}
static int separate(uint64_t a,uint64_t n,uint64_t b,uint64_t m){return a+n<=b||b+m<=a;}
static int canonical(const char*p){
    size_t n=strnlen(p,256);if(!n||n==256||p[0]!='/'||p[n-1]=='/')return 0;
    for(size_t i=1;i<n;){size_t begin=i;while(i<n&&p[i]!='/'){unsigned c=(unsigned char)p[i++];if(c<32||c==127||c=='\\'||c==':')return 0;}
        size_t k=i-begin;if(!k||(k==1&&p[begin]=='.')||(k==2&&p[begin]=='.'&&p[begin+1]=='.'))return 0;i++;}return 1;
}
int ps2_ifile_initialize(Ps2IFileMount*m,const Ps2IFileMountDescription*c,Ps2IFileSend send){
    if(!m||!c||!send||m->ready||m->poisoned||c->magic!=PS2_IFILE_MAGIC||c->version!=1||c->size!=sizeof(*c)||c->flags!=1||
       (!c->token[0]&&!c->token[1])||!c->file_count||c->file_count>PS2_IFILE_FILES||!c->segment_count||c->segment_count>PS2_IFILE_SEGMENTS||
       c->capacity!=PS2_IFILE_CACHE||!range(c->buffer,c->capacity,4096)||
       !range(c->files,c->file_count*sizeof(Ps2IFileDescription),8)||!range(c->segments,c->segment_count*sizeof(Ps2IFileSegment),8))return PS2_IFILE_INVALID;
    const uint64_t p[]={(uintptr_t)m,(uintptr_t)c,c->buffer,c->files,c->segments};
    const uint64_t n[]={sizeof(*m),sizeof(*c),c->capacity,c->file_count*sizeof(Ps2IFileDescription),c->segment_count*sizeof(Ps2IFileSegment)};
    for(unsigned i=0;i<5;i++)for(unsigned j=0;j<i;j++)if(!separate(p[i],n[i],p[j],n[j]))return PS2_IFILE_INVALID;
    const Ps2IFileDescription*f=(const void*)(uintptr_t)c->files;
    const Ps2IFileSegment*s=(const void*)(uintptr_t)c->segments;uint64_t used=0;
    for(unsigned i=0;i<c->segment_count;i++){
        if(!canonical(s[i].sd_path)||strncmp(s[i].sd_path,"/sd/izmysh/ps2/",15)||!s[i].handle||s[i].handle>=0x80000000u||s[i].reserved||
           !s[i].bytes||s[i].file_bytes>INT64_MAX||s[i].source_offset>s[i].file_bytes||s[i].bytes>s[i].file_bytes-s[i].source_offset)return PS2_IFILE_INVALID;
        for(unsigned j=0;j<i;j++)if(s[i].handle==s[j].handle&&(s[i].file_bytes!=s[j].file_bytes||strcmp(s[i].sd_path,s[j].sd_path)))return PS2_IFILE_INVALID;
    }
    for(unsigned i=0;i<c->file_count;i++){
        if(!canonical(f[i].logical)||!f[i].bytes||f[i].bytes>INT64_MAX||f[i].reserved||f[i].role>2||!f[i].segment_count||
           f[i].first_segment>=c->segment_count||f[i].segment_count>c->segment_count-f[i].first_segment)return PS2_IFILE_INVALID;
        if(f[i].role==PS2_IFILE_ROLE_ISO&&strncmp(f[i].logical,"/sd/izmysh/ps2/",15))return PS2_IFILE_INVALID;
        for(unsigned j=0;j<i;j++)if(!strcmp(f[i].logical,f[j].logical))return PS2_IFILE_INVALID;
        uint64_t total=0;for(unsigned j=0;j<f[i].segment_count;j++){
            unsigned at=f[i].first_segment+j;uint64_t bit=1ull<<at;if(used&bit)return PS2_IFILE_INVALID;used|=bit;
            if(s[at].bytes>(uint64_t)INT64_MAX-total)return PS2_IFILE_INVALID;total+=s[at].bytes;
        }if(total!=f[i].bytes)return PS2_IFILE_INVALID;
    }
    if(used!=(c->segment_count==64?UINT64_MAX:(1ull<<c->segment_count)-1))return PS2_IFILE_INVALID;
    memset(m,0,sizeof(*m));memcpy(m->files,f,c->file_count*sizeof(*f));memcpy(m->segments,s,c->segment_count*sizeof(*s));
    m->file_count=c->file_count;m->segment_count=c->segment_count;m->send=send;m->buffer=(void*)(uintptr_t)c->buffer;m->capacity=c->capacity;m->cache_file=UINT32_MAX;m->ready=1;return 0;
}
int ps2_ifile_find(const Ps2IFileMount*m,const char*path){if(!m||!m->ready||!path)return -1;for(unsigned i=0;i<m->file_count;i++)if(!strcmp(m->files[i].logical,path))return (int)i;return -1;}
static uint32_t fail(Ps2IFileMount*m,uint32_t error,int poison){m->error=error;if(poison){m->poisoned=1;m->cache_bytes=0;}return error;}
static uint32_t request(Ps2IFileMount*m,uint32_t handle,unsigned command,uint64_t off,uint64_t n,uint64_t*out){
    if(!m->ready||m->poisoned||m->in_flight)return PS2_IFILE_RETAINED;
    _Alignas(4096) unsigned char message[4096];memset(message,0,sizeof(message));uint32_t*p=(void*)message;unsigned raw=n?32:16;
    p[0]=4u|(n?1u<<24:0);p[1]=n?14:8;
    if(n){uint64_t a=(uintptr_t)m->buffer;p[2]=(uint32_t)n;p[3]=(uint32_t)a;p[4]=1u|((uint32_t)(a>>36)&7u)<<2|((uint32_t)(a>>32)&15u)<<28;}
    p[raw/4]=0x49434653;p[raw/4+2]=command;
    if(n){uint64_t input[]={0,off,n};memcpy(message+raw+16,input,sizeof(input));}
    m->in_flight=1;m->requests++;uint32_t rc=m->send(message,sizeof(message),handle);m->in_flight=0;
    if(rc){m->transport=rc;return fail(m,rc,1);}
    unsigned at=8,copies=0,moves=0;
    if(p[1]&0x80000000u){uint32_t special=p[2];copies=(special>>1)&15;moves=(special>>5)&15;at+=4;if(special&1)at+=8;
        for(unsigned i=0;i<copies+moves;i++){m->unexpected_handles[m->unexpected_count++]=*(uint32_t*)(message+at);at+=4;}}
    if(copies||moves)return fail(m,PS2_IFILE_PACKET,1); // retain, never close unexpected/borrowed capabilities
    at+=((p[0]>>16)&15)*8+(((p[0]>>20)&15)+((p[0]>>24)&15)+((p[0]>>28)&15))*12;at=(at+15)&~15u;
    if(at>4096-24||(p[1]&1023)<6||(p[1]&1023)*4>4096-at||*(uint32_t*)(message+at)!=0x4f434653)
        return fail(m,PS2_IFILE_PACKET,1);
    rc=*(uint32_t*)(message+at+8);m->result=rc;if(rc)return fail(m,rc,0);
    memcpy(out,message+at+16,8);return 0;
}
uint32_t ps2_ifile_size(Ps2IFileMount*m,unsigned index,uint64_t*out){
    if(!m||!out||((uintptr_t)out&7)||!m->ready||index>=m->file_count||!separate((uintptr_t)out,8,(uintptr_t)m,sizeof(*m))||!separate((uintptr_t)out,8,(uintptr_t)m->buffer,m->capacity))return PS2_IFILE_INVALID;*out=0;
    const Ps2IFileDescription*f=&m->files[index];uint64_t total=0;
    for(unsigned i=0;i<f->segment_count;i++){
        const Ps2IFileSegment*s=&m->segments[f->first_segment+i];uint64_t actual=0;m->size_requests++;
        uint32_t rc=request(m,s->handle,4,0,0,&actual);if(rc)return rc;
        if(actual!=s->file_bytes)return fail(m,PS2_IFILE_SIZE,1);total+=s->bytes;
    }*out=total;return 0;
}
uint32_t ps2_ifile_read(Ps2IFileMount*m,unsigned index,uint64_t offset,void*out,size_t length,size_t*actual){
    if(!m||!actual||((uintptr_t)actual&7)||!m->ready||m->poisoned||index>=m->file_count||(!out&&length)||offset>INT64_MAX||length>INT64_MAX||length>UINTPTR_MAX-(uintptr_t)out||
       !separate((uintptr_t)actual,sizeof(*actual),(uintptr_t)m,sizeof(*m))||!separate((uintptr_t)actual,sizeof(*actual),(uintptr_t)m->buffer,m->capacity)||
       (length&&(!separate((uintptr_t)out,length,(uintptr_t)actual,sizeof(*actual))||!separate((uintptr_t)out,length,(uintptr_t)m,sizeof(*m))||!separate((uintptr_t)out,length,(uintptr_t)m->buffer,m->capacity))))return PS2_IFILE_INVALID;
    *actual=0;
    const Ps2IFileDescription*f=&m->files[index];if(offset>=f->bytes||!length)return 0;
    if(length>f->bytes-offset)length=(size_t)(f->bytes-offset);unsigned char*dest=out;size_t delivered=0;
    while(delivered<length){
        if(m->cache_file!=index||offset<m->cache_offset||offset-m->cache_offset>=m->cache_bytes){
            uint64_t logical=0;const Ps2IFileSegment*s=0;
            for(unsigned i=0;i<f->segment_count;i++){s=&m->segments[f->first_segment+i];if(offset-logical<s->bytes)break;logical+=s->bytes;s=0;}
            if(!s)return fail(m,PS2_IFILE_INVALID,1);
            uint64_t within=offset-logical,wanted=s->bytes-within;if(wanted>m->capacity)wanted=m->capacity;uint64_t got=0;
            uint32_t rc=request(m,s->handle,0,s->source_offset+within,wanted,&got);if(rc)return rc;
            if(got!=wanted)return fail(m,PS2_IFILE_SHORT,1);
            m->cache_file=index;m->cache_offset=offset;m->cache_bytes=got;
            m->counters[index].physical_reads++;m->counters[index].physical_bytes+=got;
            if(offset+got>m->counters[index].max_logical_end)m->counters[index].max_logical_end=offset+got;
        }
        size_t at=(size_t)(offset-m->cache_offset),n=(size_t)m->cache_bytes-at;if(n>length-delivered)n=length-delivered;
        memcpy(dest+delivered,(unsigned char*)m->buffer+at,n);delivered+=n;offset+=n;
    }m->counters[index].delivered_bytes+=delivered;*actual=delivered;return 0;
}
