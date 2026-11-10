#include "image-relocation.h"
static uint32_t read32(const uint8_t*p){uint32_t value=0;for(unsigned i=0;i<4;i++)value|=(uint32_t)p[i]<<(8*i);return value;}
static uint64_t read64(const uint8_t*p){uint64_t value=0;for(unsigned i=0;i<8;i++)value|=(uint64_t)p[i]<<(8*i);return value;}
static void write64(uint8_t*p,uint64_t value){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(value>>(8*i));}
static int range(uint64_t base,uint64_t bytes){return base&&!(base&4095)&&bytes&&base<(1ull<<39)&&bytes<=(1ull<<39)-base;}
static int overlap(uint64_t a,uint64_t an,uint64_t b,uint64_t bn){return a<b+bn&&b<a+an;}
uint32_t ps1_relocate_image(uint8_t*code,size_t code_bytes,uint8_t*data,size_t data_bytes,const uint8_t*table,size_t table_bytes,uint64_t new_code,uint64_t new_data,uint64_t expected_old_code,uint64_t expected_old_data,uint64_t expected_data_region){
 static const uint8_t magic[8]={'P','S','1','R','L','C','0','1'};
 if(!code||!data||!table||table_bytes<64)return 1;
 for(unsigned i=0;i<8;i++)if(table[i]!=magic[i])return 1;
 if(read32(table+8)!=1)return 1;
 uint32_t count=read32(table+12);uint64_t old_code=read64(table+16),old_data=read64(table+24),code_size=read64(table+32),data_size=read64(table+40),region=read64(table+48),flags=read64(table+56);
 if(count>(table_bytes-64)/4||table_bytes!=64+(uint64_t)count*4||code_size!=code_bytes||data_size!=data_bytes||region<data_size||region!=expected_data_region||flags>1||old_code!=expected_old_code||old_data!=expected_old_data)return 2;
 if(!range(old_code,code_size)||!range(old_data,region)||!range(new_code,code_size)||!range(new_data,region)||(flags&&new_code!=old_code))return 3;
 if(overlap(old_code,code_size,old_data,region)||overlap(new_code,code_size,new_data,region))return 4;
 uint64_t ca=(uintptr_t)code,da=(uintptr_t)data,ta=(uintptr_t)table;
 if(ca+code_bytes<ca||da+data_bytes<da||ta+table_bytes<ta||overlap(ca,code_bytes,da,data_bytes)||overlap(ca,code_bytes,ta,table_bytes)||overlap(da,data_bytes,ta,table_bytes))return 4;
 uint64_t previous=0;
 for(uint32_t i=0;i<count;i++){
  uint32_t record=read32(table+64+(size_t)i*4),source=(record>>1)&1,target=record&1,offset=record>>2;size_t bytes=source?data_bytes:code_bytes;uint8_t*image=source?data:code;uint64_t key=((uint64_t)source<<32)|offset;
  if((offset&7)||bytes<8||offset>bytes-8||(i&&key<=previous)||(flags&&!target))return 5;
  previous=key;uint64_t value=read64(image+offset),base=target?old_data:old_code,size=target?region:code_size;if(value<base||value-base>size)return 6;
 }
 for(uint32_t i=0;i<count;i++){
  uint32_t record=read32(table+64+(size_t)i*4),source=(record>>1)&1,target=record&1,offset=record>>2;uint8_t*image=source?data:code;
  write64(image+offset,read64(image+offset)-(target?old_data:old_code)+(target?new_data:new_code));
 }
 return 0;
}
