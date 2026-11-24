#ifndef IZ2S_MENU_CLOCK_H
#define IZ2S_MENU_CLOCK_H

#include <stdint.h>
#include <string.h>
#include <time.h>

/* The clock occupies the gap between the activity header and tile animation. */
#define IZ2_MENU_CLOCK_OFFSET (1024u + 0xf0080u)
#define IZ2_MENU_CLOCK_MAGIC UINT32_C(0x495a434b)

static inline void iz2_menu_clock_publish(uint8_t *controls, uint8_t *snapshot)
{
    static time_t sample = (time_t)-1;
    static uint32_t clock_data[2];
    const time_t now = time(NULL);
    if (now != sample) {
        struct tm local;
        sample = now;
        if (now != (time_t)-1 && localtime_r(&now, &local) &&
            local.tm_hour >= 0 && local.tm_hour < 24 &&
            local.tm_min >= 0 && local.tm_min < 60) {
            clock_data[0] = IZ2_MENU_CLOCK_MAGIC;
            clock_data[1] = (uint32_t)(local.tm_hour * 60 + local.tm_min);
        } else {
            clock_data[0] = 0;
            clock_data[1] = 0;
        }
    }
    memcpy(controls + IZ2_MENU_CLOCK_OFFSET, clock_data, sizeof clock_data);
    memcpy(snapshot + IZ2_MENU_CLOCK_OFFSET, clock_data, sizeof clock_data);
}

#endif
