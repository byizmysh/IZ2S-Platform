#ifndef PS2_SCRATCH_RAMFS_H
#define PS2_SCRATCH_RAMFS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PS2_SCRATCH_PATH 769u
#define PS2_SCRATCH_NAME 256u
#define PS2_SCRATCH_NODES 256u
#define PS2_SCRATCH_DENTS 256u
#define PS2_SCRATCH_FDS 128u
#define PS2_SCRATCH_PAGE 4096u
#define PS2_SCRATCH_MAX_BYTES (128u*1024u*1024u)
typedef struct {uint64_t ino,bytes,version;uint32_t kind,links,mode;} Ps2ScratchStat;
typedef struct {uint32_t used,kind,links,opens,mode;uint64_t ino,bytes,version,first,pages;} Ps2ScratchNode;
typedef struct {uint32_t used,parent,node;char name[PS2_SCRATCH_NAME];} Ps2ScratchDent;
typedef struct {uint32_t refs,node,flags;uint64_t offset;} Ps2ScratchDescription;
typedef struct {uint32_t used,description,cloexec;} Ps2ScratchFd;
typedef struct {
 unsigned char*data;unsigned char*page_map;size_t capacity,page_count;
 uint64_t next_ino,epoch,sync_calls;uint32_t mounted;char root[PS2_SCRATCH_PATH];
 Ps2ScratchNode nodes[PS2_SCRATCH_NODES];Ps2ScratchDent dents[PS2_SCRATCH_DENTS];
 Ps2ScratchDescription descriptions[PS2_SCRATCH_FDS];Ps2ScratchFd fds[PS2_SCRATCH_FDS];
} Ps2ScratchRamfs;
/* Caller serializes every operation, including mount. All storage and metadata
 * stay alive across VM reset until every consumer ends. No core allocation,
 * syscall, clock, thread, logging, physical storage or SDK operation occurs.
 * Buffer arguments must not alias the FS metadata/page map/data arena.
 * Mount is once-only. Failures use negative Linux errno values. */
int ps2_scratch_mount(Ps2ScratchRamfs*,const char*root,void*data,size_t bytes,void*page_map,size_t map_bytes);
int ps2_scratch_contains(const Ps2ScratchRamfs*,const char*path);
int ps2_scratch_open(Ps2ScratchRamfs*,const char*,unsigned flags,unsigned mode);
int ps2_scratch_close(Ps2ScratchRamfs*,unsigned fd);
int ps2_scratch_dup(Ps2ScratchRamfs*,unsigned fd,int target,unsigned minimum,unsigned cloexec);
int ps2_scratch_stat(Ps2ScratchRamfs*,const char*,Ps2ScratchStat*);
int ps2_scratch_fstat(Ps2ScratchRamfs*,unsigned fd,Ps2ScratchStat*);
int ps2_scratch_mkdir(Ps2ScratchRamfs*,const char*,unsigned mode);
int ps2_scratch_unlink(Ps2ScratchRamfs*,const char*,unsigned directory);
int ps2_scratch_link(Ps2ScratchRamfs*,const char*,const char*);
int ps2_scratch_rename(Ps2ScratchRamfs*,const char*,const char*,unsigned no_replace);
int ps2_scratch_read(Ps2ScratchRamfs*,unsigned fd,void*,size_t,uint64_t offset,unsigned advance,size_t*actual);
int ps2_scratch_write(Ps2ScratchRamfs*,unsigned fd,const void*,size_t,uint64_t offset,unsigned advance,size_t*actual);
int ps2_scratch_truncate(Ps2ScratchRamfs*,unsigned fd,uint64_t bytes);
int64_t ps2_scratch_seek(Ps2ScratchRamfs*,unsigned fd,int64_t offset,int whence);
int ps2_scratch_sync(Ps2ScratchRamfs*,unsigned fd);
int ps2_scratch_child(Ps2ScratchRamfs*,unsigned fd,uint64_t position,char name[PS2_SCRATCH_NAME],Ps2ScratchStat*,uint64_t*next);
int ps2_scratch_fd_path(Ps2ScratchRamfs*,unsigned fd,char path[PS2_SCRATCH_PATH]);
#ifdef __cplusplus
}
#endif
#endif
