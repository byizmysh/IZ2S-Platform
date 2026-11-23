#ifndef PS2_FRAME_LIMIT_H
#define PS2_FRAME_LIMIT_H

enum Ps2FrameLimit {
    PS2_FRAME_LIMIT_OFF,
    PS2_FRAME_LIMIT_30,
    PS2_FRAME_LIMIT_40,
    PS2_FRAME_LIMIT_50,
    PS2_FRAME_LIMIT_60,
    PS2_FRAME_LIMIT_COUNT
};

static inline unsigned ps2_frame_limit_hz(unsigned setting)
{
    static const unsigned rates[] = {0, 30, 40, 50, 60};
    return setting < PS2_FRAME_LIMIT_COUNT ? rates[setting] : 0;
}

#endif
