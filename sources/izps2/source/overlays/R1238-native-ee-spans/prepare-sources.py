"""Record actual guest spans on RUN25 and qualify a prefix extension at load time."""
from pathlib import Path

out = Path(__file__).resolve().parent
base = out.parent/'R1236-ee-iop-split'
core = out.parent/'R1223-ee-compact-exits'


def change(text, old, new):
    assert text.count(old) == 1, old
    return text.replace(old,new)


source = (core/'iR5900-arm64.cpp').read_text()
anchor = '#include "Common.h"'
source = change(source,anchor,anchor+'\n#include "ee-span-trace.hpp"')
anchor = '''#if defined(PS2_HOST_CODE_BRIDGE) || defined(IZPS2_SWITCH2_EE_WARM)
	if (!warm_batch && (!EmuConfig.EnablePatches || !s_eeMayHaveDynamicPatches))
		capture_ee_block(startpc, smc_words * 4, scanned_block_bytes);
#endif'''
source = change(source,anchor,anchor+'''
	if (auto* const trace = iz_ps2_ee_span_trace; trace && !warm_batch) [[unlikely]]
	{
		const u32 bytes = s_nEndBlock - startpc;
		bool eligible = !s_eeMayHaveDynamicPatches && scanned_block_bytes &&
			eeWarmSpanEligible(startpc, bytes) && scanned_block_bytes == eeMem->Main + startpc &&
			eeWarmDelaySlotVerified(scanned_block_bytes, bytes);
		if (eligible)
		{
			const u32 next = (startpc & ~u32{0xfff}) + _4kb;
			const auto map = vtlb_private::vtlbdata.vmap[next >> vtlb_private::VTLB_PAGE_BITS];
			eligible = !map.isHandler(next) && map.assumePtr(next) == reinterpret_cast<uptr>(eeMem->Main + next);
		}
		trace->Record(startpc, bytes, eligible, eligible ? ps2::ee_warm::Hash({scanned_block_bytes, bytes}) : 0);
	}
''')
# Match the usual capture condition for a run where patches are disabled.
source = change(source,'bool eligible = !s_eeMayHaveDynamicPatches && scanned_block_bytes &&',
                'bool eligible = (!EmuConfig.EnablePatches || !s_eeMayHaveDynamicPatches) && scanned_block_bytes &&')
(out/'iR5900-arm64.cpp').write_text(source)

warm = (base/'ps2_sb4_ee_warm.inc').read_text()
warm = change(warm,'constexpr double EeWarmCheckpointBurstBudgetMs = 1400.0;',
    '''constexpr double EeWarmCheckpointBurstBudgetMs = 1400.0;

bool checkpoint_burst_extension_matches(std::span<const u8> bytes)
{
    return ExtendsEeCompileSeed(izps2_checkpoint_burst_seed, bytes, ps2::ee_warm::CheckpointMaxEntries);
}''')
warm=change(warm, '    bool warm_compile_burst)', '    bool warm_compile_burst, bool read_burst_extension)')
warm=change(warm, '    std::span<const u8> embedded_seed;', '''    std::span<const u8> embedded_seed;
    std::vector<u8> late_extension;''')
warm=change(warm,'''            else if (warm_compile_burst)
                embedded_seed = izps2_checkpoint_burst_seed;''','''            else if (warm_compile_burst)
            {
                embedded_seed = izps2_checkpoint_burst_seed;
                if (read_burst_extension)
                {
                    // The frame boot identity has already matched. The payload
                    // must also extend this exact ordered seed byte for byte.
                    const auto path = Path::Combine(EmuFolders::Cache, "izps2-gta-ee-burst-extension.bin");
                    FILESYSTEM_STAT_DATA file{};
                    constexpr s64 max_frame_bytes = 56 + ps2::ee_warm::HeaderBytes +
                        ps2::ee_warm::CheckpointMaxEntries * ps2::ee_warm::RecordBytes;
                    const bool accepted = FileSystem::StatFile(path.c_str(), &file) &&
                        file.Size <= max_frame_bytes &&
                        GSCacheFile::ReadFramedFile(path, EeWarmSeedKind, stamp, &late_extension) ==
                            GSCacheFile::ReadResult::Ok && checkpoint_burst_extension_matches(late_extension);
                    if (accepted)
                        embedded_seed = late_extension;
                    Console.WriteLn("IZPS2_EE_BURST_EXTENSION accepted=%u bytes=%zu", unsigned(accepted),
                        accepted ? embedded_seed.size() : 0);
                }
            }''')
warm=change(warm,'budget_ms = embedded_seed.size() == sizeof(izps2_checkpoint_burst_seed) ?',
                  'budget_ms = embedded_seed.size() >= sizeof(izps2_checkpoint_burst_seed) ?')
(out/'ps2_sb4_ee_warm.inc').write_text(warm)

source = (base/'ps2_vm_runner.cpp').read_text()
source = change(source,'static Ps2NeutralReplay s_neutral_replay;',
'''#include "ee-span-trace.hpp"
static Ps2NeutralReplay s_neutral_replay;
EeCompileSpanTrace* iz_ps2_ee_span_trace = nullptr;''')
source = change(source, '    s_neutral_replay = {};', '''    s_neutral_replay = {};
    for (auto& probe : s_ee_target_probes) probe = {probe.field};''')
