#ifndef IZ2S_MENU_NRO_RESULT_H
#define IZ2S_MENU_NRO_RESULT_H
#include <stdint.h>
/* Only the serial NRO handoff supplies this result. IZA owns its own error UI.
 * State 1 is the existing generic error, 5..11 describe Homebrew ABI 200..206. */
static inline uint32_t iz2_nro_result_message(uint32_t result) {
    if (!result) return 0;
    const uint32_t module = result & 0x1ffu, description = (result >> 9) & 0x1fffu;
    return module == 346u && description >= 200u && description <= 206u
        ? 5u + description - 200u : 1u;
}
#endif
