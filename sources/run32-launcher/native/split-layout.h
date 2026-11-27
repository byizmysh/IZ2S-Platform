#ifndef IZ_SPLIT_LAYOUT_H
#define IZ_SPLIT_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "split-windows.h"

enum { SPLIT_SCREEN_W = 1920, SPLIT_SCREEN_H = 1080, SPLIT_MENU_WORK_H = 934,
       SPLIT_BAR_X = 97, SPLIT_BAR_Y = 995, SPLIT_BUTTON_PITCH = 217,
       SPLIT_BUTTON_W = 207, SPLIT_BUTTON_H = 62, SPLIT_BUTTONS = 8, SPLIT_TOUCH_BYTES = 656 };
typedef struct { float x, y, w, h; } IzSplitRect;
typedef struct {
    bool menu, floating, swapped, dragging, held, consumed;
    unsigned pip_slot, corner, size, selected, touch_pane, finger;
    float drag_x, drag_y, grab_x, grab_y;
} IzSplitUi;
typedef struct { IzSplitRect frame[2], content[2]; } IzSplitLayout;
enum { SPLIT_UI_FOCUS, SPLIT_UI_SOUND, SPLIT_UI_SWAP, SPLIT_UI_FLOAT,
       SPLIT_UI_SMALLER, SPLIT_UI_LARGER, SPLIT_UI_CORNER, SPLIT_UI_CLOSE };

