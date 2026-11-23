// Invoked once per startup, after the final automatic or explicit state load.
extern uint32_t recEeWarmBatch(std::span<const ps2::ee_warm::Entry>);
extern void recEeStartWarmProfile(uint32_t, uint32_t);
extern size_t recEeFinishWarmProfile(const std::string&, std::span<uint8_t>);

static void warm_ee_profile(const std::string& key, uint32_t disc, uint32_t elf)
{
    std::array<uint8_t, ps2::ee_warm::MaxBytes> bytes;
    const auto length = ps2::ee_warm::ReadSdProfile(key, bytes);
    if (!length || quick_exit_requested())
        return;

    unsigned admitted = 0;
    const auto started = hbxl_svc_tick();
    // IOP DMA can write EE RAM even while the EE has returned from dispatch.
    // Acquire the existing emulation hold only after the SD read finishes.
    const auto result = [&] {
        Ps2NativeIopInputHold hold;
        return ps2::ee_warm::Warm(std::span(bytes).first(length), disc, elf,
            {eeMem->Main, Ps2MemSize::ExposedRam}, [&](std::span<const ps2::ee_warm::Entry> entries) {
                if (quick_exit_requested())
                    return false;
                const auto consumed = recEeWarmBatch(entries);
                admitted += consumed;
                return consumed == entries.size();
            });
    }();
    std::fprintf(stderr, "PS2_EE_WARM valid=%u inputBytes=%zu matched=%u stale=%u admitted=%u ticks=%llu\n",
        unsigned(result.valid), length, result.usable, result.stale, admitted,
        static_cast<unsigned long long>(hbxl_svc_tick() - started));
}

static void store_ee_profile(const std::string& key)
{
    std::array<uint8_t, ps2::ee_warm::MaxBytes> bytes;
    const auto length = recEeFinishWarmProfile(key, bytes);
    if (length)
    {
        const bool stored = ps2::ee_warm::WriteSdProfile(key, std::span(bytes).first(length));
        std::fprintf(stderr, "PS2_EE_PROFILE stored=%u bytes=%zu\n", unsigned(stored), length);
    }
}
