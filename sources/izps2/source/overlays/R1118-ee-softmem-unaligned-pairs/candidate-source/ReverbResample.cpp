// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/GSVector.h"
#include "SPU2/defs.h"

MULTI_ISA_UNSHARED_START

static constexpr u32 NUM_TAPS = 39;
// 39 tap filter, the 0's could be optimized out
static constexpr std::array<s16, 48> filter_down_coefs alignas(32) = {
	-1,
	0,
	2,
	0,
	-10,
	0,
	35,
	0,
	-103,
	0,
	266,
	0,
	-616,
	0,
	1332,
	0,
	-2960,
	0,
	10246,
	16384,
	10246,
	0,
	-2960,
	0,
	1332,
	0,
	-616,
	0,
	266,
	0,
	-103,
	0,
	35,
	0,
	-10,
	0,
	2,
	0,
	-1,
};

static constexpr std::array<s16, 48> make_up_coefs()
{
	std::array<s16, 48> ret = {};

	for (u32 i = 0; i < NUM_TAPS; i++)
	{
		ret[i] = static_cast<s16>(std::clamp<s32>(filter_down_coefs[i] * 2, INT16_MIN, INT16_MAX));
	}

	return ret;
}

static constexpr std::array<s16, 48> filter_up_coefs alignas(32) = make_up_coefs();

s32 __forceinline ReverbDownsample_reference(V_Core& core, bool right)
{
	int index = (core.RevbSampleBufPos - NUM_TAPS) & 63;
	s32 out = 0;

	for (u32 i = 0; i < NUM_TAPS; i++)
	{
		out += core.RevbDownBuf[right][index + i] * filter_down_coefs[i];
	}

	out >>= 15;

	return clamp_mix(out);
}

#if _M_SSE >= 0x501
s32 __forceinline ReverbDownsample_avx(V_Core& core, bool right)
{
	int index = (core.RevbSampleBufPos - NUM_TAPS) & 63;

	auto c = GSVector8i::load<true>(&filter_down_coefs[0]);
	auto s = GSVector8i::load<false>(&core.RevbDownBuf[right][index]);
	auto acc = s.mul16hrs(c);

	c = GSVector8i::load<true>(&filter_down_coefs[16]);
	s = GSVector8i::load<false>(&core.RevbDownBuf[right][index + 16]);
	acc = acc.adds16(s.mul16hrs(c));

	c = GSVector8i::load<true>(&filter_down_coefs[32]);
	s = GSVector8i::load<false>(&core.RevbDownBuf[right][index + 32]);
	acc = acc.adds16(s.mul16hrs(c));

	acc = acc.adds16(acc.ba());

	acc = acc.hadds16(acc);
	acc = acc.hadds16(acc);
	acc = acc.hadds16(acc);

	return acc.I16[0];
}
#endif

static __forceinline GSVector4i MulReverbTaps(const GSVector4i& samples, const GSVector4i& taps)
{
#if defined(__aarch64__)
	// Rounded doubling multiply differs from mul16hrs only for
	// INT16_MIN times INT16_MIN. Neither table has that coefficient.
	const auto valid = [](s16 tap) { return tap != INT16_MIN; };
	static_assert(std::all_of(filter_down_coefs.begin(), filter_down_coefs.end(), valid));
	static_assert(std::all_of(filter_up_coefs.begin(), filter_up_coefs.end(), valid));
	return GSVector4i(vreinterpretq_s32_s16(vqrdmulhq_s16(
		vreinterpretq_s16_s32(samples.v4s), vreinterpretq_s16_s32(taps.v4s))));
#else
	return samples.mul16hrs(taps);
#endif
}

static __forceinline s16 ReduceReverbTaps(GSVector4i acc)
{
#if defined(__aarch64__)
	// Lane zero follows the original pairwise saturating tree. Other
	// lanes are used only at the aligned pair, group and half starts.
	auto lanes = vreinterpretq_s16_s32(acc.v4s);
	lanes = vqaddq_s16(lanes, vextq_s16(lanes, lanes, 1));
	lanes = vqaddq_s16(lanes, vextq_s16(lanes, lanes, 2));
	lanes = vqaddq_s16(lanes, vextq_s16(lanes, lanes, 4));
	return vgetq_lane_s16(lanes, 0);
#else
	acc = acc.hadds16(acc);
	acc = acc.hadds16(acc);
	acc = acc.hadds16(acc);
	return acc.I16[0];
#endif
}

s32 __forceinline ReverbDownsample_sse(V_Core& core, bool right)
{
	int index = (core.RevbSampleBufPos - NUM_TAPS) & 63;

	auto c = GSVector4i::load<true>(&filter_down_coefs[0]);
	auto s = GSVector4i::load<false>(&core.RevbDownBuf[right][index]);
	auto acc = MulReverbTaps(s, c);

	c = GSVector4i::load<true>(&filter_down_coefs[8]);
	s = GSVector4i::load<false>(&core.RevbDownBuf[right][index + 8]);
	acc = acc.adds16(MulReverbTaps(s, c));

	c = GSVector4i::load<true>(&filter_down_coefs[16]);
	s = GSVector4i::load<false>(&core.RevbDownBuf[right][index + 16]);
	acc = acc.adds16(MulReverbTaps(s, c));

	c = GSVector4i::load<true>(&filter_down_coefs[24]);
	s = GSVector4i::load<false>(&core.RevbDownBuf[right][index + 24]);
	acc = acc.adds16(MulReverbTaps(s, c));

	c = GSVector4i::load<true>(&filter_down_coefs[32]);
	s = GSVector4i::load<false>(&core.RevbDownBuf[right][index + 32]);
	acc = acc.adds16(MulReverbTaps(s, c));

	return ReduceReverbTaps(acc);
}

