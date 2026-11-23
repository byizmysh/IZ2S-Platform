// SPDX-FileCopyrightText: 2026 yaps2 Dev Team
// SPDX-License-Identifier: GPL-3.0+
// Private EE-only incoming-link publication candidate. Include after the exact
// shared Arm64BaseBlocks header. Never changes that class or its inline bodies.
#pragma once

#ifndef PS2_EE_INCOMING_PAGE_BATCH
#define PS2_EE_INCOMING_PAGE_BATCH 0
#endif

#if PS2_EE_INCOMING_PAGE_BATCH && defined(PS2_HOST_CODE_BRIDGE)
#include "code_host.h"
#include "ee_publication_transaction.h"

namespace
{
class Ps2EeIncomingBlocks;
static BASEBLOCKEX* ps2EeNewIncomingPageBatch(Ps2EeIncomingBlocks& self,
    u32 startpc, uptr fnptr);

// No fields, virtual functions or shared definition changes. The friend uses
// inherited protected members through this actual derived object only.
class Ps2EeIncomingBlocks final : public Arm64BaseBlocks
{
    friend BASEBLOCKEX* ps2EeNewIncomingPageBatch(Ps2EeIncomingBlocks& self,
        u32 startpc, uptr fnptr);

public:
    void EmitLink(u32 pc, bool call)
    {
        vixl::aarch64::SingleEmissionCheckScope guard(armAsm);
        // Use the cursor after entering the scope, which can emit a pool.
        const auto site = reinterpret_cast<uptr>(armGetCurrentCodePointer());
        const BASEBLOCKEX* target = Get(pc);
        const uptr address = (target && target->startpc == pc) ? target->fnptr : StaleDispatchTarget();
        // This word belongs to the current emitter. The normal EndBlock
        // publishes it with the rest of the block. Incoming sites in
        // earlier blocks still use the original patch path.
        const u32 word = EncodeB(site, address, call);
        const s64 displacement = static_cast<s64>(static_cast<s32>(word << 6)) >> 6;
        if (call)
            armAsm->bl(displacement);
        else
            armAsm->b(displacement);

        // The iterator after the equal range survives erasure. Insert
        // before it to preserve the order of callers with the same target.
        const auto range = links.equal_range(pc);
        for (auto it = range.first; it != range.second;)
        {
            if (IsOwnerLive(it->second))
                ++it;
            else
                it = links.erase(it);
        }
        links.emplace_hint(range.second, pc,
            LinkSite{site | (call ? kLinkSiteCallBit : 0), emitting_startpc, emitting_fnptr});
    }
};
static_assert(sizeof(Ps2EeIncomingBlocks) == sizeof(Arm64BaseBlocks));
static_assert(alignof(Ps2EeIncomingBlocks) == alignof(Arm64BaseBlocks));

static BASEBLOCKEX* ps2EeNewIncomingPageBatch(Ps2EeIncomingBlocks& self,
    u32 startpc, uptr fnptr)
{
    // Outside this exact owner transaction, use the original shared New,
    // including its immediate patch/flush and overflow behavior.
    if (!ps2_code_host_bound() || !ps2_code_host_ee_transaction_active() ||
        HostSys::GetRuntimePageSize() != 4096 || !armHasBlock())
        return self.Arm64BaseBlocks::New(startpc, fnptr);

    // Exact original New metadata/liveness semantics. All protected access is
    // through self; no layout casts or private-access header substitutions.
    self.emitting_startpc = startpc;
    self.emitting_fnptr = fnptr;
    const int idx = self.Index(startpc);
    BASEBLOCKEX* block = (idx >= 0 && self.blocks[idx].startpc == startpc)
        ? &self.blocks[idx] : nullptr;
    if (block)
        block->fnptr = fnptr;
    else
        block = self.blocks.insert(startpc, fnptr);

    constexpr u32 capacity = 64;
    constexpr uptr pageMask = ~uptr(4095);
    // armGetAsmPtr is the fixed primary emit-buffer start, not the cursor.
    const uptr emitterPage = reinterpret_cast<uptr>(armGetAsmPtr()) & pageMask;
    uptr sites[capacity];
    u32 words[capacity];
    u32 count = 0;
    const auto patchChunk = [&]() {
        bool done[capacity]{};
        for (u32 i = 0; i < count; ++i)
        {
            if (done[i])
                continue;
            const uptr page = sites[i] & pageMask;
            if (page >= emitterPage)
            {
                // Preserve the emitter's exact open-window handling. This
                // helper never widens the active window or a pool scope.
                self.PatchWord(sites[i], words[i]);
                done[i] = true;
                continue;
            }
            uptr lo = sites[i], hi = sites[i] + sizeof(u32);
            for (u32 j = i + 1; j < count; ++j)
            {
                if ((sites[j] & pageMask) != page)
                    continue;
                lo = std::min(lo, sites[j]);
                hi = std::max(hi, sites[j] + sizeof(u32));
            }
            // Actual owner/range admission and dirty-page note both precede
            // EVERY raw shadow store. A failed/unknown boundary retains the
            // existing provider/outer writer; no local unlock or retry exists.
            HostSys::BeginCodeWriteRange(reinterpret_cast<void*>(lo), hi - lo);
            ps2_ee_publication_note_patch(reinterpret_cast<void*>(lo), hi - lo);
            for (u32 j = i; j < count; ++j)
            {
                if ((sites[j] & pageMask) != page)
                    continue;
                *reinterpret_cast<volatile u32*>(armGetWritableCodePtr(
                    reinterpret_cast<u8*>(sites[j]))) = words[j];
                done[j] = true;
            }
            HostSys::EndCodeWriteRange(reinterpret_cast<void*>(lo), hi - lo);
        }
        count = 0;
    };

    const auto range = self.links.equal_range(startpc);
    for (auto it = range.first; it != range.second;)
    {
        if (!self.IsOwnerLive(it->second))
        {
            it = self.links.erase(it);
            continue;
        }
        const uptr site = it->second.site & ~self.kLinkSiteCallBit;
        sites[count] = site;
        words[count++] = self.EncodeB(site, fnptr,
            (it->second.site & self.kLinkSiteCallBit) != 0);
        if (count == capacity)
            patchChunk();
        ++it;
    }
    patchChunk();
    // Each actual changed page was admitted and queued before its store;
    // paired End queues it again. The existing explicit EE transaction commit
    // publishes all touched pages before EE RX resumes. No redundant New-only
    // coalesced/overflow defer calls are needed inside this fast path.
    return block;
}
} // namespace
#endif
