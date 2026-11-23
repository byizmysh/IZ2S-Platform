constexpr auto EeLateWarmKind = static_cast<GSCacheFile::Kind>(0x324c4545); // EEL2
constexpr double EeLateWarmBudgetMs = 250.0;

struct Sb4EeLateWarm
{
    Sb4EeLateWarm(u32 disc, u32 elf, const std::string& key, GSCacheFile::Stamp identity)
        : disc(disc), elf(elf), path(Path::Combine(EmuFolders::Cache, key + "-late-bursts-v2.bin")),
          identity(std::move(identity)),
          capture(Common::Timer::ConvertMillisecondsToValue(ps2::ee_warm::LateWarmCapture::FrameThresholdMs),
                  iz_tick_frequency() * ps2::ee_warm::LateWarmCapture::CompileThresholdMs / 1000) {}

    u32 disc, elf;
    std::string path;
    GSCacheFile::Stamp identity;
    std::vector<u8> previous;
    ps2::ee_warm::LateWarmCapture capture;
};

std::unique_ptr<Sb4EeLateWarm> start_sb4_ee_late_warm(const Sb4EeWarmProfile& profile,
                                                     const Ps2VmControl& control)
{
    using ps2::ee_warm::LateWarmCapture;
    if (!iz_tick_frequency())
        return {};

    // The stock prefix already authenticated RAM, PC, guest cycle and disc.
    // A separate cache lets later blocks be verified individually and sorted by cost.
    auto identity = ee_warm_stamp(profile)
        .AddHex("embedded-checkpoint", kStallSeedStamp.bytes, sizeof(kStallSeedStamp.bytes));
    auto tail = std::unique_ptr<Sb4EeLateWarm>(
        new (std::nothrow) Sb4EeLateWarm(profile.disc, profile.elf, profile.key, std::move(identity)));
    if (!tail || !tail->capture.Available())
        return {};

    for (const auto& entry : profile.embedded_entries)
        tail->capture.Cover(entry.pc, entry.bytes);

    FILESYSTEM_STAT_DATA info{};
    constexpr size_t max_file = 128 + ps2::ee_warm::HeaderBytes + LateWarmCapture::Limit * ps2::ee_warm::RecordBytes;
    if (FileSystem::StatFile(tail->path.c_str(), &info) && info.Size >= 0 && static_cast<u64>(info.Size) <= max_file &&
        GSCacheFile::ReadFramedFile(tail->path, EeLateWarmKind, tail->identity, &tail->previous) == GSCacheFile::ReadResult::Ok)
    {
        unsigned available = 0;
        std::vector<u8> accepted(tail->previous.begin(),
            tail->previous.begin() + std::min<size_t>(ps2::ee_warm::HeaderBytes, tail->previous.size()));
        accepted.reserve(tail->previous.size());
        const auto begin = Common::Timer::GetCurrentValue();
        const auto result = [&] {
#ifdef IZPS2_SWITCH2_CHECKPOINT_PUBLICATION_SCOPE
            const bool batched = iz_ps2_begin_ee_checkpoint_ranges();
            const ScopedGuard drain([batched] { if (batched) iz_ps2_end_ee_warm_ranges(); });
#endif
            return ps2::ee_warm::Warm(tail->previous, tail->disc, tail->elf,
                {eeMem->Main, Ps2MemSize::ExposedRam}, [&](std::span<const ps2::ee_warm::Entry> entries) {
                    if (__atomic_load_n(control.stop_requested, __ATOMIC_ACQUIRE) ||
                        Common::Timer::ConvertValueToMilliseconds(Common::Timer::GetCurrentValue() - begin) >=
                            EeLateWarmBudgetMs)
                        return false;
                    const auto warmed = recEeWarmOrderedBatch(entries);
                    available += warmed;
                    for (const auto& entry : entries.first(warmed))
                    {
                        tail->capture.Cover(entry.pc, entry.bytes);
                        const auto at = accepted.size();
                        accepted.resize(at + ps2::ee_warm::RecordBytes);
                        ps2::ee_warm::WriteLE(std::span(accepted).subspan(at, 4), entry.pc);
                        ps2::ee_warm::WriteLE(std::span(accepted).subspan(at + 4, 4), entry.bytes);
                        ps2::ee_warm::WriteLE(std::span(accepted).subspan(at + 8, 8),
                            ps2::ee_warm::Hash({eeMem->Main + entry.pc, entry.bytes}));
                    }
                    return warmed == entries.size();
                }, LateWarmCapture::Limit);
        }();
        if (result.valid && available)
        {
            ps2::ee_warm::WriteLE(std::span(accepted).subspan(16, 4), available);
            tail->previous = std::move(accepted);
        }
        else
        {
            tail->previous.clear();
        }
        Console.WriteLn("IZPS2_EE_LATE_WARM valid=%u available=%u stale=%u elapsed_ms=%.3f",
            unsigned(result.valid), available, result.stale,
            Common::Timer::ConvertValueToMilliseconds(Common::Timer::GetCurrentValue() - begin));
    }
    else
    {
        tail->previous.clear();
    }

    ps2::ee_warm::active_late_warm_capture = &tail->capture;
    return tail;
}

void finish_sb4_ee_late_warm(const Sb4EeLateWarm& tail)
{
    if (VMManager::GetDiscCRC() != tail.disc || VMManager::GetCurrentCRC() != tail.elf)
        return;

    std::vector<u8> output;
    if (!tail.capture.Build(tail.disc, tail.elf, tail.previous, output))
        return;

    const auto deadline = GSCacheFile::Clock::now() + std::chrono::milliseconds(250);
    const bool saved = GSCacheFile::WriteFramedFile(tail.path, EeLateWarmKind, tail.identity,
        output.data(), output.size(), deadline);
    Console.WriteLn("IZPS2_EE_LATE_WARM saved=%u records=%zu", unsigned(saved),
        (output.size() - ps2::ee_warm::HeaderBytes) / ps2::ee_warm::RecordBytes);
}
