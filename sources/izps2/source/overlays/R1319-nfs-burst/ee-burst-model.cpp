#include "ee-warm-boot-coverage.h"
#include "ee-late-warm.hpp"

#include <cassert>
#include <iostream>

int main()
{
    using namespace ps2::ee_warm;
    constexpr uint32_t disc = 0xf5c7b45f;
    constexpr uint32_t base = 0x4000;
    const std::array<Entry, 3> ordinary{{{base+32,128},{base,64},{base+192,64}}};
    const BootCoverage coverage(ordinary);
    assert(coverage.Spans().size() == 2);
    assert(coverage.Intersects({base-4,8}));
    assert(coverage.Intersects({base+156,8}));
    assert(!coverage.Intersects({base+160,32}));
    assert(!coverage.Intersects({base+256,4}));

    std::vector<uint8_t> ram(0x10000);
    const auto guest = std::span<uint8_t>(ram);
    auto capture = std::make_unique<LateWarmCapture>(40,20);
    assert(capture->Available());
    for (const auto& entry : coverage.Spans())
        capture->Cover(entry.pc, entry.bytes);
    capture->OnField(1,1000,0);
    capture->Observe(base-8,base-8,4,true,Hash(guest.subspan(base-8,4)));
    capture->OnField(2,1016,8); // Quiet interval, should not train.
    for (uint32_t i=0; i<2197; ++i)
    {
        const uint32_t pc = base + 4*i;
        guest[pc] = static_cast<uint8_t>(i);
        capture->Observe(pc,pc,4,true,Hash(guest.subspan(pc,4)));
    }
    capture->OnField(3,1243,217);
    capture->Discontinuity();
    capture->Observe(0x9000,0x9000,4,true,Hash(guest.subspan(0x9000,4)));
    capture->OnField(8,1500,300);
    std::vector<uint8_t> profile;
    assert(capture->Build(disc,disc,{},profile));
    assert((profile.size()-HeaderBytes)/RecordBytes == LateWarmCapture::EntriesPerBurst);
    guest[base+160] ^= 0x80; // The first recorded block changed since training.
    uint32_t submitted = 0, prior = 0;
    const auto observe = [&](std::span<const Entry> entries)
    {
        for (const auto& record : entries)
        {
            assert(record.pc > prior && !coverage.Intersects(record));
            assert(record.pc != base+160 && record.pc != 0x9000);
            prior = record.pc;
            ++submitted;
        }
        return true;
    };
    const auto replay = Warm(profile,disc,disc,guest,observe,LateWarmCapture::Limit);
    assert(replay.valid && replay.stale == 1 && submitted == LateWarmCapture::EntriesPerBurst - 1);
    assert(!Warm(profile,disc,disc^1,guest,observe,LateWarmCapture::Limit).valid);
    std::cout << "{\"eligibleWorstFieldLimit\":" << LateWarmCapture::EntriesPerBurst
              << ",\"replayed\":" << submitted << ",\"staleRejected\":" << replay.stale
              << ",\"bootOverlapExcluded\":true,\"quietAndDiscontinuousFieldsExcluded\":true}\n";
}
