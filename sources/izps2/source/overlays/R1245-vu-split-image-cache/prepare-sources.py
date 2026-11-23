"""Give the VU program cache the same split image layout as the RUN25 loader."""

from pathlib import Path
import shutil

out = Path(__file__).resolve().parent
base = out.parent / 'R1244-default-late-ee-seed'
vu = out.parent / 'R1175-publish-owners-and-stalls'
snapshot = out.parent / 'R1118-ee-softmem-unaligned-pairs/candidate-source'


def change(source, before, after):
    assert source.count(before) == 1, before
    return source.replace(before, after)


for name in ('microVU-arm64.cpp', 'microVU-arm64.h', 'microVU_Branch-arm64.inl'):
    shutil.copyfile(vu / name, out / name)
for name in ('ps2_sb4_ee_warm.inc', 'checkpoint-late-suffix.bin'):
    shutil.copyfile(base / name, out / name)
header = (base / 'checkpoint-late-suffix.h').read_text().replace(str(base), str(out))
(out / 'checkpoint-late-suffix.h').write_text(header)

script = (base / 'engine.ld').read_text()
script = change(script, '    .text : {', '    .text : {\n        HIDDEN(__iz_vu_rx_start = .);')
script = change(script, '    .got : { *(.got .igot) } :code',
    '    .got : { *(.got .igot) } :code\n    HIDDEN(__iz_vu_rx_last = . - 1);')
script = change(script, '    . = ALIGN(4096) + 0x100000;',
    '    . = ALIGN(4096) + 0x100000;\n    HIDDEN(__iz_vu_rw_start = .);')
script = change(script, '    .bss : { *(.bss .bss.* .gnu.linkonce.b.*) *(COMMON) } :data',
    '    .bss : { *(.bss .bss.* .gnu.linkonce.b.*) *(COMMON) } :data\n    HIDDEN(__iz_vu_rw_last = . - 1);')
(out / 'engine.ld').write_text(script)

source = (snapshot / 'microVU_Persist-arm64.inl').read_text()
source = change(source, 'namespace mVUPersist\n{\n\tenum : u8', '''#ifdef IZPS2_SWITCH2_VU_SPLIT_PERSIST
extern "C"
{
    extern const u8 __iz_vu_rx_start[], __iz_vu_rx_last[];
    extern const u8 __iz_vu_rw_start[], __iz_vu_rw_last[];
    // Kept in RX and filled from both PT_LOAD segments before .izdata export.
    [[gnu::used, gnu::visibility("hidden")]] extern const u8 iz_vu_cache_image_seal[32];
}
asm(R"seal(.pushsection .rodata.iz_vu_cache_image_seal,"a",%progbits
.globl iz_vu_cache_image_seal
.hidden iz_vu_cache_image_seal
.type iz_vu_cache_image_seal,%object
iz_vu_cache_image_seal:
.zero 32
.size iz_vu_cache_image_seal,.-iz_vu_cache_image_seal
.popsection)seal");
#endif

namespace mVUPersist
{
\tenum : u8''')
source = change(source, '\t\tkTargetData,            // data arena base + offset\n\t\tkTargetCount,', '''\t\tkTargetData,            // data arena base + offset
#ifdef IZPS2_SWITCH2_VU_SPLIT_PERSIST
\t\tkTargetImageWritable,   // separately mapped engine data and bss
#endif
\t\tkTargetCount,''')
source = change(source, '\t\tbool valid = false;\n\t};\n\n#if defined(__linux__)', '''#ifdef IZPS2_SWITCH2_VU_SPLIT_PERSIST
		uptr writableBegin = 0, writableEnd = 0;
#endif
		bool valid = false;
	};

#ifdef IZPS2_SWITCH2_VU_SPLIT_PERSIST
	// Called on the EE owner before it starts VMManager and MTVU.
	static bool s_hostedCacheRequested = false;
	void ConfigureHostedPersist(bool requested) { s_hostedCacheRequested = requested; }

	static bool HasHostedImageSeal()
	{
		return std::any_of(std::begin(iz_vu_cache_image_seal), std::end(iz_vu_cache_image_seal),
			[](u8 byte) { return byte != 0; });
	}
#endif

#if defined(__linux__)''')
source = change(source, '''	static const Regions& GetRegions()
	{
		static Regions r = []() {''', '''#ifdef IZPS2_SWITCH2_VU_SPLIT_PERSIST
	static Regions GetRegions()
	{
		if (!s_hostedCacheRequested)
			return {};
		static const Regions image = []() {
			Regions out;
			out.imgBegin = reinterpret_cast<uptr>(__iz_vu_rx_start);
			out.imgEnd = reinterpret_cast<uptr>(__iz_vu_rx_last) + 1;
			out.writableBegin = reinterpret_cast<uptr>(__iz_vu_rw_start);
			out.writableEnd = reinterpret_cast<uptr>(__iz_vu_rw_last) + 1;
			out.imageBase = out.imgBegin;
			const uptr probe = reinterpret_cast<uptr>(&microVU0);
			out.valid = HasHostedImageSeal() && out.imgBegin && out.writableBegin &&
				!((out.imgBegin | out.writableBegin) & 0xfff) && out.imgBegin < out.imgEnd &&
				out.writableBegin < out.writableEnd && probe >= out.writableBegin && probe < out.writableEnd &&
				(out.imgEnd <= out.writableBegin || out.writableEnd <= out.imgBegin);
			return out;
		}();
		Regions out = image;
		if (!out.valid)
			return out;
		out.dataBegin = reinterpret_cast<uptr>(SysMemory::GetDataPtr(0));
		out.dataEnd = out.dataBegin + HostMemoryMap::MainSize;
		out.dataBase = out.dataBegin;
		out.valid = out.dataBegin && !(out.dataBegin & 0xfff) && out.dataEnd > out.dataBegin &&
			(out.dataEnd <= out.imgBegin || out.imgEnd <= out.dataBegin) &&
			(out.dataEnd <= out.writableBegin || out.writableEnd <= out.dataBegin);
		return out;
	}
#else
	static const Regions& GetRegions()
	{
		static Regions r = []() {''')
