#ifndef PS2_APP_MENU_H
#define PS2_APP_MENU_H
#include "sw_merge.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct Ps2AppMenuView {
    uint32_t visible, selected, waiting, confirming, message;
} Ps2AppMenuView;
enum Ps2AppMenuMessage {
    PS2_MENU_NONE=0, PS2_MENU_SAVED=1, PS2_MENU_LOADED=2,
    PS2_MENU_FAILED=3, PS2_MENU_OUTCOME_UNKNOWN=4
};
/* Publish from the application CPU thread; snapshot/render on serial MTGS.
 * One atomic packed value prevents mixed menu selections/status messages. */
void ps2_app_menu_publish(const Ps2AppMenuView *);
void ps2_app_menu_snapshot(Ps2AppMenuView *);
/* Draw only into a presentation COPY, leaving the guest's GS pixels intact. */
int ps2_app_menu_render(Ps2SwMergeOutput *,const Ps2AppMenuView *);
#ifdef __cplusplus
}
#endif
#endif
