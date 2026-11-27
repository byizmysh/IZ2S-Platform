#ifndef IZ_SPLIT_WINDOWS_H
#define IZ_SPLIT_WINDOWS_H

#include "split-audio.h"

enum {
    SPLIT_LEFT = 1u << 0,
    SPLIT_RIGHT = 1u << 1,
    SPLIT_BOTH = SPLIT_LEFT | SPLIT_RIGHT,
};

typedef struct {
    unsigned visible;
    unsigned retiring;
    unsigned focus;
} IzSplitWindows;

static inline void split_window_exit(IzSplitWindows *windows, unsigned pane)
{
    windows->retiring |= windows->visible & pane;
    windows->visible &= ~pane;
    if (windows->focus & pane)
        windows->focus = windows->visible;
}

static inline void split_window_focus(IzSplitWindows *windows)
{
    if (windows->visible == SPLIT_BOTH) windows->focus ^= SPLIT_BOTH;
}

static inline IzSplitSoundSource split_sound_after_exit(unsigned visible,
                                                        IzSplitSoundSource current)
{
    if (current == IZ_SPLIT_SOUND_NONE) return current;
    if (visible & (1u << (current - 1))) return current;
    if (visible & SPLIT_RIGHT) return IZ_SPLIT_SOUND_RIGHT;
    if (visible & SPLIT_LEFT) return IZ_SPLIT_SOUND_LEFT;
    return IZ_SPLIT_SOUND_NONE;
}

static inline IzSplitSoundSource split_sound_next(unsigned visible,
                                                  IzSplitSoundSource current)
{
    if (current == IZ_SPLIT_SOUND_NONE)
        return split_sound_after_exit(visible, IZ_SPLIT_SOUND_RIGHT);
    if (current == IZ_SPLIT_SOUND_RIGHT && (visible & SPLIT_LEFT))
        return IZ_SPLIT_SOUND_LEFT;
    return IZ_SPLIT_SOUND_NONE;
}

#endif
