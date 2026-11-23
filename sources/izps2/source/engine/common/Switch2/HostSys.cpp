#include "common/HostSys.h"
#include "common/Error.h"
#include "common/Assertions.h"
#include "common/Console.h"
#include "common/Switch2/CodeCache.h"
#include "native-system.h"
#include <sb4/jit-cache.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>
#include <thread>

namespace
{
// EE, IOP, VU0 and VU1 publish and execute on the VM CPU thread in the
// Switch 2 configuration. VM shutdown stops the CPU providers before close.
const Sb4JitCacheApi* s_jit_api;
const Sb4JitCacheApi* s_cache_owner;
Sb4JitCache s_cache;
size_t s_cache_size;
std::thread::id s_cache_thread;
#ifdef IZPS2_WRAPPER_AUDIT
HostSys::JitCacheTiming s_publication;
#endif

size_t code_offset(const void* address)
{
    return reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(s_cache.rx);
}
}

void HostSys::SetJitCacheApi(const Sb4JitCacheApi* api)
{
    s_jit_api = api;
}

#ifdef IZPS2_WRAPPER_AUDIT
HostSys::JitCacheTiming HostSys::GetJitCacheTiming()
{
    return s_publication;
}
#endif

void* HostSys::AllocateJitCache(size_t size)
{
    if (!s_jit_api || !s_jit_api->open || !s_jit_api->publish || !s_jit_api->close)
        return nullptr;

    if (s_cache.owner)
    {
        ReleaseJitCache();
        if (s_cache.owner)
            return nullptr;
    }

    s_cache_owner = s_jit_api;
    const uint32_t result = s_cache_owner->open(&s_cache, size);
    if (result)
    {
        Console.Warning("IZPS2 JIT cache initialization failed: 0x%08x", result);
        std::fprintf(stderr, "IZPS2_JIT_OPEN bytes=%zu rc=0x%08x\n", size, result);
        // A partial open may still own mappings. Retry cleanup without losing
        // the provider or overwriting the original Result in the open log.
        ReleaseJitCache();
        return nullptr;
    }

    s_cache_size = size;
    s_cache_thread = std::this_thread::get_id();
#ifdef IZPS2_WRAPPER_AUDIT
    s_publication = {};
#endif
    Console.WriteLn("IZPS2 JIT cache ready: %zu bytes", size);
    std::fprintf(stderr, "IZPS2_JIT_OPEN bytes=%zu rc=0x00000000\n", size);
    return s_cache.rx;
}

void HostSys::ReleaseJitCache()
{
    // A failed close retains the SDK owner for a subsequent close attempt.
#ifdef IZPS2_WRAPPER_AUDIT
    if (s_cache_size)
    {
        std::fprintf(stderr,
            "IZPS2_JIT_PUBLICATION calls=%llu bytes=%llu ticks=%llu max_ticks=%llu hz=%llu\n",
            static_cast<unsigned long long>(s_publication.calls),
            static_cast<unsigned long long>(s_publication.bytes),
            static_cast<unsigned long long>(s_publication.ticks),
            static_cast<unsigned long long>(s_publication.max_ticks),
            static_cast<unsigned long long>(iz_tick_frequency()));
    }
#endif
    s_cache_size = 0;
    if (s_cache.owner)
    {
        const uint32_t result = s_cache_owner->close(&s_cache);
        std::fprintf(stderr, "IZPS2_JIT_CLOSE rc=0x%08x\n", result);
        if (result)
            Console.Error("IZPS2 JIT cache close failed: 0x%08x", result);
    }
}

void* HostSys::GetWritableCodeAddress(void* address)
{
    const size_t offset = code_offset(address);
    if (offset < s_cache_size && s_cache_thread != std::this_thread::get_id())
    {
        std::fprintf(stderr, "IZPS2_JIT_WRITE fatal=wrong-thread offset=%zu\n", offset);
        std::abort();
    }
    return offset < s_cache_size ? s_cache.rw + offset : address;
}

void HostSys::MemProtect(void* p,size_t n,const PageProtectionMode& mode){
    if(iz_memory_protect(p,n,mode.CanRead(),mode.CanWrite(),mode.CanExecute()))
        pxFailRel("libounce memory protection failed");
}
std::string HostSys::GetFileMappingName(const char* prefix){return prefix;}
void* HostSys::CreateSharedMemory(const char*,size_t){errno=ENOTSUP;return nullptr;}
void HostSys::DestroySharedMemory(void*){pxFailRel("No shared mappings in interpreter build");}
size_t HostSys::GetRuntimePageSize(){return 4096;}
size_t HostSys::GetRuntimeCacheLineSize(){return 64;}
void HostSys::FlushInstructionCache(void* address, u32 size)
{
    if (!size)
        return;

    const size_t offset = code_offset(address);
    if (offset >= s_cache_size)
    {
        iz_flush_instructions(address, size);
        return;
    }

#ifdef IZPS2_WRAPPER_AUDIT
    const uint64_t start = iz_system_ticks();
#endif
    const uint32_t result = s_cache_owner->publish(&s_cache, offset, size);
#ifdef IZPS2_WRAPPER_AUDIT
    const uint64_t elapsed = iz_system_ticks() - start;
    ++s_publication.calls;
    s_publication.bytes += size;
    s_publication.ticks += elapsed;
    if (elapsed > s_publication.max_ticks)
        s_publication.max_ticks = elapsed;
#endif
    if (result)
    {
        Console.Error("IZPS2 JIT publication failed at 0x%zx: 0x%08x", offset, result);
        std::fprintf(stderr, "IZPS2_JIT_PUBLISH offset=%zu bytes=%u rc=0x%08x\n", offset, size, result);
        // An active recompiler cannot resume into partially published code.
        std::abort();
    }
}
SharedMemoryMappingArea::SharedMemoryMappingArea(u8* p,size_t n,size_t pages):m_base_ptr(p),m_size(n),m_num_pages(pages){}
SharedMemoryMappingArea::~SharedMemoryMappingArea(){pxAssert(m_num_mappings==0);}
std::unique_ptr<SharedMemoryMappingArea> SharedMemoryMappingArea::Create(size_t,bool,uptr){errno=ENOTSUP;return nullptr;}
u8* SharedMemoryMappingArea::Map(void*,size_t,void*,size_t,const PageProtectionMode&){errno=ENOTSUP;return nullptr;}
bool SharedMemoryMappingArea::Unmap(void*,size_t,bool){errno=ENOTSUP;return false;}
bool PageFaultHandler::Install(Error* e){Error::SetStringView(e,"Switch 2 interpreter mode has no fastmem fault handler");return false;}
bool PageFaultHandler::InstallSecondaryThread(){return false;}
