#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
using u8 = std::uint8_t;
using u64 = std::uint64_t;
using s64 = std::int64_t;
using uptr = std::uintptr_t;
enum : u8 { kFormMovRun, kFormAdrp, kFormRel26, kFormRel19 };
enum : u8 { kTargetBlockObject, kTargetBlockEntry, kTargetStub, kTargetImage, kTargetData, kTargetImageWritable, kTargetCount };
struct PersistFixup { u8 form; u8 target; s64 offset; };
	struct Regions
	{
		uptr imgBegin = 0, imgEnd = 0;
		uptr dataBegin = 0, dataEnd = 0;
		uptr imageBase = 0; // imgBegin rounded down to 4KB
		uptr dataBase = 0;
#ifdef IZPS2_SWITCH2_VU_SPLIT_PERSIST
		uptr writableBegin = 0, writableEnd = 0;
#endif
		bool valid = false;
	};

static Regions layout;
static Regions GetRegions() { return layout; }
	static u8 RegionOf(const void* addr)
	{
		const Regions& r = GetRegions();
		if (!r.valid)
			return kTargetCount;
		const uptr a = reinterpret_cast<uptr>(addr);
		if (a >= r.imgBegin && a < r.imgEnd)
			return kTargetImage;
#ifdef IZPS2_SWITCH2_VU_SPLIT_PERSIST
		if (a >= r.writableBegin && a < r.writableEnd)
			return kTargetImageWritable;
#endif
		if (a >= r.dataBegin && a < r.dataEnd)
			return kTargetData;
		return kTargetCount;
	}

	static uptr RegionBase(u8 target)
	{
		const Regions& r = GetRegions();
#ifdef IZPS2_SWITCH2_VU_SPLIT_PERSIST
		if (target == kTargetImageWritable)
			return r.writableBegin;
#endif
		return (target == kTargetImage) ? r.imageBase : r.dataBase;
	}

	static bool IsRegionTarget(u8 target)
	{
		return target >= kTargetImage && target < kTargetCount;
	}

	static bool ResolveRegionTarget(const PersistFixup& fixup, u64& address)
	{
		const Regions& r = GetRegions();
		if (!r.valid || fixup.offset < 0)
			return false;
		uptr base = 0, begin = 0, end = 0;
		switch (fixup.target)
		{
			case kTargetImage:
				base = r.imageBase; begin = r.imgBegin; end = r.imgEnd;
				break;
			case kTargetData:
				base = r.dataBase; begin = r.dataBegin; end = r.dataEnd;
				break;
#ifdef IZPS2_SWITCH2_VU_SPLIT_PERSIST
			case kTargetImageWritable:
				base = r.writableBegin; begin = r.writableBegin; end = r.writableEnd;
				break;
#endif
			default:
				return false;
		}
		const u64 offset = static_cast<u64>(fixup.offset);
		if (!base || begin < base || end <= begin || offset < begin - base || offset >= end - base)
			return false;
		// A cached direct branch may target executable image bytes, never writable regions.
		if ((fixup.form == kFormRel19 || fixup.form == kFormRel26) && fixup.target != kTargetImage)
			return false;
		address = base + offset;
		return true;
	}

	//------------------------------------------------------------------
int main()
{
    layout = {0x150000000, 0x150002000, 0x260000000, 0x260003000,
              0x150000000, 0x260000000, 0x370000000, 0x370004000, true};
    const Regions recording = layout;
    const Regions hydration = {0x790000000, 0x790002000, 0x380000000, 0x380003000,
                               0x790000000, 0x380000000, 0x180000000, 0x180004000, true};
    const uptr starts[] = {recording.imgBegin, recording.dataBegin, recording.writableBegin};
    const uptr ends[] = {recording.imgEnd, recording.dataEnd, recording.writableEnd};
    const uptr rebased[] = {hydration.imgBegin, hydration.dataBegin, hydration.writableBegin};
    const u8 types[] = {kTargetImage, kTargetData, kTargetImageWritable};
    for (unsigned i = 0; i < 3; ++i)
    {
        layout = recording;
        assert(RegionBase(types[i]) == starts[i]);
        for (const uptr address : {starts[i], starts[i] + 4095, ends[i] - 1})
        {
            assert(RegionOf(reinterpret_cast<const void*>(address)) == types[i]);
            const s64 offset = static_cast<s64>(address - RegionBase(types[i]));
            layout = hydration;
            u64 resolved = 0;
            assert(ResolveRegionTarget({kFormMovRun, types[i], offset}, resolved));
            assert(resolved == rebased[i] + offset);
            layout = recording;
        }
        assert(RegionOf(reinterpret_cast<const void*>(starts[i] - 1)) == kTargetCount);
        assert(RegionOf(reinterpret_cast<const void*>(ends[i])) == kTargetCount);
        layout = hydration;
        u64 output = 0;
        assert(ResolveRegionTarget({kFormRel26, types[i], 0}, output) == (types[i] == kTargetImage));
        assert(!ResolveRegionTarget({kFormMovRun, types[i], -1}, output));
        assert(!ResolveRegionTarget({kFormMovRun, types[i], static_cast<s64>(ends[i] - starts[i])}, output));
        assert(!ResolveRegionTarget({kFormMovRun, types[i], std::numeric_limits<s64>::max()}, output));
    }
    u64 output = 0;
    assert(!ResolveRegionTarget({kFormMovRun, kTargetCount, 0}, output));
    layout.valid = false;
    assert(!ResolveRegionTarget({kFormMovRun, kTargetImage, 0}, output));
    assert(RegionOf(reinterpret_cast<const void*>(hydration.imgBegin)) == kTargetCount);
    std::puts("Three independent relocation bases, changed order, gaps and invalid offsets verified");
}
