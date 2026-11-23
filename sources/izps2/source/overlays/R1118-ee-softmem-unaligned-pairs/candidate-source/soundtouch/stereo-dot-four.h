#pragma once

#include <arm_neon.h>

// Four consecutive stereo candidate starts. Each result follows the reduction
// tree emitted for the pinned scalar API by the RUN25 SoundTouch build.
inline float32x4_t StereoDotFour(const float* input, const float* reference, int length)
{
    float32x4_t head[4] = {};
    float32x4_t tail[4] = {};
    for (int i = 0; i < length; i += 8)
    {
        const auto r0 = vld1q_f32(reference + i);
        const auto r1 = vld1q_f32(reference + i + 4);
        const auto x0 = vld1q_f32(input + i);
        const auto x4 = vld1q_f32(input + i + 4);
        const auto x8 = vld1q_f32(input + i + 8);
        const auto x10 = vld1q_f32(input + i + 10);
        const auto x2 = vextq_f32(x0, x4, 2);
        const auto x6 = vextq_f32(x4, x8, 2);
        head[0] = vfmaq_f32(head[0], r0, x0);
        head[1] = vfmaq_f32(head[1], r0, x2);
        head[2] = vfmaq_f32(head[2], r0, x4);
        head[3] = vfmaq_f32(head[3], r0, x6);
        tail[0] = vfmaq_f32(tail[0], r1, x4);
        tail[1] = vfmaq_f32(tail[1], r1, x6);
        tail[2] = vfmaq_f32(tail[2], r1, x8);
        tail[3] = vfmaq_f32(tail[3], r1, x10);
    }

    const auto pair01 = vpaddq_f32(vaddq_f32(tail[0], head[0]), vaddq_f32(tail[1], head[1]));
    const auto pair23 = vpaddq_f32(vaddq_f32(tail[2], head[2]), vaddq_f32(tail[3], head[3]));
    return vpaddq_f32(pair01, pair23);
}

// Six input vectors remain live between steps. Rotate their logical
// indices across five steps so the loop can keep the physical registers.
template<int Base>
inline void StereoDotEightStep(const float* input, const float* reference,
    float32x4_t* inputVectors, float32x4_t* head, float32x4_t* tail)
{
    const auto r0 = vld1q_f32(reference);
    const auto r1 = vld1q_f32(reference + 4);
    for (int i = 6; i < 10; ++i)
        inputVectors[(Base + i) % 10] = vld1q_f32(input + 2 * i);
    for (int i = 0; i < 8; ++i)
    {
        head[i] = vfmaq_f32(head[i], r0, inputVectors[(Base + i) % 10]);
        tail[i] = vfmaq_f32(tail[i], r1, inputVectors[(Base + i + 2) % 10]);
    }
}

// The eight independent reductions retain the pinned float FMA order.
inline void StereoDotEight(const float* input, const float* reference, int length, float* output)
{
    float32x4_t head[8] = {};
    float32x4_t tail[8] = {};
    float32x4_t inputVectors[10];
    for (int i = 0; i < 6; ++i)
        inputVectors[i] = vld1q_f32(input + 2 * i);

    int remaining = length;
    for (; remaining >= 40; remaining -= 40, input += 40, reference += 40)
    {
        StereoDotEightStep<0>(input, reference, inputVectors, head, tail);
        StereoDotEightStep<4>(input + 8, reference + 8, inputVectors, head, tail);
        StereoDotEightStep<8>(input + 16, reference + 16, inputVectors, head, tail);
        StereoDotEightStep<2>(input + 24, reference + 24, inputVectors, head, tail);
        StereoDotEightStep<6>(input + 32, reference + 32, inputVectors, head, tail);
    }
    if (remaining >= 8)
        StereoDotEightStep<0>(input, reference, inputVectors, head, tail);
    if (remaining >= 16)
        StereoDotEightStep<4>(input + 8, reference + 8, inputVectors, head, tail);
    if (remaining >= 24)
        StereoDotEightStep<8>(input + 16, reference + 16, inputVectors, head, tail);
    if (remaining >= 32)
        StereoDotEightStep<2>(input + 24, reference + 24, inputVectors, head, tail);

    for (int i = 0; i < 8; i += 4)
    {
        const auto pair01 = vpaddq_f32(vaddq_f32(tail[i], head[i]), vaddq_f32(tail[i + 1], head[i + 1]));
        const auto pair23 = vpaddq_f32(vaddq_f32(tail[i + 2], head[i + 2]), vaddq_f32(tail[i + 3], head[i + 3]));
        vst1q_f32(output + i, vpaddq_f32(pair01, pair23));
    }
}
