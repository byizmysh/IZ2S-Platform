#include "sdk-storage.h"
#include "sdk-storage-profile.h"
extern u32 iz_self_process_id(u64*);
extern u32 iz_self_query(IzPs2MemoryRegion*,u64);
static int overlap(u64 a,u64 n,u64 b,u64 m){return a<b+m&&b<a+n;}
static int range(u64 p,u64 n){return p&&p<(1ull<<39)&&n&&n<=(1ull<<39)-p;}
static u32 writable(u64 at,u64 bytes){
 if(!range(at,bytes))return 0xef01;u64 end=at+bytes;unsigned count=0;
 while(at<end){IzPs2MemoryRegion r={0};u32 rc=iz_self_query(&r,at);if(rc||++count>16||!range(r.base,r.bytes)||r.base%4096||r.bytes%4096||r.base>at||r.base+r.bytes<=at||r.type!=5||r.permission!=3||r.attributes||r.ipc_refs||r.device_refs)return 0xef02;u64 next=r.base+r.bytes;at=next<end?next:end;}return 0;
}
u32 iz_ps2_sdk_storage_bind(IzPs2StorageIo*out,const IzNativeEnvironment*e,void*scratch,u64 bytes){
 if(!out||!e||e->version!=1||e->bytes!=sizeof *e||e->sdk_bytes!=IZ_PS2_SDK_BYTES||!range(e->sdk,e->sdk_bytes)||e->sdk%4096||!e->application_pid||e->application_pid>=(1ull<<32)||!e->filesystem||e->filesystem>=0x80000000u||bytes!=65536||(uintptr_t)scratch%4096||(uintptr_t)out%8)return 0xef01;
 u64 data=(uintptr_t)out,buffer=(uintptr_t)scratch;if(!range(data,sizeof *out)||!range(buffer,bytes)||overlap(data,sizeof *out,buffer,bytes)||overlap(data,sizeof *out,e->sdk,e->sdk_bytes)||overlap(buffer,bytes,e->sdk,e->sdk_bytes))return 0xef01;
 if(out->filesystem.c||out->filesystem.leaked||out->context.poisoned)return 0xef19;for(unsigned i=0;i<32;i++)if(out->filesystem.handles[i]||out->filesystem.ram_open[i])return 0xef19;
 u64 pid=0;u32 rc=iz_self_process_id(&pid);if(rc||pid!=e->application_pid)return 0xef03;
 rc=iz_ps2_sdk_check_cut(e->sdk,IZ_PS2_SEND_OFFSET,iz_ps2_send_cut,sizeof iz_ps2_send_cut);if(rc)return rc;rc=iz_ps2_sdk_check_cut(e->sdk,IZ_PS2_CLOSE_OFFSET,iz_ps2_close_cut,sizeof iz_ps2_close_cut);if(rc)return rc;
 rc=writable(data,sizeof *out);if(rc)return rc;rc=writable(buffer,bytes);if(rc)return rc;rc=iz_self_process_id(&pid);if(rc||pid!=e->application_pid)return 0xef03;
 const IzNativeEnvironment saved=*e;memset(out,0,sizeof *out);out->context.sdk=saved.sdk;out->context.pid=saved.application_pid;out->context.ro_pid=saved.ro_pid;out->context.filesystem=saved.filesystem;out->context.functions[F_SEND]=saved.sdk+IZ_PS2_SEND_OFFSET;out->context.functions[F_CLOSE]=saved.sdk+IZ_PS2_CLOSE_OFFSET;out->filesystem.c=&out->context;out->filesystem.buffer=scratch;return 0;
}
