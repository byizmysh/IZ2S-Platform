// SPDX-License-Identifier: GPL-3.0+
#pragma once

namespace Spu2Horizon
{
constexpr unsigned Batch = 32;
constexpr unsigned ClockPerSample = 768;
constexpr u32 RamWords = 0x100000;

constexpr bool VoiceCanReach(u32 base, u32 target, unsigned samples)
{
    // Each call to DecodeSamples advances NAX at most twice. It may jump
    // to LSA, including a header address captured from the same reachable
    // interval. Include aligned header reads and reject wrap conservatively.
    if (base >= RamWords || base + 2 * samples >= RamWords)
        return true;
    return target >= (base & ~7u) && target <= base + 2 * samples;
}

enum class Gate : unsigned { Allowed, PlayMode, Key, Dma, Addr, Reverb, Nax, Loop, Count };

inline Gate Check(unsigned samples)
{
    // Output in normal and PCM-clone modes follows the same input path.
    if (PlayMode < 0 || PlayMode > 1 || Arcade::IsActive())
        return Gate::PlayMode;

    for (const auto& core : Cores)
    {
        if (core.KeyOn)
            return Gate::Key;
        if (core.DMAICounter || core.InputDataLeft >= 0x100 || core.InputDataTransferred ||
            core.AdmaInProgress || (core.AutoDMACtrl & 3))
            return Gate::Dma;
    }

    for (unsigned i = 0; i < 2; ++i)
    {
        const auto& irq = Cores[i];
        if (!irq.IRQEnable || (Spdif.Info & (4u << i)))
            continue;

        const u32 address = irq.IRQA;
        if (address < SPU2_DYN_MEMLINE || address >= RamWords)
            return Gate::Addr;

        for (const auto& core : Cores)
        {
            if (core.FxEnable && address >= core.EffectsStartA && address <= core.EffectsEndA)
                return Gate::Reverb;
            for (const auto& voice : core.Voices)
            {
                if (VoiceCanReach(voice.NextA, address, samples))
                    return Gate::Nax;
                if (VoiceCanReach(voice.LoopStartA, address, samples))
                    return Gate::Loop;
            }
        }
    }
    return Gate::Allowed;
}
}
