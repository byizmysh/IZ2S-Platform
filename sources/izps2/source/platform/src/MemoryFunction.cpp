#include "MemoryFunction.h"
#include "code_runtime.h"
#include <stdexcept>
#include <utility>
#include <cstdlib>

static Ps2CodeArena *arena=nullptr;
void ps2_install_code_arena(Ps2CodeArena *a) {
    if(arena && arena->live_pages)throw std::runtime_error("Cannot replace a live PS2 JIT arena");
    arena=a;
}
Ps2CodeArena *ps2_code_arena() { return arena; }
CMemoryFunction::CMemoryFunction():m_code(nullptr),m_size(0) {}
CMemoryFunction::CMemoryFunction(const void *code,size_t size):m_code(nullptr),m_size(0) {
    if(!arena || ps2_arena_allocate(arena,code,size,&m_code))
        throw std::runtime_error("PS2 JIT allocation/publication failed; stop execution");
    m_size=size;
}
CMemoryFunction::CMemoryFunction(CMemoryFunction&& rhs):m_code(nullptr),m_size(0) { *this=std::move(rhs); }
CMemoryFunction::~CMemoryFunction() { Reset(); }
void CMemoryFunction::Reset() {
    /* Continuing after uncertain RX ownership would allow stale code to execute. */
    if(m_code && (!arena || ps2_arena_release(arena,m_code,m_size)))std::abort();
    m_code=nullptr; m_size=0;
}
bool CMemoryFunction::IsEmpty() const { return !m_code; }
CMemoryFunction& CMemoryFunction::operator=(CMemoryFunction&& rhs) {
    if(this!=&rhs) { Reset(); std::swap(m_code,rhs.m_code); std::swap(m_size,rhs.m_size); }
    return *this;
}
void CMemoryFunction::operator()(void *context) { reinterpret_cast<void(*)(void*)>(m_code)(context); }
void *CMemoryFunction::GetCode() const { return m_code; }
void *CMemoryFunction::GetWritableCode() const {
    auto p=ps2_arena_writable(arena,m_code,m_size);
    if(!p)throw std::runtime_error("Invalid PS2 JIT write range");
    return p;
}
size_t CMemoryFunction::GetSize() const { return m_size; }
void CMemoryFunction::BeginModify() { (void)GetWritableCode(); }
void CMemoryFunction::EndModify() { ClearCache(); }
void CMemoryFunction::ClearCache() {
    if(ps2_arena_publish(arena,m_code,m_size))
        throw std::runtime_error("PS2 JIT republication failed; stop execution");
}
CMemoryFunction CMemoryFunction::CreateInstance() { return CMemoryFunction(GetCode(),GetSize()); }
