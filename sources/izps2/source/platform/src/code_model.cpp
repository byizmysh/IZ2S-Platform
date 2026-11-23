#include "code_model.h"
#include "code_runtime.h"
#include <sys/mman.h>
#include <stdexcept>
#include <cstring>
static Ps2CodeArena model;
static bool opened=false;
static void *reserve(size_t bytes, int protection) {
    auto p=mmap(nullptr,bytes,protection,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(p==MAP_FAILED)throw std::runtime_error("Cannot reserve PS2 model arena");
    return p;
}
static int map_page(void *,uintptr_t source,uintptr_t rx) {
    if(mprotect(reinterpret_cast<void*>(rx),PS2_PAGE,PROT_READ|PROT_WRITE))return 1;
    memcpy(reinterpret_cast<void*>(rx),reinterpret_cast<void*>(source),PS2_PAGE);
    if(mprotect(reinterpret_cast<void*>(source),PS2_PAGE,PROT_NONE))return 2;
    return mprotect(reinterpret_cast<void*>(rx),PS2_PAGE,PROT_READ|PROT_EXEC)?3:0;
}
static int unmap_page(void *,uintptr_t source,uintptr_t rx) {
    if(mprotect(reinterpret_cast<void*>(rx),PS2_PAGE,PROT_READ))return 1;
    if(mprotect(reinterpret_cast<void*>(source),PS2_PAGE,PROT_READ|PROT_WRITE))return 2;
    memcpy(reinterpret_cast<void*>(source),reinterpret_cast<void*>(rx),PS2_PAGE);
    return mprotect(reinterpret_cast<void*>(rx),PS2_PAGE,PROT_NONE)?3:0;
}
static void cache(uintptr_t address,size_t bytes) {
    __builtin___clear_cache(reinterpret_cast<char*>(address),reinterpret_cast<char*>(address+bytes));
}
void ps2_model_open(size_t bytes) {
    if(opened)throw std::runtime_error("PS2 model arena is already installed");
    auto source=reserve(bytes,PROT_READ|PROT_WRITE), shadow=reserve(bytes,PROT_READ|PROT_WRITE), rx=reserve(bytes,PROT_NONE);
    if(ps2_arena_init(&model,source,shadow,rx,bytes,nullptr,map_page,unmap_page,cache,cache))
        throw std::runtime_error("Invalid PS2 model configuration");
    ps2_install_code_arena(&model);
    opened=true;
}
void ps2_model_close() {
    if(model.live_pages || model.poisoned)throw std::runtime_error("PS2 JIT resources remain live");
    ps2_install_code_arena(nullptr);
    munmap(model.source,model.bytes); munmap(model.shadow,model.bytes); munmap(model.rx,model.bytes);
    opened=false;
}
