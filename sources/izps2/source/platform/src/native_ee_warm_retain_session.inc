extern uint32_t recEeWarmBatch(std::span<const ps2::ee_warm::Entry>);
extern void recEeStartWarmProfile(uint32_t, uint32_t);
extern size_t recEeFinishWarmProfile(const std::string&, std::span<uint8_t>);

struct PreviousEeProfile {
    std::vector<uint8_t> bytes;
    std::vector<uint32_t> matched;
};

static PreviousEeProfile warm_ee_profile(const std::string& key, uint32_t disc, uint32_t elf)
{
    PreviousEeProfile profile;
    profile.bytes.resize(ps2::ee_warm::MaxBytes);
    profile.bytes.resize(ps2::ee_warm::ReadSdProfile(key, profile.bytes));
    if (profile.bytes.empty() || quick_exit_requested())
        return {};

    profile.matched.reserve((profile.bytes.size() - std::min<size_t>(profile.bytes.size(),
                            ps2::ee_warm::HeaderBytes)) / ps2::ee_warm::RecordBytes);
    unsigned admitted = 0;
    const auto started = hbxl_svc_tick();
    const auto result = [&] {
        Ps2NativeIopInputHold hold;
        return ps2::ee_warm::Warm(profile.bytes, disc, elf,
            {eeMem->Main, Ps2MemSize::ExposedRam}, [&](std::span<const ps2::ee_warm::Entry> entries) {
                if (quick_exit_requested())
                    return false;
                for (const auto& entry : entries)
                    profile.matched.push_back(entry.pc);
                admitted += recEeWarmBatch(entries);
                // Individual entries can be ineligible even when the RAM
                // bytes match. Continue to the next batch in that case.
                return true;
            });
    }();
    std::fprintf(stderr, "PS2_EE_WARM valid=%u inputBytes=%zu matched=%u stale=%u admitted=%u ticks=%llu\n",
        unsigned(result.valid), profile.bytes.size(), result.usable, result.stale, admitted,
        static_cast<unsigned long long>(hbxl_svc_tick() - started));
    if (!result.valid)
        return {};
    std::sort(profile.matched.begin(), profile.matched.end());
    return profile;
}

static void store_ee_profile(const std::string& key, const PreviousEeProfile& previous)
{
    std::vector<uint8_t> observed(ps2::ee_warm::MaxBytes);
    observed.resize(recEeFinishWarmProfile(key, observed));
    std::vector<uint8_t> merged;
    if (ps2::ee_warm::MergeProfiles(previous.bytes, previous.matched, observed, merged))
    {
        const bool stored = ps2::ee_warm::WriteSdProfile(key, merged);
        std::fprintf(stderr, "PS2_EE_PROFILE stored=%u bytes=%zu previousBytes=%zu observedBytes=%zu\n",
            unsigned(stored), merged.size(), previous.bytes.size(), observed.size());
    }
}
