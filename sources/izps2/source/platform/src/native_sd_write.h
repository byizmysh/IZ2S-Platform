#ifndef PS2_NATIVE_SD_WRITE_H
#define PS2_NATIVE_SD_WRITE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef uint32_t (*Ps2SdSend)(void*,uint64_t,uint32_t);
typedef uint32_t (*Ps2SdClose)(uint32_t);
typedef struct {
 uint32_t magic,version,size,flags;
 uint64_t sdk;
 uint32_t filesystem,reserved;
 uint64_t buffer,capacity,token[2];
} Ps2SdWriteConfig;
typedef struct {
 Ps2SdSend send;Ps2SdClose close;
 uint8_t *buffer;uint32_t filesystem,ready,poisoned,error,transport,leaked;
 uint32_t files[32];uint64_t opened,closed,read_bytes,written_bytes;
} Ps2SdWrite;
enum {PS2_SD_WRITE_INVALID=0xf701,PS2_SD_WRITE_PACKET=0xf702,PS2_SD_WRITE_RETAINED=0xf703};
int ps2_sd_write_initialize(Ps2SdWrite*,const Ps2SdWriteConfig*,Ps2SdSend,Ps2SdClose);
uint32_t ps2_sd_write_open(Ps2SdWrite*,uint64_t*,const char*,int);
void ps2_sd_write_close(Ps2SdWrite*,uint64_t);
uint32_t ps2_sd_write_size(Ps2SdWrite*,int64_t*,uint64_t);
uint32_t ps2_sd_write_read(Ps2SdWrite*,size_t*,uint64_t,int64_t,void*,size_t);
uint32_t ps2_sd_write_write(Ps2SdWrite*,uint64_t,int64_t,const void*,size_t,const uint32_t*);
uint32_t ps2_sd_write_flush(Ps2SdWrite*,uint64_t);
uint32_t ps2_sd_write_resize(Ps2SdWrite*,uint64_t,int64_t);
uint32_t ps2_sd_write_create(Ps2SdWrite*,const char*,int64_t,int);
uint32_t ps2_sd_write_type(Ps2SdWrite*,uint32_t*,const char*);
uint32_t ps2_sd_write_rename(Ps2SdWrite*,const char*,const char*);
int ps2_sd_write_healthy(const Ps2SdWrite*);
int ps2_sd_write_shutdown(Ps2SdWrite*);
#ifdef __cplusplus
}
#endif
#endif