static inline bool split_contains(IzSplitRect r, float x, float y)
{ return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
static inline float split_clamp(float v, float lo, float hi)
{ return v < lo ? lo : v > hi ? hi : v; }

static inline float split_work_height(const IzSplitUi *ui)
{ return ui->menu ? SPLIT_MENU_WORK_H : SPLIT_SCREEN_H; }

static inline IzSplitLayout split_layout(const IzSplitUi *ui, unsigned visible)
{
    IzSplitLayout layout = {0};
    const float work_h = split_work_height(ui), bottom = work_h - 4;
    if (visible != SPLIT_BOTH) {
        for (unsigned slot = 0; slot < 2; slot++)
            if (visible & (1u << slot)) layout.frame[slot] = (IzSplitRect){4, 4, 1912, work_h - 8};
    } else if (ui->floating) {
        const float widths[] = {680, 750, 820, 890, 960};
        const float w = widths[ui->size <= 4 ? ui->size : 4], h = w * 9 / 16;
        const unsigned pip = ui->pip_slot & 1;
        layout.frame[pip ^ 1] = (IzSplitRect){4, 4, 1912, work_h - 8};
        float x = (ui->corner & 1) ? 1916 - w : 4;
        float y = (ui->corner & 2) ? bottom - h : 4;
        if (ui->dragging) {x = ui->drag_x; y = ui->drag_y;}
        layout.frame[pip] = (IzSplitRect){split_clamp(x, 4, 1916 - w),
            split_clamp(y, 4, bottom - h), w, h};
    } else {
        const float widths[] = {638, 1270};
        const float xs[] = {ui->swapped ? 1278 : 4, ui->swapped ? 4 : 646};
        for (unsigned slot = 0; slot < 2; slot++) {
            const float w = widths[slot];
            const float h = ui->menu ? 34 + (w - 4) * 9 / 16 : w * 9 / 16;
            layout.frame[slot] = (IzSplitRect){xs[slot], (work_h - h) / 2, w, h};
        }
    }
    for (unsigned slot = 0; slot < 2; slot++) {
        IzSplitRect f = layout.frame[slot];
        if (!f.w) continue;
        // A narrow draggable title strip, with no padding around app content.
        const float title = ui->menu ? 32 : 0, inset = ui->menu ? 2 : 0;
        const float available_h = f.h - title;
        float w = f.w - inset * 2, h = w * 9 / 16;
        if (h > available_h - inset) {h = available_h - inset; w = h * 16 / 9;}
        layout.content[slot] = (IzSplitRect){f.x + (f.w - w) / 2,
            f.y + title + (available_h - h) / 2, w, h};
    }
    return layout;
}

static inline unsigned split_hit_pane(const IzSplitUi *ui, const IzSplitLayout *layout,
                                       unsigned visible, float x, float y)
{
    const IzSplitRect *targets = ui->menu ? layout->frame : layout->content;
    if (ui->floating && visible == SPLIT_BOTH &&
        split_contains(targets[ui->pip_slot], x, y)) return 1u << ui->pip_slot;
    for (unsigned slot = 0; slot < 2; slot++)
        if ((visible & (1u << slot)) && split_contains(targets[slot], x, y)) return 1u << slot;
    return 0;
}

static inline void split_ui_action(IzSplitUi *ui, IzSplitWindows *windows,
                                    unsigned *sound, unsigned action)
{
    if (action == SPLIT_UI_FOCUS) split_window_focus(windows);
    else if (action == SPLIT_UI_SOUND) *sound = split_sound_next(windows->visible, *sound);
    else if (action == SPLIT_UI_CLOSE) split_window_exit(windows, windows->visible);
    else if (windows->visible == SPLIT_BOTH) {
        if (action == SPLIT_UI_SWAP) {if (ui->floating) ui->pip_slot ^= 1; else ui->swapped = !ui->swapped;}
        else if (action == SPLIT_UI_FLOAT) ui->floating = !ui->floating;
        else if (ui->floating && action == SPLIT_UI_SMALLER && ui->size) ui->size--;
        else if (ui->floating && action == SPLIT_UI_LARGER && ui->size < 4) ui->size++;
        else if (ui->floating && action == SPLIT_UI_CORNER) ui->corner = (ui->corner + 1) % 4;
    }
}

// The existing app ABI uses the 656-byte HID block in 1280 x 720 coordinates.
static inline uint32_t split_touch_word(const uint8_t *p, unsigned at)
{ uint32_t value; memcpy(&value, p + at, 4); return value; }
static inline void split_touch_put(uint8_t *p, unsigned at, uint32_t value)
{ memcpy(p + at, &value, 4); }

static inline void split_ui_touch(IzSplitUi *ui, IzSplitWindows *windows,
                                   unsigned *sound, const uint8_t raw[SPLIT_TOUCH_BYTES])
{
    unsigned count = split_touch_word(raw, 8);
    if (count > 16) count = 0;
    IzSplitLayout layout = split_layout(ui, windows->visible);
    if (!count) {
        if (ui->dragging) {
            IzSplitRect r = layout.frame[ui->pip_slot];
            ui->corner = (r.x + r.w / 2 >= 960 ? 1 : 0) |
                         (r.y + r.h / 2 >= split_work_height(ui) / 2 ? 2 : 0);
        }
        ui->held = ui->dragging = ui->consumed = false;
        ui->touch_pane = 0;
        return;
    }
    unsigned index = 0;
    if (ui->held) {
        while (index < count && split_touch_word(raw, 28 + index * 40) != ui->finger) index++;
        if (index == count) return; // Do not transfer an in-progress UI gesture to another finger.
    }
    const float x = split_touch_word(raw, 32 + index * 40) * 1.5f;
    const float y = split_touch_word(raw, 36 + index * 40) * 1.5f;
    if (!ui->held) {
        ui->held = true;
        ui->finger = split_touch_word(raw, 28 + index * 40);
        ui->consumed = true;
        if (ui->menu && y >= SPLIT_BAR_Y && y < SPLIT_BAR_Y + SPLIT_BUTTON_H && x >= SPLIT_BAR_X) {
            unsigned button = (unsigned)(x - SPLIT_BAR_X) / SPLIT_BUTTON_PITCH;
            if (button < SPLIT_BUTTONS && x < SPLIT_BAR_X + button * SPLIT_BUTTON_PITCH + SPLIT_BUTTON_W) {
                ui->selected = button;
                split_ui_action(ui, windows, sound, button);
            }
            return;
        }
        unsigned pane = split_hit_pane(ui, &layout, windows->visible, x, y);
        if (!pane) return;
        unsigned slot = pane == SPLIT_LEFT ? 0 : 1;
        IzSplitRect frame = layout.frame[slot];
        if (ui->menu && y < frame.y + 32 && x >= frame.x + frame.w - 42) {
            *sound = *sound == slot + 1 ? 0 : slot + 1;
            return;
        }
        bool changed = windows->focus != pane;
        windows->focus = pane;
        if (ui->menu && y < frame.y + 32 && ui->floating && slot == ui->pip_slot && windows->visible == SPLIT_BOTH) {
            ui->dragging = true;
            ui->grab_x = x - frame.x; ui->grab_y = y - frame.y;
            ui->drag_x = frame.x; ui->drag_y = frame.y;
        } else if (!ui->menu && !changed && split_contains(layout.content[slot], x, y)) {
            ui->consumed = false;
            ui->touch_pane = pane;
        }
        // The first tap selects an inactive pane; it never presses an app button.
    } else if (ui->dragging && windows->visible == SPLIT_BOTH) {
        IzSplitRect r = layout.frame[ui->pip_slot];
        ui->drag_x = split_clamp(x - ui->grab_x, 4, 1916 - r.w);
        ui->drag_y = split_clamp(y - ui->grab_y, 4, split_work_height(ui) - 4 - r.h);
    }
}

static inline void split_route_touch(const IzSplitUi *ui, const IzSplitLayout *layout,
                                      unsigned visible, unsigned pane, const uint8_t *raw,
                                      uint8_t *out, bool neutral)
{
    memset(out, 0, SPLIT_TOUCH_BYTES);
    memcpy(out, raw, 8); // Sampling number continues even for empty touch reports.
    if (neutral || ui->consumed || ui->touch_pane != pane) return;
    unsigned count = split_touch_word(raw, 8), kept = 0, slot = pane == SPLIT_LEFT ? 0 : 1;
    if (count > 16 || !(visible & pane)) return;
    IzSplitRect r = layout->content[slot];
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *touch = raw + 16 + i * 40;
        float x = split_touch_word(touch, 16) * 1.5f, y = split_touch_word(touch, 20) * 1.5f;
        if (!split_contains(r, x, y) || split_hit_pane(ui, layout, visible, x, y) != pane) continue;
        uint8_t *mapped = out + 16 + kept++ * 40;
        memcpy(mapped, touch, 40);
        split_touch_put(mapped, 16, (uint32_t)split_clamp((x - r.x) * 1280 / r.w, 0, 1279));
        split_touch_put(mapped, 20, (uint32_t)split_clamp((y - r.y) * 720 / r.h, 0, 719));
        split_touch_put(mapped, 24, (uint32_t)split_clamp(split_touch_word(touch, 24) * 1920.0f / r.w, 0, 1280));
        split_touch_put(mapped, 28, (uint32_t)split_clamp(split_touch_word(touch, 28) * 1080.0f / r.h, 0, 720));
    }
    split_touch_put(out, 8, kept);
}
#endif
