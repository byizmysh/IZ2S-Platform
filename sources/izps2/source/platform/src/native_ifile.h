#ifndef PS2_NATIVE_IFILE_H
#define PS2_NATIVE_IFILE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PS2_IFILE_MAGIC 0x32445350u
#define PS2_IFILE_FILES 16u
#define PS2_IFILE_SEGMENTS 64u
#define PS2_IFILE_CACHE 65536u
typedef struct {
    char logical[256];
    uint64_t bytes;
    uint32_t first_segment,segment_count,role,reserved;
} Ps2IFileDescription;
typedef struct {
    char sd_path[256];
    uint64_t source_offset,bytes,file_bytes;
    uint32_t handle,reserved;
} Ps2IFileSegment;
/* HBX3 args[3]. Borrowed capabilities/table/cache remain resident until every
 * native file user has joined. Token/flags attest caller ownership; they do
 * not acquire ownership or authorize closing any supplied handle. */
typedef struct {
    uint32_t magic,version,size,flags;
    uint64_t buffer,capacity,files,segments;
    uint32_t file_count,segment_count;
    uint64_t token[2];
} Ps2IFileMountDescription;
typedef uint32_t (*Ps2IFileSend)(void*,uint64_t,uint32_t);
typedef struct {
    uint64_t physical_reads,physical_bytes,delivered_bytes,max_logical_end;
} Ps2IFileCounters;
typedef struct {
    Ps2IFileDescription files[PS2_IFILE_FILES];
    Ps2IFileSegment segments[PS2_IFILE_SEGMENTS];
    Ps2IFileCounters counters[PS2_IFILE_FILES];
    Ps2IFileSend send;
    void*buffer;size_t capacity;
    uint64_t cache_offset,cache_bytes,requests,size_requests;
    uint32_t file_count,segment_count,cache_file,ready,poisoned,in_flight;
    uint32_t error,transport,result,unexpected_count,unexpected_handles[30];
} Ps2IFileMount;
enum {PS2_IFILE_INVALID=0xf301,PS2_IFILE_PACKET=0xf302,PS2_IFILE_SIZE=0xf303,
      PS2_IFILE_SHORT=0xf304,PS2_IFILE_RETAINED=0xf305,PS2_IFILE_NOT_FOUND=0xf306};
enum {PS2_IFILE_ROLE_AUX=0,PS2_IFILE_ROLE_ISO=1,PS2_IFILE_ROLE_BIOS=2};
/* One external serial owner protects message/cache operations. Init copies
 * and validates descriptors and performs no IPC. Every operation preserves
 * borrowed handles. Unknown transport/packet/short-read poisons the owner. */
int ps2_ifile_initialize(Ps2IFileMount*,const Ps2IFileMountDescription*,Ps2IFileSend);
int ps2_ifile_sdk_initialize(Ps2IFileMount*,const Ps2IFileMountDescription*,uintptr_t,size_t);
int ps2_ifile_find(const Ps2IFileMount*,const char*);
uint32_t ps2_ifile_size(Ps2IFileMount*,unsigned,uint64_t*);
uint32_t ps2_ifile_read(Ps2IFileMount*,unsigned,uint64_t,void*,size_t,size_t*);
#ifdef __cplusplus
}
#endif
#endif
