#include "../../native/split-windows.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    IzSplitWindows windows = { .visible = SPLIT_BOTH, .focus = SPLIT_RIGHT };
    assert(split_sound_next(windows.visible, IZ_SPLIT_SOUND_RIGHT) == IZ_SPLIT_SOUND_LEFT);
    split_window_exit(&windows, SPLIT_RIGHT);
    assert(windows.visible == SPLIT_LEFT && windows.focus == SPLIT_LEFT);
    assert(windows.retiring == SPLIT_RIGHT);
    assert(split_sound_after_exit(windows.visible, IZ_SPLIT_SOUND_RIGHT) == IZ_SPLIT_SOUND_LEFT);
    assert(split_sound_after_exit(windows.visible, IZ_SPLIT_SOUND_NONE) == IZ_SPLIT_SOUND_NONE);
    assert(split_sound_next(windows.visible, IZ_SPLIT_SOUND_NONE) == IZ_SPLIT_SOUND_LEFT);
    split_window_focus(&windows);
    assert(windows.focus == SPLIT_LEFT);

    windows = (IzSplitWindows){ .visible = SPLIT_BOTH, .focus = SPLIT_RIGHT };
    split_window_focus(&windows);
    assert(windows.focus == SPLIT_LEFT);
    split_window_exit(&windows, SPLIT_LEFT);
    assert(windows.visible == SPLIT_RIGHT && windows.focus == SPLIT_RIGHT);
    assert(split_sound_after_exit(windows.visible, IZ_SPLIT_SOUND_RIGHT) == IZ_SPLIT_SOUND_RIGHT);
    assert(split_sound_next(windows.visible, IZ_SPLIT_SOUND_RIGHT) == IZ_SPLIT_SOUND_NONE);
    split_window_exit(&windows, SPLIT_RIGHT);
    assert(!windows.visible && !windows.focus && windows.retiring == SPLIT_BOTH);
    assert(split_sound_after_exit(windows.visible, IZ_SPLIT_SOUND_RIGHT) == IZ_SPLIT_SOUND_NONE);
    puts("PASS: exit either pane, retain the survivor, move input and audio, preserve mute");
}
