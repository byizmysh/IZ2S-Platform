#include "native_ifile.h"
#include "native_sd_write.h"
#include <string.h>
typedef struct {
 uint32_t magic,version,size,flags;
 uint64_t sdk,mount,reader,write_config,writer,output,transfer,transfer_bytes;
 char path[256],renamed[256];
} Probe;
_Static_assert(sizeof(Probe)==592,"probe ABI");
static uint32_t crc(const void*data,size_t n){
 const unsigned char*p=data;uint32_t c=~0u;
 while(n--){c^=*p++;for(unsigned b=0;b<8;b++)c=(c>>1)^(0xedb88320u&(0u-(c&1)));}return c^~0u;
}
__attribute__((section(".text.entry"))) uint64_t ps2_sd_probe_entry(const Probe*c){
 if(!c||c->magic!=0x31504453||c->version!=1||c->size!=sizeof(*c)||c->flags!=1||c->transfer_bytes<131103)return 0xf801;
 uint64_t*out=(void*)(uintptr_t)c->output;Ps2IFileMount*r=(void*)(uintptr_t)c->reader;
 Ps2SdWrite*w=(void*)(uintptr_t)c->writer;const Ps2SdWriteConfig*config=(void*)(uintptr_t)c->write_config;
 unsigned char*buffer=(void*)(uintptr_t)c->transfer;uint32_t rc=0;uint64_t h=0;size_t actual=0;
 memset(out,0,256);out[0]=0x31524453;
#define STAGE(n) out[1]=(n)
#define TRY(x) do{rc=(x);if(rc)goto finish;}while(0)
#define REQUIRE(x) do{if(!(x)){rc=0xf802;goto finish;}}while(0)
 STAGE(1);TRY(ps2_ifile_sdk_initialize(r,(void*)(uintptr_t)c->mount,c->sdk,6164480));
 REQUIRE(r->file_count==2);
 for(unsigned i=0;i<2;i++){
  STAGE(2+i);uint64_t bytes=0;TRY(ps2_ifile_size(r,i,&bytes));out[3+i]=bytes;
  TRY(ps2_ifile_read(r,i,0,buffer,4096,&actual));REQUIRE(actual==4096);out[5+i]=crc(buffer,actual);
 }
 STAGE(4);REQUIRE(config->sdk==c->sdk);
 static const unsigned char close_cut[]={0xc1,2,0,0xd4,0xc0,3,0x5f,0xd6};
 REQUIRE(!memcmp((void*)(uintptr_t)(c->sdk+0x1583ec),close_cut,sizeof close_cut));
 TRY(ps2_sd_write_initialize(w,config,(Ps2SdSend)(uintptr_t)(c->sdk+0x15845c),(Ps2SdClose)(uintptr_t)(c->sdk+0x1583ec)));
 uint32_t type=2;TRY(ps2_sd_write_type(w,&type,"/izmysh/ps2/saves/cards"));REQUIRE(type==0);
 STAGE(5);TRY(ps2_sd_write_create(w,c->path,0,0));out[9]=1;
 STAGE(6);TRY(ps2_sd_write_open(w,&h,c->path,3));
 STAGE(7);TRY(ps2_sd_write_resize(w,h,131103));
 for(size_t i=0;i<131103;i++)buffer[i]=(unsigned char)(i*37+11);
 out[7]=crc(buffer,131103);uint32_t option=1;
 STAGE(8);TRY(ps2_sd_write_write(w,h,0,buffer,131103,&option));
 STAGE(9);TRY(ps2_sd_write_flush(w,h));
 STAGE(10);memset(buffer,0,131103);TRY(ps2_sd_write_read(w,&actual,h,0,buffer,131103));
 REQUIRE(actual==131103&&crc(buffer,actual)==out[7]);
 STAGE(11);int64_t size=-1;TRY(ps2_sd_write_size(w,&size,h));REQUIRE(size==131103);
 STAGE(12);ps2_sd_write_close(w,h);h=0;REQUIRE(ps2_sd_write_healthy(w));
 STAGE(13);TRY(ps2_sd_write_open(w,&h,c->path,1));
 STAGE(14);memset(buffer,0,131103);TRY(ps2_sd_write_read(w,&actual,h,0,buffer,131103));
 out[8]=crc(buffer,actual);REQUIRE(actual==131103&&out[8]==out[7]);
 ps2_sd_write_close(w,h);h=0;REQUIRE(ps2_sd_write_healthy(w));
 STAGE(15);TRY(ps2_sd_write_rename(w,c->path,c->renamed));out[10]=1;
 STAGE(16);
finish:
 if(h)ps2_sd_write_close(w,h);
 for(unsigned i=0;i<32;i++)if(w->files[i])ps2_sd_write_close(w,w->files[i]);
 out[2]=rc;out[11]=w->opened;out[12]=w->closed;out[13]=ps2_sd_write_healthy(w);
 out[14]=w->poisoned;out[15]=w->leaked;out[16]=1;
 for(unsigned i=0;i<32;i++)if(w->files[i])out[16]=0;
 out[17]=w->read_bytes;out[18]=w->written_bytes;out[19]=r->in_flight;
 out[20]=r->unexpected_count;out[21]=r->transport;out[22]=r->result;
 if(!rc){if(!ps2_sd_write_shutdown(w)){out[2]=0xf803;return out[2];}STAGE(17);}
 return out[2];
}
// No libc personality or global state is needed by this finite SDK probe.
void*memset(void*d,int c,size_t n){unsigned char*p=d;while(n--)*p++=(unsigned char)c;return d;}
void*memcpy(void*d,const void*s,size_t n){unsigned char*p=d;const unsigned char*q=s;while(n--)*p++=*q++;return d;}
int memcmp(const void*a,const void*b,size_t n){const unsigned char*p=a,*q=b;while(n--){if(*p!=*q)return *p-*q;p++;q++;}return 0;}
size_t strlen(const char*p){size_t n=0;while(p[n])n++;return n;}
size_t strnlen(const char*p,size_t limit){size_t n=0;while(n<limit&&p[n])n++;return n;}
int strncmp(const char*a,const char*b,size_t n){while(n--){if(*a!=*b)return (unsigned char)*a-(unsigned char)*b;if(!*a)return 0;a++;b++;}return 0;}
int strcmp(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
