#include "checkpoint-stall-seed.h"
#include "checkpoint-extended-seed.h"
#include "checkpoint-burst-seed.h"
#include "checkpoint-late-suffix.h"

#ifdef IZPS2_HOSTED_WORKER
bool recEeCheckpointWorkerIdle();
bool recEeCheckpointIopIdle();
#endif

#ifdef IZPS2_SWITCH2_CHECKPOINT_PUBLICATION_SCOPE
#include "common/ScopedGuard.h"
bool iz_ps2_begin_ee_checkpoint_ranges();
void iz_ps2_end_ee_warm_ranges();
#endif

// Uses the module's existing checksummed, atomic cache publication path.
#if defined(__SWITCH2__) && defined(IZPS2_SWITCH2_EE_WARM_FASTLOAD)
#include "ps2_ee_warm_fastload.hpp"
#endif

namespace {

struct Sb4EeWarmProfile
{
    u32 disc = 0;
    u32 elf = 0;
    std::string key;
    std::string path;
    std::vector<u8> previous;
    std::vector<u32> matched;
    unsigned limit = ps2::ee_warm::DefaultEntries;
    bool fastload_restored = true;
    bool read_only_seed = false;
};

constexpr auto EeWarmFileKind = static_cast<GSCacheFile::Kind>(0x31574545); // EEW1
constexpr auto EeWarmSeedKind = static_cast<GSCacheFile::Kind>(0x31534545); // EES1
constexpr double EeWarmStartupBudgetMs = 100.0;
constexpr double EeWarmBurstBudgetMs = 750.0;
// Verified checkpoint data has a fixed ordered prefix. Keep the unverified
// profile on the ordinary budget and give the exact prefix time to finish.
constexpr double EeWarmCheckpointBudgetMs = 1150.0;
constexpr double EeWarmCheckpointBurstBudgetMs = 1400.0;

bool checkpoint_burst_extension_matches(std::span<const u8> bytes)
{
    return ExtendsEeCompileSeed(izps2_checkpoint_burst_seed, bytes, ps2::ee_warm::CheckpointMaxEntries);
}

GSCacheFile::Stamp ee_warm_stamp(const Sb4EeWarmProfile& profile)
{
    return GSCacheFile::Stamp().Add("disc", profile.disc).Add("elf", profile.elf);
}

GSCacheFile::Stamp ee_warm_seed_stamp(const Sb4EeWarmProfile& profile)
{
    const auto ram = GSCacheFile::Hash128(eeMem->Main, Ps2MemSize::ExposedRam);
    return ee_warm_stamp(profile)
        .AddHex("ee-ram", ram.bytes, sizeof(ram.bytes))
        .Add("ee-pc", cpuRegs.pc)
        .Add("ee-cycle", cpuRegs.cycle);
}

bool sb4_ee_warm_ready(const Ps2VmControl& control)
{
    return control.jit_cache && SysMemory::HasCodeMemory() && Cpu == &recCpu &&
        VMManager::Internal::HasBootedELF() && VMManager::GetDiscCRC();
}

Sb4EeWarmProfile start_sb4_ee_warm(const Ps2VmControl& control, bool restored_checkpoint,
    bool allow_isolated_mtvu, bool prefer_stall_seed, bool extend_embedded_seed,
    bool warm_compile_burst, bool read_burst_extension, bool embedded_late_requested)
{
    if (!sb4_ee_warm_ready(control))
        return {};

    const u32 disc = VMManager::GetDiscCRC();
    const u32 elf = VMManager::GetCurrentCRC();

    Sb4EeWarmProfile profile;
    profile.disc = disc;
    profile.elf = elf;
    profile.key = ps2::ee_warm::Key(disc, elf);
    const bool burst = FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/ee-warm-burst.flag");
    const auto ordinary_path = Path::Combine(EmuFolders::Cache, profile.key + ".bin");
    profile.path = burst ? Path::Combine(EmuFolders::Cache, profile.key + "-burst.bin") : ordinary_path;
    profile.limit = burst ? ps2::ee_warm::MaxEntries : ps2::ee_warm::DefaultEntries;
    Console.WriteLn("IZPS2 EE warm capture disc=%08x elf=%08x", disc, elf);
    const auto input_start = Common::Timer::GetCurrentValue();
    const auto read_profile = [&](const std::string& path, unsigned limit, GSCacheFile::Kind kind,
                                  const GSCacheFile::Stamp& stamp)
    {
        const size_t max_file_bytes = ps2::ee_warm::HeaderBytes + limit * ps2::ee_warm::RecordBytes + 128;
        FILESYSTEM_STAT_DATA info{};
        const bool stat_ok = FileSystem::StatFile(path.c_str(), &info);
        const char* status = "stat-failed";
        bool ok = false;
        profile.previous.clear();
        if (stat_ok)
        {
            if (info.Size < 0 || static_cast<size_t>(info.Size) > max_file_bytes)
                status = "size-rejected";
            else
            {
                const auto result = GSCacheFile::ReadFramedFile(path, kind, stamp, &profile.previous);
                status = GSCacheFile::ReadResultString(result);
                ok = result == GSCacheFile::ReadResult::Ok;
            }
        }
        Console.WriteLn("IZPS2_EE_WARM_INPUT status=\"%s\" file_bytes=%lld payload_bytes=%zu elapsed_ms=%.3f path=\"%s\"",
            status, stat_ok ? static_cast<long long>(info.Size) : -1LL, profile.previous.size(),
            Common::Timer::ConvertValueToMilliseconds(Common::Timer::GetCurrentValue() - input_start),
            path.c_str());
        return ok;
    };
    bool vu_queue_idle = !THREAD_VU1;
#ifdef IZPS2_HOSTED_WORKER
    if (restored_checkpoint && THREAD_VU1 && allow_isolated_mtvu)
    {
        vu_queue_idle = recEeCheckpointWorkerIdle();
        Console.WriteLn("IZPS2_EE_WARM_MTVU idle=%u", unsigned(vu_queue_idle));
    }
#endif
    bool iop_idle = IopThread::GetMode() == IopThreadMode::Off;
#ifdef IZPS2_HOSTED_WORKER
    iop_idle = iop_idle || recEeCheckpointIopIdle();
#endif
    const bool seed_requested = restored_checkpoint && !CHECK_FASTMEM && vu_queue_idle && iop_idle;
    std::span<const u8> embedded_seed;
    std::vector<u8> late_extension;
    if (seed_requested)
    {
        const auto stamp = ee_warm_seed_stamp(profile);
        if (prefer_stall_seed && stamp.GetDigest() == kStallSeedStamp)
        {
            if (!extend_embedded_seed)
                embedded_seed = izps2_stall_seed;
            else if (warm_compile_burst)
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
                else if (embedded_late_requested)
                {
                    constexpr size_t late_bytes = sizeof(izps2_checkpoint_late_suffix);
                    constexpr size_t total_bytes = sizeof(izps2_checkpoint_burst_seed) + late_bytes;
                    constexpr u32 entries = (total_bytes - ps2::ee_warm::HeaderBytes) / ps2::ee_warm::RecordBytes;
                    static_assert(late_bytes % ps2::ee_warm::RecordBytes == 0);
                    static_assert(entries <= ps2::ee_warm::CheckpointMaxEntries);
                    late_extension.reserve(total_bytes);
                    late_extension.assign(embedded_seed.begin(), embedded_seed.end());
                    late_extension.insert(late_extension.end(), std::begin(izps2_checkpoint_late_suffix),
                                          std::end(izps2_checkpoint_late_suffix));
                    for (unsigned byte = 0; byte < sizeof(entries); ++byte)
                        late_extension[16 + byte] = static_cast<u8>(entries >> (8 * byte));
                    const bool accepted = checkpoint_burst_extension_matches(late_extension);
                    if (accepted)
                        embedded_seed = late_extension;
                    Console.WriteLn("IZPS2_EE_EMBEDDED_LATE accepted=%u entries=%u suffix_bytes=%zu",
                        unsigned(accepted), accepted ? entries : 0, late_bytes);
                }
            }
            else
                embedded_seed = izps2_extended_stall_seed;
            profile.read_only_seed = true;
        }
        else
        {
            const auto seed_path = Path::Combine(EmuFolders::Cache, profile.key + "-checkpoint.bin");
            profile.read_only_seed = read_profile(seed_path, ps2::ee_warm::MaxEntries, EeWarmSeedKind, stamp);
        }
        if (prefer_stall_seed)
            Console.WriteLn("IZPS2_EE_STALL_SEED accepted=%u bytes=%zu", unsigned(!embedded_seed.empty()), embedded_seed.size());
        Console.WriteLn("IZPS2_EE_WARM_CHECKPOINT accepted=%u", unsigned(profile.read_only_seed));
    }
    const auto ordinary_stamp = ee_warm_stamp(profile);
    bool read_ok = profile.read_only_seed || read_profile(profile.path, profile.limit, EeWarmFileKind, ordinary_stamp);
    if (!read_ok && burst)
        read_ok = read_profile(ordinary_path, ps2::ee_warm::DefaultEntries, EeWarmFileKind, ordinary_stamp);
    const bool extended = extend_embedded_seed && !embedded_seed.empty();
    const unsigned warm_limit = extended ? ps2::ee_warm::CheckpointMaxEntries :
        profile.read_only_seed ? ps2::ee_warm::MaxEntries : profile.limit;
    const auto order = extended ? ps2::ee_warm::Order::EmbeddedPrefix :
        profile.read_only_seed ? ps2::ee_warm::Order::Prefix : ps2::ee_warm::Order::Independent;
    double budget_ms = burst ? EeWarmBurstBudgetMs : EeWarmStartupBudgetMs;
    if (profile.read_only_seed)
        budget_ms = embedded_seed.size() >= sizeof(izps2_checkpoint_burst_seed) ?
            EeWarmCheckpointBurstBudgetMs : EeWarmCheckpointBudgetMs;
    if (burst)
        Console.WriteLn("IZPS2_EE_WARM_BURST limit=%u budget_ms=%.0f", profile.limit, budget_ms);
    if (read_ok)
    {
#if defined(__SWITCH2__) && defined(IZPS2_SWITCH2_EE_WARM_FASTLOAD)
        const bool request_fastload = (burst || profile.read_only_seed) && restored_checkpoint &&
            FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/ee-warm-fastload.flag");
        Ps2EeWarmFastLoad fastload(request_fastload);
#endif
        const auto start = Common::Timer::GetCurrentValue();
        unsigned attempted = 0;
        unsigned available = 0;
        const auto result = [&] {
#ifdef IZPS2_SWITCH2_CHECKPOINT_PUBLICATION_SCOPE
            // The verified seed runs while EE and IOP are parked. Keep
            // the per-submit stop/budget checks, and drain the provider
            // before leaving this scope even when the prefix stops early.
            const bool scoped_publication = profile.read_only_seed && iz_ps2_begin_ee_checkpoint_ranges();
            ScopedGuard close_publication([scoped_publication] {
                if (scoped_publication)
                    iz_ps2_end_ee_warm_ranges();
            });
#endif
            const std::span<const u8> input = embedded_seed.empty() ? std::span<const u8>(profile.previous) : embedded_seed;
            return ps2::ee_warm::Warm(input, disc, elf,
                {eeMem->Main, Ps2MemSize::ExposedRam}, [&](std::span<const ps2::ee_warm::Entry> entries) {
                    if (__atomic_load_n(control.stop_requested, __ATOMIC_ACQUIRE) ||
                        Common::Timer::ConvertValueToMilliseconds(Common::Timer::GetCurrentValue() - start) >=
                            budget_ms)
                        return false;
                    attempted += entries.size();
                    if (profile.read_only_seed)
                    {
                        // Later entries can depend on the preceding block boundaries.
                        const u32 compiled = recEeWarmOrderedBatch(entries);
                        available += compiled;
                        return compiled == entries.size();
                    }
                    for (const auto& entry : entries)
                        profile.matched.push_back(entry.pc);
                    available += recEeWarmBatch(entries);
                    return true;
                }, warm_limit, order);
        }();
        if (!result.valid)
            profile.previous.clear();
#if defined(__SWITCH2__) && defined(IZPS2_SWITCH2_EE_WARM_FASTLOAD)
        profile.fastload_restored = fastload.Restore();
        if (request_fastload)
            Console.WriteLn("IZPS2_EE_WARM_FASTLOAD request_rc=0x%08x cancel_rc=0x%08x normal_rc=0x%08x fallback=%d restored=%d",
                fastload.request_result, fastload.cancel_result, fastload.normal_result,
                fastload.fallback_used ? 1 : 0, profile.fastload_restored ? 1 : 0);
#endif
        std::sort(profile.matched.begin(), profile.matched.end());
        Console.WriteLn("IZPS2 EE warm profile valid=%u matched=%u stale=%u attempted=%u available=%u elapsed_ms=%.3f",
            unsigned(result.valid), result.usable, result.stale, attempted, available,
            Common::Timer::ConvertValueToMilliseconds(Common::Timer::GetCurrentValue() - start));
    }
    if (!profile.read_only_seed)
        recEeStartWarmProfileWithLimit(disc, elf, profile.limit);
    return profile;
}