source = change(source,'    const auto prepare_ee_warm =', '''    const bool read_burst_extension =
#ifdef IZPS2_HOSTED_WORKER
        hosted && module_flag(*hosted, "ee-checkpoint-burst-extension.flag");
#else
        false;
#endif
    const auto prepare_ee_warm =''')
source = change(source,'extend_embedded_seed, warm_compile_burst);','extend_embedded_seed, warm_compile_burst, read_burst_extension);')
anchor = '    if (restore_checkpoint) {'
source = change(source,anchor,'''    std::unique_ptr<EeCompileSpanTrace> span_trace;
    u64 span_baseline_blocks = 0;
    const ScopedGuard clear_span_trace([] { iz_ps2_ee_span_trace = nullptr; });
'''+anchor)
anchor = '''                return {Ps2VmResult::CpuFailed, false, "Unable to restore the normal applet clock profile."};
            }
#endif
#ifdef IZPS2_TEST_SOFTMEM_AB'''
source = change(source,anchor,'''                return {Ps2VmResult::CpuFailed, false, "Unable to restore the normal applet clock profile."};
            }
#if defined(IZPS2_HOSTED_WORKER) && defined(IZPS2_SWITCH2_SESSION_TIMING)
            if (hosted && neutral_requested && ee_warm.read_only_seed && !read_burst_extension &&
                module_flag(*hosted, "perf-ee-compile-spans.flag"))
            {
                span_trace.reset(new (std::nothrow) EeCompileSpanTrace);
                if (span_trace)
                {
                    span_baseline_blocks = recGetEeCompileAudit().blocks;
                    iz_ps2_ee_span_trace = span_trace.get();
                }
                Console.WriteLn("IZPS2_EE_SPAN_TRACE armed=%u", unsigned(bool(span_trace)));
            }
#endif
#endif
#ifdef IZPS2_TEST_SOFTMEM_AB''')
anchor='    if (control) report_frame_windows();\n    using ReplayState'
source=change(source,anchor,'''    iz_ps2_ee_span_trace = nullptr;
    if (span_trace)
    {
        const auto records = span_trace->Records();
        const auto audited = recGetEeCompileAudit().blocks - span_baseline_blocks;
        const bool complete = s_neutral_replay.state == Ps2NeutralReplay::State::Complete &&
            !span_trace->Lost() && records.size() == audited;
        const auto path = Path::Combine(EmuFolders::Logs, "izps2-r1238-ee-spans.bin");
        const bool saved = complete && GSCacheFile::WriteFileAtomic(path, records.data(), records.size_bytes(),
            GSCacheFile::Clock::now() + std::chrono::milliseconds(250));
        Console.WriteLn("IZPS2_EE_SPAN_TRACE saved=%u records=%zu lost=%u audited=%llu crc=%08x",
            unsigned(saved), records.size(), span_trace->Lost(), static_cast<unsigned long long>(audited),
            crc32(0, reinterpret_cast<const u8*>(records.data()), records.size_bytes()));
    }
    if (control) report_frame_windows();
    for (const auto& probe : s_ee_target_probes) probe.Report(iz_tick_frequency());
    using ReplayState''')
(out/'ps2_vm_runner.cpp').write_text(source)

windows = (base/'ps2_frame_windows.inc').read_text()
windows = change(windows, 'Ps2FrameWindows s_frame_windows;', '''Ps2FrameWindows s_frame_windows;

struct EeFieldProbe
{
    u32 field;
    u64 start_tick = 0;
    u64 end_tick = 0;
    EeCompileAudit begin{};
    EeCompileAudit end{};

    void Observe(u32 current, u64 tick)
    {
        if (current == field)
        {
            start_tick = tick;
            begin = recGetEeCompileAudit();
        }
        else if (current == field + 1 && start_tick)
        {
            end_tick = tick;
            end = recGetEeCompileAudit();
        }
    }

    void Report(u64 hz) const
    {
        if (end_tick > start_tick && end.blocks >= begin.blocks && end.ticks >= begin.ticks && hz)
            Console.WriteLn("IZPS2_EE_TARGET_FIELD start=%u wall_ms=%.6f ee_blocks=%llu ee_compile_ms=%.6f",
                field, Common::Timer::ConvertValueToMilliseconds(end_tick - start_tick),
                static_cast<unsigned long long>(end.blocks - begin.blocks), 1000.0 * (end.ticks - begin.ticks) / hz);
    }
};
EeFieldProbe s_ee_target_probes[] = { {5170}, {6212} };''')
windows = change(windows, '    const auto tick = Common::Timer::GetCurrentValue();',
    '''    const auto tick = Common::Timer::GetCurrentValue();
    if (s_neutral_replay.state == Ps2NeutralReplay::State::Running)
        for (auto& probe : s_ee_target_probes) probe.Observe(frame, tick);''')
windows = change(windows,'    iz_ps2_publish_deferred_ee_links();',
                  '    if (iz_ps2_ee_span_trace) iz_ps2_ee_span_trace->SetField(frame);\n    iz_ps2_publish_deferred_ee_links();')
(out/'ps2_frame_windows.inc').write_text(windows)