source = change(source, '''		return r;
	}

	// Which region''', '''		return r;
	}
#endif

	// Which region''')
source = change(source, '''		if (a >= r.imgBegin && a < r.imgEnd)
			return kTargetImage;''', '''		if (a >= r.imgBegin && a < r.imgEnd)
			return kTargetImage;
#ifdef IZPS2_SWITCH2_VU_SPLIT_PERSIST
		if (a >= r.writableBegin && a < r.writableEnd)
			return kTargetImageWritable;
#endif''')
source = change(source, '''		return (target == kTargetImage) ? r.imageBase : r.dataBase;
	}''', '''#ifdef IZPS2_SWITCH2_VU_SPLIT_PERSIST
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
	}''')
source = change(source, '''	static std::vector<u8> ReadBuildIdBytes()
	{
#if defined(__linux__)''', '''	static std::vector<u8> ReadBuildIdBytes()
	{
#if defined(IZPS2_SWITCH2_VU_SPLIT_PERSIST)
		return std::vector<u8>(std::begin(iz_vu_cache_image_seal), std::end(iz_vu_cache_image_seal));
#elif defined(__linux__)''')
source = change(source, '	static constexpr u32 kImageVersion = 3;', '''#ifdef IZPS2_SWITCH2_VU_SPLIT_PERSIST
	static constexpr u32 kImageVersion = 4; // distinct relocations for engine RX and engine RW
#else
	static constexpr u32 kImageVersion = 3;
#endif''')
source = change(source, '''				if ((f.codeOffset & 3) || f.codeOffset + FixupSpanInsns(f) * 4 > c.codeSize)
					return nullptr;''', '''				if ((f.codeOffset & 3) || f.codeOffset > c.codeSize ||
					FixupSpanInsns(f) * 4 > c.codeSize - f.codeOffset)
					return nullptr;
				u64 address = 0;
				if (IsRegionTarget(f.target) && !ResolveRegionTarget(f, address))
					return nullptr;''')
source = change(source, '''				case kTargetImage:
				case kTargetData:
					out = static_cast<u64>(RegionBase(f.target)) + static_cast<u64>(f.offset);
					return true;''', '''				case kTargetImage:
				case kTargetData:
#ifdef IZPS2_SWITCH2_VU_SPLIT_PERSIST
				case kTargetImageWritable:
#endif
					return ResolveRegionTarget(f, out);''')
(out / 'microVU_Persist-arm64.inl').write_text(source)

runner = (base / 'ps2_vm_runner.cpp').read_text()
runner = change(runner, '#include "ps2_vm_runner.h"', '''#include "ps2_vm_runner.h"
#include "arm64/microVU_Persist-arm64.h"
namespace mVUPersist { void ConfigureHostedPersist(bool requested); }''')
runner = change(runner, '    const bool observed_code_requested = hosted && module_flag(*hosted, "ee-parallel-writes.flag");',
    '''    const bool vu_cache_requested = hosted && module_flag(*hosted, "vu-split-persist.flag");
    mVUPersist::ConfigureHostedPersist(vu_cache_requested);
    const bool observed_code_requested = hosted && module_flag(*hosted, "ee-parallel-writes.flag");''')
runner = change(runner, '    trace_exit("27 VM shutdown returned\\n");', '''    trace_exit("27 VM shutdown returned\\n");
#ifdef IZPS2_HOSTED_WORKER
    if (vu_cache_requested)
    {
        const auto vu = mVUPersist::GetStats(1);
        Console.WriteLn("IZPS2_VU_SPLIT_CACHE recorded=%llu dropped=%llu hydrated_programs=%llu hydrated_blocks=%llu rejected=%llu",
            static_cast<unsigned long long>(vu.chunksRecorded), static_cast<unsigned long long>(vu.chunksDropped),
            static_cast<unsigned long long>(vu.programsHydrated), static_cast<unsigned long long>(vu.blocksHydrated),
            static_cast<unsigned long long>(vu.hydrationRejects));
    }
#endif''')
(out / 'ps2_vm_runner.cpp').write_text(runner)
