#ifndef PS2_PSX_UI_H
#define PS2_PSX_UI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PS2_UI_WIDTH = 640, PS2_UI_HEIGHT = 480 };
void ps2_ui_chrome(uint32_t *pixels, const char *section);
void ps2_ui_pause(uint32_t *pixels, unsigned selection, int confirm_restart);
void ps2_ui_logo(uint32_t *pixels, unsigned x, unsigned y, unsigned scale);
void ps2_ui_rect(uint32_t *pixels, unsigned x, unsigned y, unsigned width,
                 unsigned height, uint32_t color);
void ps2_ui_text(uint32_t *pixels, unsigned x, unsigned y, const char *text,
                 unsigned scale, uint32_t color, unsigned limit);

#ifdef __cplusplus
}
#endif
#endif
