/* Only link with the complete explicit-wrap GS core. These are ordinary
 * canonical VRAM buffers, never physical or copied aliases of other buffers. */
#include "gs_portable_vram.h"
#include <cstdlib>
#include <cstdio>
#include <mutex>
#ifdef PS2_GS_PORTABLE_GUARDS
#include <sys/mman.h>
#endif

extern "C" [[noreturn]] void ps2_hbxl_vm_fatal(const char *, unsigned);
extern "C" { Ps2GsPortableStatistics ps2_gs_portable_stats; }
static std::mutex mutex;
static void *buffers[16];
static constexpr size_t vram_bytes = 4u * 1024 * 1024;

extern "C" void *__wrap__Z23GSAllocateWrappedMemorymm(size_t size, size_t repeat)
{
    std::lock_guard<std::mutex> guard(mutex);
    auto &s = ps2_gs_portable_stats;
    if (size != vram_bytes || repeat != 4) {
        s.failures++;
        return nullptr;
    }
    unsigned index = 0;
    while (index < 16 && buffers[index]) index++;
    if (index == 16) {
        s.failures++;
        return nullptr;
    }
    void *pointer = nullptr;
#ifdef PS2_GS_PORTABLE_GUARDS
    void *mapping = mmap(nullptr, size + 8192, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping != MAP_FAILED) {
        pointer = static_cast<unsigned char*>(mapping) + 4096;
        if (mprotect(pointer, size, PROT_READ | PROT_WRITE)) {
            munmap(mapping, size + 8192);
            pointer = nullptr;
        }
    }
#else
    if (posix_memalign(&pointer, 64, size)) pointer = nullptr;
#endif
    if (!pointer) {
        s.failures++;
        return nullptr;
    }
    buffers[index] = pointer;
    s.allocations++; s.live_objects++; s.live_bytes += size; s.allocation_bytes += size;
    if (s.live_bytes > s.peak_bytes) s.peak_bytes = s.live_bytes;
    std::fprintf(stderr, "PS2_GS_PORTABLE_VRAM canonical=%p bytes=%zu aliases=0\n", pointer, size);
    return pointer;
}

extern "C" void __wrap__Z19GSFreeWrappedMemoryPvmm(void *pointer, size_t size, size_t repeat)
{
    bool failed = false;
    {
        std::lock_guard<std::mutex> guard(mutex);
        auto &s = ps2_gs_portable_stats;
        unsigned index = 0;
        while (index < 16 && buffers[index] != pointer) index++;
        if (!pointer || size != vram_bytes || repeat != 4 || index == 16) {
            failed = true;
        } else {
#ifdef PS2_GS_PORTABLE_GUARDS
            failed = munmap(static_cast<unsigned char*>(pointer) - 4096, size + 8192) != 0;
#else
            std::free(pointer);
#endif
            if (!failed) {
                buffers[index] = nullptr;
                s.frees++; s.live_objects--; s.live_bytes -= size; s.release_bytes += size;
            }
        }
        if (failed) s.failures++;
    }
    if (failed) ps2_hbxl_vm_fatal("portable GS VRAM ownership retained", __LINE__);
}
