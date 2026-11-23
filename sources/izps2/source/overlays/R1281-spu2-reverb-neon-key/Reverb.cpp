// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "SPU2/defs.h"
#include "reverb-tap-cursor.h"
#include "GS/GSVector.h"

#include "common/Console.h"

#include <cstring>

void V_Core::AnalyzeReverbPreset()
{
	Console.WriteLn("Reverb Parameter Update for Core %d:", Index);
	Console.WriteLn("----------------------------------------------------------");

	Console.WriteLn("    IN_COEF_L, IN_COEF_R       0x%08x, 0x%08x", Revb.IN_COEF_L, Revb.IN_COEF_R);
	Console.WriteLn("    APF1_SIZE, APF2_SIZE       0x%08x, 0x%08x", Revb.APF1_SIZE, Revb.APF2_SIZE);
	Console.WriteLn("    APF1_VOL, APF2_VOL         0x%08x, 0x%08x", Revb.APF1_VOL, Revb.APF2_VOL);

	Console.WriteLn("    COMB1_VOL                  0x%08x", Revb.COMB1_VOL);
	Console.WriteLn("    COMB2_VOL                  0x%08x", Revb.COMB2_VOL);
	Console.WriteLn("    COMB3_VOL                  0x%08x", Revb.COMB3_VOL);
	Console.WriteLn("    COMB4_VOL                  0x%08x", Revb.COMB4_VOL);

	Console.WriteLn("    COMB1_L_SRC, COMB1_R_SRC   0x%08x, 0x%08x", Revb.COMB1_L_SRC, Revb.COMB1_R_SRC);
	Console.WriteLn("    COMB2_L_SRC, COMB2_R_SRC   0x%08x, 0x%08x", Revb.COMB2_L_SRC, Revb.COMB2_R_SRC);
	Console.WriteLn("    COMB3_L_SRC, COMB3_R_SRC   0x%08x, 0x%08x", Revb.COMB3_L_SRC, Revb.COMB3_R_SRC);
	Console.WriteLn("    COMB4_L_SRC, COMB4_R_SRC   0x%08x, 0x%08x", Revb.COMB4_L_SRC, Revb.COMB4_R_SRC);

	Console.WriteLn("    SAME_L_SRC, SAME_R_SRC     0x%08x, 0x%08x", Revb.SAME_L_SRC, Revb.SAME_R_SRC);
	Console.WriteLn("    DIFF_L_SRC, DIFF_R_SRC     0x%08x, 0x%08x", Revb.DIFF_L_SRC, Revb.DIFF_R_SRC);
	Console.WriteLn("    SAME_L_DST, SAME_R_DST     0x%08x, 0x%08x", Revb.SAME_L_DST, Revb.SAME_R_DST);
	Console.WriteLn("    DIFF_L_DST, DIFF_R_DST     0x%08x, 0x%08x", Revb.DIFF_L_DST, Revb.DIFF_R_DST);
	Console.WriteLn("    IIR_VOL, WALL_VOL          0x%08x, 0x%08x", Revb.IIR_VOL, Revb.WALL_VOL);

	Console.WriteLn("    APF1_L_DST                 0x%08x", Revb.APF1_L_DST);
	Console.WriteLn("    APF1_R_DST                 0x%08x", Revb.APF1_R_DST);
	Console.WriteLn("    APF2_L_DST                 0x%08x", Revb.APF2_L_DST);
	Console.WriteLn("    APF2_R_DST                 0x%08x", Revb.APF2_R_DST);

	Console.WriteLn("    EffectStartA               0x%x", EffectsStartA & 0x3f'ffff);
	Console.WriteLn("    EffectsEndA                0x%x", EffectsEndA & 0x3f'ffff);
	Console.WriteLn("----------------------------------------------------------");
}

namespace
{
bool SameRegisters(const V_Reverb& saved, const V_Reverb& current)
{
#if defined(__aarch64__)
	const auto* previous = reinterpret_cast<const unsigned char*>(&saved);
	const auto* live = reinterpret_cast<const unsigned char*>(&current);
	constexpr std::size_t bytes = sizeof(V_Reverb);
	uint8x16_t difference = vdupq_n_u8(0);
	for (std::size_t offset = 0; offset + 16 <= bytes; offset += 16)
	{
		const auto left = vld1q_u8(previous + offset);
		const auto right = vld1q_u8(live + offset);
		difference = vorrq_u8(difference, veorq_u8(left, right));
	}
	if constexpr (bytes % 16 != 0)
	{
		// Overlap the last vector so neither read runs past the registers.
		const auto left = vld1q_u8(previous + bytes - 16);
		const auto right = vld1q_u8(live + bytes - 16);
		difference = vorrq_u8(difference, veorq_u8(left, right));
	}
	return vmaxvq_u8(difference) == 0;
#else
	return std::memcmp(&saved, &current, sizeof(current)) == 0;
#endif
}

// The cursor is derived from the live reverb registers and sample counter.
// Both cursor lanes are invalidated by an address-register change. Sample
// discontinuities (including savestate loads) rebase the selected lane.
struct ReverbTapState
{
	Spu2Reverb::TapAddresses Resolve(const V_Reverb& registers, u32 start, u32 period, u32 position, bool right)
	{
		if (period != m_period || !SameRegisters(m_registers, registers))
		{
			m_period = period;
			m_modulo.SetPeriod(period);
			std::memcpy(&m_registers, &registers, sizeof(registers));
			m_channel[0].Invalidate();
			m_channel[1].Invalidate();
		}

		auto& cursor = m_channel[right];
		if (cursor.CanAdvance(position))
			return cursor.Addresses<true>(start, period);

		const u32 same_dst = right ? registers.SAME_R_DST : registers.SAME_L_DST;
		const u32 diff_dst = right ? registers.DIFF_R_DST : registers.DIFF_L_DST;
		const u32 apf1_dst = right ? registers.APF1_R_DST : registers.APF1_L_DST;
		const u32 apf2_dst = right ? registers.APF2_R_DST : registers.APF2_L_DST;
		const Spu2Reverb::TapAddresses offsets{
			right ? registers.SAME_R_SRC : registers.SAME_L_SRC, same_dst, same_dst - 1,
			right ? registers.DIFF_L_SRC : registers.DIFF_R_SRC, diff_dst, diff_dst - 1,
			right ? registers.COMB1_R_SRC : registers.COMB1_L_SRC,
			right ? registers.COMB2_R_SRC : registers.COMB2_L_SRC,
			right ? registers.COMB3_R_SRC : registers.COMB3_L_SRC,
			right ? registers.COMB4_R_SRC : registers.COMB4_L_SRC,
			apf1_dst - registers.APF1_SIZE, apf1_dst,
			apf2_dst - registers.APF2_SIZE, apf2_dst,
		};
		cursor.Rebase(position, m_modulo, offsets);
		return cursor.Addresses<false>(start, period);
	}

private:
	V_Reverb m_registers{};
	Spu2Reverb::TapCursor m_channel[2];
	Spu2Reverb::Reciprocal m_modulo;
	u32 m_period = 0;
};

ReverbTapState s_reverb_taps[2];
} // namespace

StereoOut32 V_Core::DoReverb(StereoOut32 Input)
{
	if (EffectsStartA >= EffectsEndA)
	{
		return StereoOut32::Empty;
	}

	Input = clamp_mix(Input);

	RevbDownBuf[0][RevbSampleBufPos] = Input.Left;
	RevbDownBuf[1][RevbSampleBufPos] = Input.Right;
	RevbDownBuf[0][RevbSampleBufPos | 64] = Input.Left;
	RevbDownBuf[1][RevbSampleBufPos | 64] = Input.Right;

	bool R = Cycles & 1;

	const u32 start = EffectsStartA & 0x3f'ffff;
	const u32 end = (EffectsEndA & 0x3f'ffff) | 0xffff;
	const auto [same_src, same_dst, same_prv, diff_src, diff_dst, diff_prv,
		comb1_src, comb2_src, comb3_src, comb4_src, apf1_src, apf1_dst, apf2_src, apf2_dst,
		unused0, unused1] = s_reverb_taps[Index & 1].Resolve(Revb, start, end - start + 1, Cycles >> 1, R);

	// -----------------------------------------
	//          Optimized IRQ Testing !
	// -----------------------------------------

	// This test is enhanced by using the reverb effects area begin/end test as a
	// shortcut, since all buffer addresses are within that area.  If the IRQA isn't
	// within that zone then the "bulk" of the test is skipped, so this should only
	// be a slowdown on a few evil games.

	for (int i = 0; i < 2; i++)
	{
		if (FxEnable && Cores[i].IRQEnable && ((Cores[i].IRQA >= EffectsStartA) && (Cores[i].IRQA <= EffectsEndA)))
		{
			if ((Cores[i].IRQA == same_src) || (Cores[i].IRQA == diff_src) ||
				(Cores[i].IRQA == same_dst) || (Cores[i].IRQA == diff_dst) ||
				(Cores[i].IRQA == same_prv) || (Cores[i].IRQA == diff_prv) ||

				(Cores[i].IRQA == comb1_src) || (Cores[i].IRQA == comb2_src) ||
				(Cores[i].IRQA == comb3_src) || (Cores[i].IRQA == comb4_src) ||

				(Cores[i].IRQA == apf1_dst) || (Cores[i].IRQA == apf1_src) ||
				(Cores[i].IRQA == apf2_dst) || (Cores[i].IRQA == apf2_src))
			{
				//printf("Core %d IRQ Called (Reverb). IRQA = %x\n",i,addr);
				SetIrqCall(i);
			}
		}
	}

	// Reverb algorithm pretty much directly ripped from http://drhell.web.fc2.com/ps1/
	// minus the 35 step FIR which just seems to break things.

	s32 in, same, diff, apf1, apf2, out;

#define MUL(x, y) ((x) * (y) >> 15)
	in = MUL(R ? Revb.IN_COEF_R : Revb.IN_COEF_L, ReverbDownsample(*this, R));

	same = MUL(Revb.IIR_VOL, in + MUL(Revb.WALL_VOL, _spu2mem[same_src]) - _spu2mem[same_prv]) + _spu2mem[same_prv];
	diff = MUL(Revb.IIR_VOL, in + MUL(Revb.WALL_VOL, _spu2mem[diff_src]) - _spu2mem[diff_prv]) + _spu2mem[diff_prv];

	out = MUL(Revb.COMB1_VOL, _spu2mem[comb1_src]) + MUL(Revb.COMB2_VOL, _spu2mem[comb2_src]) + MUL(Revb.COMB3_VOL, _spu2mem[comb3_src]) + MUL(Revb.COMB4_VOL, _spu2mem[comb4_src]);

	apf1 = out - MUL(Revb.APF1_VOL, _spu2mem[apf1_src]);
	out = _spu2mem[apf1_src] + MUL(Revb.APF1_VOL, apf1);
	apf2 = out - MUL(Revb.APF2_VOL, _spu2mem[apf2_src]);
	out = _spu2mem[apf2_src] + MUL(Revb.APF2_VOL, apf2);

	// According to no$psx the effects always run but don't always write back, see check in V_Core::Mix
	if (FxEnable)
	{
		_spu2mem[same_dst] = clamp_mix(same);
		_spu2mem[diff_dst] = clamp_mix(diff);
		_spu2mem[apf1_dst] = clamp_mix(apf1);
		_spu2mem[apf2_dst] = clamp_mix(apf2);
	}

	out = clamp_mix(out);

	RevbUpBuf[R][RevbSampleBufPos] = out;
	RevbUpBuf[!R][RevbSampleBufPos] = 0;

	RevbUpBuf[R][RevbSampleBufPos | 64] = out;
	RevbUpBuf[!R][RevbSampleBufPos | 64] = 0;

	RevbSampleBufPos = (RevbSampleBufPos + 1) & 63;

	return ReverbUpsample(*this);
}
