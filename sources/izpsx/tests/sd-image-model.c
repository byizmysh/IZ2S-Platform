#include "sd-runtime/image.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t*read_file(const char*path,size_t*size){FILE*f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0);rewind(f);uint8_t*p=malloc(n);assert(p&&fread(p,1,n,f)==(size_t)n&&!fclose(f));*size=n;return p;}
static uint32_t crc(const void*data,size_t size){const uint8_t*p=data;uint32_t c=0xffffffff;while(size--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0xedb88320u&-(c&1));}return ~c;}
int main(int argc,char**argv){
 assert(argc==4);size_t bytes;uint8_t*asset=read_file(argv[1],&bytes);assert(bytes>=256);IzRelocHeader header;memcpy(&header,asset,sizeof header);
 for(unsigned i=0;i<3;i++){size_t n=i<2?header.file_bytes[i]:(size_t)header.record_count*32;assert(header.offsets[i]<=bytes&&n<=bytes-header.offsets[i]&&crc(asset+header.offsets[i],n)==header.crc32[i]);}
 void*segments[2];for(unsigned i=0;i<2;i++){segments[i]=calloc(1,header.memory_bytes[i]);assert(segments[i]);memcpy(segments[i],asset+header.offsets[i],header.file_bytes[i]);}
 uint64_t bases[]={0x6700100000ull,0x6808200000ull};IzRelocState s;assert(!iz_reloc_begin(&s,&header,bytes,segments,bases));
 for(unsigned at=0;at<header.record_count;){unsigned n=header.record_count-at;if(n>IZ_RELOC_CHUNK_RECORDS)n=IZ_RELOC_CHUNK_RECORDS;assert(!iz_reloc_batch(&s,(void*)(asset+header.offsets[2]+at*32),n));at+=n;}
 assert(!iz_reloc_finish(&s));for(unsigned i=0;i<2;i++){size_t n;uint8_t*expected=read_file(argv[2+i],&n);assert(n==header.file_bytes[i]&&!memcmp(segments[i],expected,n));free(expected);free(segments[i]);}
 free(asset);printf("{\"passed\":true,\"records\":%u,\"actualLauncherRelocator\":true,\"independentLinkExact\":true,\"consoleExecuted\":false}\n",header.record_count);return 0;
}
