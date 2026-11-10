#ifndef IZPSX_SD_SERVICES_H
#define IZPSX_SD_SERVICES_H
#include <stdint.h>
#include <stddef.h>
#include "native-app.h"
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
/* Module-owned copy of public services. Never cast app->owner or assume any
 * supervisor context layout. Every SDK entry is signature-checked at bind. */
enum {F_SEND,F_CLOSE,F_ALLOCATE,F_FREE,F_SLEEP,F_TICK,F_COUNT};
typedef struct {
 u32 stop,poisoned,ram_only;
 u64 functions[F_COUNT],pid,ro_pid;
 u32 ro_handle,filesystem;
 u64 tick_hz;
} Ps1SdServices;
typedef u64 (*Ps1SdkFunction)(u64,u64,u64,u64,u64,u64,u64,u64);
#define CALL(c,n,a,b,d,e,f,g,h,i) ((Ps1SdkFunction)(uintptr_t)(c)->functions[n])((u64)(uintptr_t)(a),(u64)(uintptr_t)(b),(u64)(uintptr_t)(d),(u64)(uintptr_t)(e),(u64)(uintptr_t)(f),(u64)(uintptr_t)(g),(u64)(uintptr_t)(h),(u64)(uintptr_t)(i))
void *memset(void*,int,size_t);
void *memcpy(void*,const void*,size_t);
int memcmp(const void*,const void*,size_t);
/* ABI revisions add suffixes. We consume only the ABI 5 prefix and required
 * callbacks. Declaring ABI 5 means older providers refuse before execution. */
static inline int ps1_sd_public_app(const IzNativeApp*a){
 return a&&a->magic==0x414e5a49&&a->version>=5&&a->bytes>=sizeof(IzNativeApp5)&&a->data&&a->data_bytes>=256u*1024u&&a->controls&&a->controls_bytes>=1024;
}
static inline const IzNativeEnvironment*ps1_sd_environment(const IzNativeApp*a){
 if(!ps1_sd_public_app(a))return 0;
 const IzNativeEnvironment*e=((const IzNativeApp5*)a)->environment;
 if(!e||e->version!=1||e->bytes<sizeof *e||!e->sdk||!e->sdk_bytes||e->sdk>=(1ull<<39)||e->sdk_bytes>(1ull<<39)-e->sdk||!e->application_pid||!e->ro_pid||!e->ro_handle||!e->tick_hz||e->tick_hz!=a->tick_hz)return 0;
 return e;
}
static inline void ps1_sd_copy_services(Ps1SdServices*s,const IzNativeApp*a,const IzNativeEnvironment*e){
 s->pid=e->application_pid;s->ro_pid=e->ro_pid;s->ro_handle=e->ro_handle;s->filesystem=e->filesystem;s->tick_hz=e->tick_hz;s->ram_only=!!(a->flags&IZ_NATIVE_SOURCE_RAM);
}
_Static_assert(offsetof(IzNativeApp5,environment)==192,"Required ABI 5 environment prefix");
_Static_assert(offsetof(IzNativeApp4,image_begin)==168,"Required image loading prefix");
#endif
