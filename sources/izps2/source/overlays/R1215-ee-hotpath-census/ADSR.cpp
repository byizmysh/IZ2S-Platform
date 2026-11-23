// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "SPU2/defs.h"

#include "common/Assertions.h"

static constexpr s32 ADSR_MAX_VOL = 0x7fff;

// Rate registers are left intact in the emulated state and in savestates.
struct EnvelopeRate
{
	u16 normal;
	u16 slow;
};

static constexpr auto s_adsr_rates = [] {
	std::array<EnvelopeRate, 32> rates{};
	for (unsigned shift = 0; shift < rates.size(); ++shift)
	{
		const unsigned counter = 0x8000 >> std::max(0, static_cast<int>(shift) - 11);
		rates[shift] = {static_cast<u16>(std::max(1u, counter)),
			static_cast<u16>(std::max(1u, counter >> 2))};
	}
	return rates;
}();

struct SlideRate
{
	EnvelopeRate counter;
	s16 increase;
	s16 decrease;
};

static constexpr auto s_slide_rates = [] {
	std::array<SlideRate, 128> rates{};
	for (unsigned index = 0; index < rates.size(); ++index)
	{
		const int step = index & 3;
		const int shift = index >> 2;
		const unsigned counter = 0x8000 >> std::max(0, shift - 11);
		const unsigned floor = step != 3 && shift != 31 ? 1 : 0;
		const int scale = 1 << std::max(0, 11 - shift);
		rates[index] = {{static_cast<u16>(std::max(floor, counter)),
			static_cast<u16>(std::max(floor, counter >> 2))},
			static_cast<s16>((7 - step) * scale), static_cast<s16>(~(7 - step) * scale)};
	}
	return rates;
}();


void V_ADSR::UpdateCache()
{
	CachedPhases[PHASE_ATTACK].Decr = false;
	CachedPhases[PHASE_ATTACK].Exp = AttackMode;
	CachedPhases[PHASE_ATTACK].Shift = AttackShift;
	CachedPhases[PHASE_ATTACK].Step = 7 - AttackStep;
	CachedPhases[PHASE_ATTACK].Target = ADSR_MAX_VOL;

	CachedPhases[PHASE_DECAY].Decr = true;
	CachedPhases[PHASE_DECAY].Exp = true;
	CachedPhases[PHASE_DECAY].Shift = DecayShift;
	CachedPhases[PHASE_DECAY].Step = -8;
	CachedPhases[PHASE_DECAY].Target = (SustainLevel + 1) << 11;

	CachedPhases[PHASE_SUSTAIN].Decr = SustainDir;
	CachedPhases[PHASE_SUSTAIN].Exp = SustainMode;
	CachedPhases[PHASE_SUSTAIN].Shift = SustainShift;
	CachedPhases[PHASE_SUSTAIN].Step = 7 - SustainStep;

	if (CachedPhases[PHASE_SUSTAIN].Decr)
		CachedPhases[PHASE_SUSTAIN].Step = ~CachedPhases[PHASE_SUSTAIN].Step;

	CachedPhases[PHASE_SUSTAIN].Target = 0;

	CachedPhases[PHASE_RELEASE].Decr = true;
	CachedPhases[PHASE_RELEASE].Exp = ReleaseMode;
	CachedPhases[PHASE_RELEASE].Shift = ReleaseShift;
	CachedPhases[PHASE_RELEASE].Step = -8;
	CachedPhases[PHASE_RELEASE].Target = 0;
}

bool V_ADSR::Calculate(int voiceidx)
{
	pxAssume(Phase != PHASE_STOPPED);

	auto& p = CachedPhases.at(Phase);

	// ADSR increments remain at least one, including the slow attack path.
	const auto& rate = s_adsr_rates[p.Shift];
	Counter += p.Exp && !p.Decr && Value > 0x6000 ? rate.slow : rate.normal;
	if (Counter >= 0x8000)
	{
		Counter = 0;
		s32 level_inc = p.Step * (1 << std::max(0, 11 - p.Shift));
		if (p.Exp && p.Decr)
			level_inc = static_cast<s16>((level_inc * Value) >> 15);
		Value = std::clamp<s32>(Value + level_inc, 0, INT16_MAX);
	}

	// Stay in sustain until key off or silence
	if (Phase == PHASE_SUSTAIN)
	{
		return Value != 0;
	}

	// Check if target is reached to advance phase
	if ((!p.Decr && Value >= p.Target) || (p.Decr && Value <= p.Target))
	{
		Phase++;
	}

	// All phases done, stop the voice
	if (Phase > PHASE_RELEASE)
	{
		return false;
	}

	return true;
}

void V_ADSR::Attack()
{
	Phase = PHASE_ATTACK;
	Counter = 0;
	Value = 0;
}

void V_ADSR::Release()
{
	if (Phase != PHASE_STOPPED)
	{
		Phase = PHASE_RELEASE;
		Counter = 0;
	}
}

void V_VolumeSlide::RegSet(u16 src)
{
	Reg_VOL = src;
	if (!Enable)
	{
		Value = SignExtend16(src << 1);
	}
}

void V_VolumeSlide::Update()
{
	if (!Enable)
		return;

	const auto& rate = s_slide_rates[Reg_VOL & 0x7f];
	Counter += Exp && !Decr && Value > 0x6000 ? rate.counter.slow : rate.counter.normal;
	if (Counter < 0x8000)
		return;

	Counter = 0;
	s32 level_inc = Decr ? rate.decrease : rate.increase;
	if (Exp && Decr)
	{
		level_inc = static_cast<s16>((level_inc * Value) >> 15);
		Value = std::clamp<s32>(Value + level_inc, 0, INT16_MAX);
	}
	else
	{
		if (Phase)
			level_inc = -level_inc;
		const s32 low = !Decr || Phase ? INT16_MIN : 0;
		const s32 high = Decr && Phase ? 0 : INT16_MAX;
		Value = std::clamp<s32>(Value + level_inc, low, high);
	}
}