s32 ReverbDownsample(V_Core& core, bool right)
{
#if _M_SSE >= 0x501
	return ReverbDownsample_avx(core, right);
#else
	return ReverbDownsample_sse(core, right);
#endif
}

StereoOut32 __forceinline ReverbUpsample_reference(V_Core& core)
{
	int index = (core.RevbSampleBufPos - NUM_TAPS) & 63;
	s32 l = 0, r = 0;

	for (u32 i = 0; i < NUM_TAPS; i++)
	{
		l += core.RevbUpBuf[0][index + i] * filter_up_coefs[i];
		r += core.RevbUpBuf[1][index + i] * filter_up_coefs[i];
	}

	l >>= 15;
	r >>= 15;

	return {clamp_mix(l), clamp_mix(r)};
}

#if _M_SSE >= 0x501
StereoOut32 __forceinline ReverbUpsample_avx(V_Core& core)
{
	int index = (core.RevbSampleBufPos - NUM_TAPS) & 63;

	auto c = GSVector8i::load<true>(&filter_up_coefs[0]);
	auto l = GSVector8i::load<false>(&core.RevbUpBuf[0][index]);
	auto r = GSVector8i::load<false>(&core.RevbUpBuf[1][index]);

	auto lacc = l.mul16hrs(c);
	auto racc = r.mul16hrs(c);

	c = GSVector8i::load<true>(&filter_up_coefs[16]);
	l = GSVector8i::load<false>(&core.RevbUpBuf[0][index + 16]);
	r = GSVector8i::load<false>(&core.RevbUpBuf[1][index + 16]);
	lacc = lacc.adds16(l.mul16hrs(c));
	racc = racc.adds16(r.mul16hrs(c));

	c = GSVector8i::load<true>(&filter_up_coefs[32]);
	l = GSVector8i::load<false>(&core.RevbUpBuf[0][index + 32]);
	r = GSVector8i::load<false>(&core.RevbUpBuf[1][index + 32]);
	lacc = lacc.adds16(l.mul16hrs(c));
	racc = racc.adds16(r.mul16hrs(c));

	lacc = lacc.adds16(lacc.ba());
	racc = racc.adds16(racc.ba());

	lacc = lacc.hadds16(lacc);
	lacc = lacc.hadds16(lacc);
	lacc = lacc.hadds16(lacc);

	racc = racc.hadds16(racc);
	racc = racc.hadds16(racc);
	racc = racc.hadds16(racc);

	return {lacc.I16[0], racc.I16[0]};
}
#endif

StereoOut32 __forceinline ReverbUpsample_sse(V_Core& core)
{
	int index = (core.RevbSampleBufPos - NUM_TAPS) & 63;

	auto c = GSVector4i::load<true>(&filter_up_coefs[0]);
	auto l = GSVector4i::load<false>(&core.RevbUpBuf[0][index]);
	auto r = GSVector4i::load<false>(&core.RevbUpBuf[1][index]);

	auto lacc = MulReverbTaps(l, c);
	auto racc = MulReverbTaps(r, c);

	c = GSVector4i::load<true>(&filter_up_coefs[8]);
	l = GSVector4i::load<false>(&core.RevbUpBuf[0][index + 8]);
	r = GSVector4i::load<false>(&core.RevbUpBuf[1][index + 8]);
	lacc = lacc.adds16(MulReverbTaps(l, c));
	racc = racc.adds16(MulReverbTaps(r, c));

	c = GSVector4i::load<true>(&filter_up_coefs[16]);
	l = GSVector4i::load<false>(&core.RevbUpBuf[0][index + 16]);
	r = GSVector4i::load<false>(&core.RevbUpBuf[1][index + 16]);
	lacc = lacc.adds16(MulReverbTaps(l, c));
	racc = racc.adds16(MulReverbTaps(r, c));

	c = GSVector4i::load<true>(&filter_up_coefs[24]);
	l = GSVector4i::load<false>(&core.RevbUpBuf[0][index + 24]);
	r = GSVector4i::load<false>(&core.RevbUpBuf[1][index + 24]);
	lacc = lacc.adds16(MulReverbTaps(l, c));
	racc = racc.adds16(MulReverbTaps(r, c));

	c = GSVector4i::load<true>(&filter_up_coefs[32]);
	l = GSVector4i::load<false>(&core.RevbUpBuf[0][index + 32]);
	r = GSVector4i::load<false>(&core.RevbUpBuf[1][index + 32]);
	lacc = lacc.adds16(MulReverbTaps(l, c));
	racc = racc.adds16(MulReverbTaps(r, c));

	return {ReduceReverbTaps(lacc), ReduceReverbTaps(racc)};
}

StereoOut32 ReverbUpsample(V_Core& core)
{
#if _M_SSE >= 0x501
	return ReverbUpsample_avx(core);
#else
	return ReverbUpsample_sse(core);
#endif
}

MULTI_ISA_UNSHARED_END
