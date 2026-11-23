/* Candidate-only terminal cold-island patch helper. No provider/native ABI. */
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include "arm64/AsmHelpers.h"
#include "common/HostSys.h"
#include "code_host.h"

__attribute__((visibility("hidden"))) bool ps2ArmCodeWriteWindowActive();
class Ps2ColdIslandWordPageBatch final {
    struct Word { uintptr_t site; uint32_t value; };
    static constexpr unsigned Capacity = 64;
    static constexpr uintptr_t Page = 4096;
    Word words_[Capacity]{};
    uintptr_t lower_, upper_;
    unsigned count_ = 0;
    bool batch_, finished_ = false;

    [[noreturn]] static void reject() { std::abort(); }
    void flush() {
        if (!count_) return;
        if (!batch_ || ps2ArmCodeWriteWindowActive()) reject();
        bool done[Capacity]{};
        for (unsigned i = 0; i < count_; ++i) {
            if (done[i]) continue;
            const uintptr_t page = words_[i].site & ~(Page - 1);
            uintptr_t lo = words_[i].site, hi = lo + 4;
            for (unsigned j = i + 1; j < count_; ++j) {
                if ((words_[j].site & ~(Page - 1)) != page) continue;
                if (words_[j].site < lo) lo = words_[j].site;
                if (words_[j].site + 4 > hi) hi = words_[j].site + 4;
            }
            // Existing owner/range admission precedes the first store.
            // Each group's scope is immediately paired, preserving LIFO.
            HostSys::BeginCodeWriteRange(reinterpret_cast<void*>(lo), hi - lo);
            for (unsigned j = i; j < count_; ++j) {
                if ((words_[j].site & ~(Page - 1)) != page) continue;
                *reinterpret_cast<volatile uint32_t*>(armGetWritableCodePtr(
                    reinterpret_cast<u8*>(words_[j].site))) = words_[j].value;
                done[j] = true;
            }
            // Existing failures remain terminal/retained: no forced unlock,
            // replay, or invented rollback after a partial commit.
            HostSys::EndCodeWriteRange(reinterpret_cast<void*>(lo), hi - lo);
            HostSys::FlushInstructionCache(reinterpret_cast<void*>(lo),
                                          static_cast<uint32_t>(hi - lo));
        }
        count_ = 0;
    }
public:
    Ps2ColdIslandWordPageBatch(const void* lower, const void* upper)
        : lower_(reinterpret_cast<uintptr_t>(lower)),
          upper_(reinterpret_cast<uintptr_t>(upper)),
          batch_(ps2_code_host_bound() && !ps2ArmCodeWriteWindowActive()) {
        if (!lower_ || lower_ >= upper_ || ((lower_ | upper_) & (Page - 1))) reject();
        if (batch_ && HostSys::GetRuntimePageSize() != Page) reject();
    }
    Ps2ColdIslandWordPageBatch(const Ps2ColdIslandWordPageBatch&) = delete;
    Ps2ColdIslandWordPageBatch& operator=(const Ps2ColdIslandWordPageBatch&) = delete;
    ~Ps2ColdIslandWordPageBatch() { if (!finished_) reject(); }
    void AddWord(void* site, uint32_t value) {
        const uintptr_t p = reinterpret_cast<uintptr_t>(site);
        if (finished_ || (p & 3) || p < lower_ || p >= upper_ || upper_ - p < 4) reject();
        if (!batch_) {
            // Exact immediate behavior within an existing emit window or
            // without a bound bridge. No deferred shadow store in this path.
            armPatchCodeWord(site, value);
            HostSys::FlushInstructionCache(site, 4);
            return;
        }
        if (ps2ArmCodeWriteWindowActive()) reject();
        if (count_ == Capacity) flush();
        words_[count_++] = {p, value};
    }
    void AddBranch(u8* site, const u8* target) {
        const uintptr_t p = reinterpret_cast<uintptr_t>(target);
        if ((p & 3) || p < lower_ || p >= upper_) reject();
        const intptr_t offset = static_cast<intptr_t>(p) - reinterpret_cast<intptr_t>(site);
        if ((offset & 3) || offset / 4 < -(intptr_t(1) << 25) ||
            offset / 4 >= (intptr_t(1) << 25)) reject();
        AddWord(site, 0x14000000u | (static_cast<uint32_t>(offset / 4) & 0x03ffffffu));
    }
    void Finish() {
        if (finished_) reject();
        flush();
        finished_ = true;
    }
};
