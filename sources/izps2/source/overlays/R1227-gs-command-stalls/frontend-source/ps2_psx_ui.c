/* IZPSX bitmap UI primitives, shared with the IZPS2 presentation. */
#include "ps2_psx_ui.h"
#include "psx-ui-assets/logo-data.h"
#include "psx-ui-assets/ui-font.h"
#include <stddef.h>

/* Original compact 5x7 bitmap lettering. Lowercase filenames render uppercase;
 * their exact UTF-8 bytes are preserved for filesystem access. */
typedef struct
{
    char c;
    uint8_t r[7];
} Glyph;
static const Glyph glyphs[] = {{'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}},
                               {'C', {14, 17, 16, 16, 16, 17, 14}}, {'D', {30, 17, 17, 17, 17, 17, 30}},
                               {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
                               {'G', {14, 17, 16, 23, 17, 17, 15}}, {'H', {17, 17, 17, 31, 17, 17, 17}},
                               {'I', {14, 4, 4, 4, 4, 4, 14}},      {'J', {7, 2, 2, 2, 18, 18, 12}},
                               {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
                               {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 25, 25, 21, 19, 19, 17}},
                               {'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}},
                               {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
                               {'S', {15, 16, 16, 14, 1, 1, 30}},   {'T', {31, 4, 4, 4, 4, 4, 4}},
                               {'U', {17, 17, 17, 17, 17, 17, 14}}, {'V', {17, 17, 17, 17, 17, 10, 4}},
                               {'W', {17, 17, 17, 21, 21, 21, 10}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
                               {'Y', {17, 17, 10, 4, 4, 4, 4}},     {'Z', {31, 1, 2, 4, 8, 16, 31}},
                               {'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}},
                               {'2', {14, 17, 1, 2, 4, 8, 31}},     {'3', {30, 1, 1, 14, 1, 1, 30}},
                               {'4', {2, 6, 10, 18, 31, 2, 2}},     {'5', {31, 16, 16, 30, 1, 1, 30}},
                               {'6', {14, 16, 16, 30, 17, 17, 14}}, {'7', {31, 1, 2, 4, 8, 8, 8}},
                               {'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 1, 14}},
                               {'.', {0, 0, 0, 0, 0, 12, 12}},      {'-', {0, 0, 0, 31, 0, 0, 0}},
                               {'_', {0, 0, 0, 0, 0, 0, 31}},       {'/', {1, 2, 2, 4, 8, 8, 16}},
                               {':', {0, 12, 12, 0, 12, 12, 0}},    {'[', {14, 8, 8, 8, 8, 8, 14}},
                               {']', {14, 2, 2, 2, 2, 2, 14}},      {'(', {2, 4, 8, 8, 8, 4, 2}},
                               {')', {8, 4, 2, 2, 2, 4, 8}},        {'+', {0, 4, 4, 31, 4, 4, 0}},
                               {'?', {14, 17, 1, 2, 4, 0, 4}},      {'@', {14, 17, 23, 21, 23, 16, 14}},
                               {'%', {25, 25, 2, 4, 8, 19, 19}},    {'b', {16, 16, 22, 25, 17, 17, 30}},
                               {'y', {0, 0, 17, 17, 15, 1, 14}},    {'i', {4, 0, 12, 4, 4, 4, 14}},
                               {'z', {0, 0, 31, 2, 4, 8, 31}},      {'m', {0, 0, 26, 21, 21, 21, 21}},
                               {'s', {0, 0, 15, 16, 14, 1, 30}},    {'h', {16, 16, 22, 25, 17, 17, 17}},
                               {'>', {16, 8, 4, 2, 4, 8, 16}},      {'!', {4, 4, 4, 4, 4, 0, 4}},
                               {'=', {0, 31, 0, 31, 0, 0, 0}},      {',', {0, 0, 0, 0, 0, 4, 8}}};
void ps2_ui_rect(uint32_t *p, unsigned x, unsigned y, unsigned w, unsigned h, uint32_t color)
{
    if (x >= 640 || y >= 480)
        return;
    if (w > 640 - x)
        w = 640 - x;
    if (h > 480 - y)
        h = 480 - y;
    for (unsigned yy = y; yy < y + h; yy++)
        for (unsigned xx = x; xx < x + w; xx++)
            p[yy * 640 + xx] = color;
}
static void text_case(uint32_t *p, unsigned x, unsigned y, const char *s, unsigned scale, uint32_t color,
                      unsigned limit, int upper)
{
    if (scale == 1)
    {
        for (unsigned n = 0; s[n] && n < limit && x + n * 7 + 6 < 640; n++)
        {
            unsigned c = (unsigned char)s[n];
            if (c < 32 || c > 126)
                c = '?';
            for (unsigned yy = 0; yy < 12 && y + yy < 480; yy++)
                for (unsigned xx = 0; xx < 7; xx++)
                {
                    unsigned a = ((izpsx_ui_font[c - 32][yy] >> (xx * 2)) & 3) * 85;
                    if (!a)
                        continue;
                    uint32_t old = p[(y + yy) * 640 + x + n * 7 + xx], out = 0xff000000;
                    for (unsigned ch = 0; ch < 3; ch++)
                    {
                        unsigned shift = ch * 8;
                        out |=
                            ((((color >> shift) & 255) * a + ((old >> shift) & 255) * (255 - a)) / 255)
                            << shift;
                    }
                    p[(y + yy) * 640 + x + n * 7 + xx] = out;
                }
        }
        return;
    }
    for (unsigned n = 0; s[n] && n < limit; n++)
    {
        unsigned c = (unsigned char)s[n];
        if (upper && c >= 'a' && c <= 'z')
            c -= 32;
        if (c == ' ')
            continue;
        const uint8_t *g = 0;
        for (size_t j = 0; j < sizeof glyphs / sizeof glyphs[0]; j++)
            if ((unsigned char)glyphs[j].c == c)
            {
                g = glyphs[j].r;
                break;
            }
        if (!g)
        {
            static const uint8_t unknown[] = {14, 17, 1, 2, 4, 0, 4};
            g = unknown;
        }
        for (unsigned yy = 0; yy < 7; yy++)
            for (unsigned xx = 0; xx < 5; xx++)
                if (g[yy] & (1u << (4 - xx)))
                    ps2_ui_rect(p, x + n * 6 * scale + xx * scale, y + yy * scale, scale, scale, color);
    }
}
void ps2_ui_text(uint32_t *p, unsigned x, unsigned y, const char *s, unsigned scale, uint32_t color,
                 unsigned limit)
{
    text_case(p, x, y, s, scale, color, limit, 1);
}

void ps2_ui_logo(uint32_t *p, unsigned x, unsigned y, unsigned scale)
{
    unsigned pixel = 0;
    for (unsigned i = 0; i < sizeof izpsx_logo / sizeof izpsx_logo[0]; i++)
        for (unsigned n = 0; n < izpsx_logo[i].count; n++, pixel++)
        {
            uint32_t color = izpsx_logo[i].color;
            if (!(color >> 24))
                continue;
            unsigned xx = x + (pixel % IZPSX_LOGO_W) * scale, yy = y + (pixel / IZPSX_LOGO_W) * scale;
            for (unsigned dy = 0; dy < scale; dy++)
                for (unsigned dx = 0; dx < scale; dx++)
                    if (xx + dx < 640 && yy + dy < 480)
                    {
                        uint32_t old = p[(yy + dy) * 640 + xx + dx], a = color >> 24, out = 0xff000000;
                        for (unsigned ch = 0; ch < 3; ch++)
                        {
                            unsigned shift = ch * 8;
                            out |= ((((color >> shift) & 255) * a + ((old >> shift) & 255) * (255 - a)) /
                                    255)
                                   << shift;
                        }
                        p[(yy + dy) * 640 + xx + dx] = out;
                    }
        }
}

void ps2_ui_chrome(uint32_t *p, const char *section)
{
    uint32_t accent = UINT32_C(0xffeac45c);
    for (unsigned y = 0; y < 480; y++)
    {
        unsigned r = 12 + y / 80, g = 16 + y / 48, b = 28 + y / 24;
        ps2_ui_rect(p, 0, y, 640, 1, 0xff000000u | (b << 16) | (g << 8) | r);
    }
    ps2_ui_rect(p, 0, 0, 640, 3, accent);
    ps2_ui_rect(p, 22, 18, 3, 52, accent);
    ps2_ui_logo(p, 17, 4, 1);
    ps2_ui_text(p, 110, 19, "IZPS2", 4, 0xfff5f2ec, 5);
    ps2_ui_text(p, 112, 58, section, 1, 0xffccbcb0, 60);
    text_case(p, 490, 27, "@byizmysh", 1, accent, 10, 0);
    ps2_ui_text(p, 490, 46, "PLAYSTATION 2", 1, 0xffccbcb0, 13);
    ps2_ui_rect(p, 22, 78, 596, 1, 0xff3c3026);
    ps2_ui_rect(p, 22, 416, 596, 1, 0xff3c3026);
}

void ps2_ui_pause(uint32_t *pixels, unsigned selection, int confirm_restart) {
    static const char labels[PS2_PAUSE_ROWS][24] = {
        "CONTINUE", "SCENES", "SETTINGS", "RESTART GAME", "CHOOSE GAME", "EXIT IZPS2 EMULATOR"
    };
    static const char notes[PS2_PAUSE_ROWS][48] = {
        "PICK UP WHERE YOU LEFT OFF", "SAVE OR LOAD A SCENE FOR THIS GAME",
        "VIDEO, SOUND, EMULATION AND CONTROLS",
        "RESTART THE CURRENT GAME", "BROWSE YOUR SD LIBRARY", "BACK TO YOUR APPLICATIONS"
    };
    const uint32_t accent = 0xffeac45c;
    ps2_ui_chrome(pixels, "IZPS2 EMULATOR MENU");
    for (unsigned i = 0; i < PS2_PAUSE_ROWS; ++i) {
        const unsigned y = 105 + i * 45;
        if (i == selection) {
            ps2_ui_rect(pixels, 22, y - 7, 596, 43, 0xff493623);
            ps2_ui_rect(pixels, 22, y - 7, 3, 43, accent);
            ps2_ui_text(pixels, 38, y, ">", 2, accent, 1);
        }
        ps2_ui_text(pixels, 70, y, labels[i], 2, i == selection ? 0xfff5f2ec : 0xffc5b8ab, 40);
        ps2_ui_text(pixels, 70, y + 22, notes[i], 1, 0xffb3a79b, 70);
    }
    if (confirm_restart)
        ps2_ui_text(pixels, 24, 380, "RESTART THIS GAME? A CONFIRM / B CANCEL", 1, accent, 80);
    ps2_ui_text(pixels, 24, 432, "A SELECT    B CONTINUE    D-PAD SELECT", 1, 0xffe6d5c4, 85);
    ps2_ui_text(pixels, 24, 455, "+ GAME MENU / - IZPS2 MENU", 1, 0xffb3a79b, 85);
}
