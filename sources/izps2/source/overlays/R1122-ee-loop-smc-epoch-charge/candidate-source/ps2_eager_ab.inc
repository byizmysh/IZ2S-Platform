// Included by ps2_frame_windows.inc in the VM-owner translation unit.
struct EagerAbEndpoint {
    uint64_t tick = 0;
    uint64_t presents = 0, gs_headless = 0, updates = 0, blits = 0;
    float nominal_hz = 0;
    Ps2FrameWindows::Timing timing{};
};

struct EagerAbDigest {
    uint64_t ee_cycle = 0;
    uint32_t ee_pc = 0, ram_crc = 0;
    bool operator==(const EagerAbDigest&) const = default;
};

struct EagerAbTrial {
    Ps2EagerAbFrames frames{};
    EagerAbEndpoint warm, first, last;
    EagerAbDigest digest, exact_digest;
    Common::Timer::Value launched = 0;
    const char* failure = nullptr;
    bool optimized = false;
    bool done = false;
};
EagerAbTrial* s_eager_ab = nullptr;

EagerAbEndpoint eager_ab_endpoint(uint64_t tick)
{
    const auto signals = PerformanceMetrics::GetGameFrameSignals();
    return {tick, iz_video_accepted_presentations(), GSHeadless::PresentCount(),
            signals.register_updates, signals.framebuffer_blits, VMManager::GetFrameRate(),
            sample_session_timing()};
}

void eager_ab_record(uint32_t frame, uint64_t tick)
{
    auto* trial = s_eager_ab;
    if (!trial || trial->done || trial->failure)
        return;
    if (eeAllowEagerCode() != trial->optimized || Cpu != &recCpu)
    {
        trial->failure = "engine-mode-changed";
        return;
    }
    using Event = Ps2EagerAbFrames::Event;
    switch (trial->frames.Add(frame, tick))
    {
    case Event::None: break;
    case Event::FirstVblank: trial->warm = eager_ab_endpoint(tick); break;
    case Event::SampleStart: trial->first = eager_ab_endpoint(tick); break;
    case Event::SampleEnd:
        trial->last = eager_ab_endpoint(tick);
        if (const auto* ram = eeMem; ram)
            trial->digest = {cpuRegs.cycle, cpuRegs.pc, static_cast<uint32_t>(crc32(0, ram->Main, Ps2MemSize::ExposedRam))};
        else
            trial->failure = "missing-ee-ram";
        trial->done = true;
        break;
    case Event::Discontinuity:
        trial->failure = "vblank-discontinuity";
        break;
    }
}

bool eager_ab_report(const EagerAbTrial& trial)
{
    const auto& first = trial.first;
    const auto& last = trial.last;
    if (!trial.done || trial.failure || !first.tick || last.tick <= first.tick || first.nominal_hz <= 0 ||
        first.nominal_hz != last.nominal_hz || last.presents < first.presents ||
        last.gs_headless < first.gs_headless || last.updates < first.updates || last.blits < first.blits)
        return false;
    const auto [p50, p95, p99, max] = trial.frames.Quantiles();
    const char* phase = trial.optimized ? "eager" : "exact";
    const double ms = Common::Timer::ConvertValueToMilliseconds(last.tick - first.tick);
    std::fprintf(stderr,
        "IZPS2_EAGER_AB_WINDOW mode=%s vblanks=%u wall_ms=%.3f nominal_hz=%.3f "
        "wall_start_tick=%llu wall_end_tick=%llu tick_hz=%llu presents=%llu gs_headless=%llu "
        "gs_updates=%llu framebuffer_blits=%llu vblank_p50_ms=%.3f vblank_p95_ms=%.3f "
        "vblank_p99_ms=%.3f vblank_max_ms=%.3f ee_cycle=%llu ee_pc=%08x ram_crc=%08x\n",
        phase, Ps2EagerAbFrames::SampleIntervals, ms, first.nominal_hz,
        static_cast<unsigned long long>(first.tick), static_cast<unsigned long long>(last.tick),
        static_cast<unsigned long long>(iz_tick_frequency()),
        static_cast<unsigned long long>(last.presents - first.presents),
        static_cast<unsigned long long>(last.gs_headless - first.gs_headless),
        static_cast<unsigned long long>(last.updates - first.updates),
        static_cast<unsigned long long>(last.blits - first.blits),
        Common::Timer::ConvertValueToMilliseconds(p50), Common::Timer::ConvertValueToMilliseconds(p95),
        Common::Timer::ConvertValueToMilliseconds(p99), Common::Timer::ConvertValueToMilliseconds(max),
        static_cast<unsigned long long>(trial.digest.ee_cycle), trial.digest.ee_pc, trial.digest.ram_crc);
    report_session_timing(trial.optimized ? "ab_eager_warm" : "ab_exact_warm", 0, trial.warm.timing, first.timing);
    report_session_timing(trial.optimized ? "ab_eager_sample" : "ab_exact_sample", 0, first.timing, last.timing);
    return true;
}