void finish_sb4_ee_warm(const Sb4EeWarmProfile& profile)
{
    if (!profile.disc || profile.read_only_seed)
        return;

    std::vector<u8> observed(ps2::ee_warm::HeaderBytes + profile.limit * ps2::ee_warm::RecordBytes);
    observed.resize(recEeFinishWarmProfile(profile.key, observed));
    if (VMManager::GetDiscCRC() != profile.disc || VMManager::GetCurrentCRC() != profile.elf)
    {
        Console.WriteLn("IZPS2 EE warm profile skipped=title-changed observed_bytes=%zu", observed.size());
        return;
    }
    if (observed.empty())
    {
        Console.WriteLn("IZPS2 EE warm profile skipped=no-records");
        return;
    }

    std::vector<u8> merged;
    if (ps2::ee_warm::MergeProfiles(profile.previous, profile.matched, observed, merged, profile.limit))
    {
        const bool saved = GSCacheFile::WriteFramedFile(profile.path, EeWarmFileKind,
            ee_warm_stamp(profile), merged.data(), merged.size());
        Console.WriteLn("IZPS2 EE warm profile saved=%u bytes=%zu", unsigned(saved), merged.size());
    }
    else
    {
        Console.WriteLn("IZPS2 EE warm profile skipped=unchanged observed_bytes=%zu", observed.size());
    }
}

} // namespace
