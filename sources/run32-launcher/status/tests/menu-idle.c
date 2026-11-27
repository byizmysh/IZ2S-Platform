#include "../menu-idle.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    enum { HZ = 60000, POLL = HZ / 60 };
    Iz2MenuIdle idle = {0};
    for (uint64_t tick = 0; tick < HZ * IZ2_MENU_IDLE_SECONDS; tick += POLL)
        assert(iz2_menu_draw(&idle, tick, HZ, true, false, false));

    unsigned draws = 0;
    const uint64_t quiet_at = HZ * IZ2_MENU_IDLE_SECONDS;
    for (uint64_t tick = quiet_at; tick < quiet_at + 10 * HZ; tick += POLL)
        draws += iz2_menu_draw(&idle, tick, HZ, true, false, false);
    assert(draws == 10);

    const uint64_t changed_at = quiet_at + 10 * HZ - POLL;
    assert(iz2_menu_draw(&idle, changed_at, HZ, true, false, true));
    assert(!iz2_menu_draw(&idle, changed_at + POLL, HZ, true, false, false));

    int32_t axes[4] = {0, -700, 0, 500};
    assert(!iz2_menu_input_active(0, 0, axes));
    const uint64_t wake_at = idle.last_draw + POLL;
    assert(!iz2_menu_draw(&idle, wake_at, HZ, true, false, false));
    assert(iz2_menu_draw(&idle, wake_at, HZ, true, iz2_menu_input_active(1, 0, axes), false));
    assert(iz2_menu_draw(&idle, wake_at + POLL, HZ, true, false, false));

    assert(iz2_menu_input_active(0, 1, axes));
    axes[1] = -IZ2_MENU_STICK_WAKE - 1;
    assert(iz2_menu_input_active(0, 0, axes));
    assert(iz2_menu_draw(&idle, wake_at + 2 * HZ, HZ, false, false, false));
    assert(iz2_menu_draw(&idle, wake_at + 3 * HZ, HZ, true, false, false));
    assert(iz2_menu_draw(&idle, 1, HZ, true, false, false));
    puts("PASS: 10 idle frames in 600 input polls; immediate refresh for input, status and busy screens");
}
