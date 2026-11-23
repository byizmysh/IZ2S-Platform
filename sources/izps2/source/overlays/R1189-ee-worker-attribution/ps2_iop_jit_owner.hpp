#pragma once

#include <cstddef>
#include <cstdint>

namespace IopThread { class Thread; }
struct Ps2Sb4Worker;
struct Sb4JitCacheApi;

struct Ps2IopJitView {
    std::uint8_t* rx = nullptr;
    std::uint8_t* rw = nullptr;
    std::size_t bytes = 0;

    bool contains(const void* address, std::size_t& offset) const {
        offset = reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(rx);
        return offset < bytes;
    }
};

bool iz_iop_jit_configure(Ps2Sb4Worker&, const Sb4JitCacheApi*, IopThread::Thread&, std::size_t);
void iz_iop_jit_unconfigure();
bool iz_iop_jit_selects_thread(const IopThread::Thread&);
bool iz_iop_jit_enabled();
bool iz_iop_jit_attach(IopThread::Thread&);
void iz_iop_jit_arm_scoped_create();
void iz_iop_jit_disarm_scoped_create();
void iz_iop_jit_detach(IopThread::Thread&);
bool iz_iop_jit_open_on_worker();
void iz_iop_jit_note_worker_drain();
void iz_iop_jit_leave_worker();
void iz_iop_jit_reap_after_join();
void iz_iop_jit_prepare_worker_reap(void* thread);
void iz_iop_jit_park();
bool iz_iop_jit_notify(const IopThread::Thread&);
bool iz_iop_is_jit_owner();
bool iz_iop_jit_can_write();
Ps2IopJitView iz_iop_jit_view();
void iz_iop_jit_publish(std::uint32_t offset, std::uint32_t bytes);
