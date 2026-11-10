#ifndef PS1_NATIVE_DISPLAY_H
#define PS1_NATIVE_DISPLAY_H
#include "platform.h"
typedef struct {
 uint64_t device,queue,swapchain,acquire;
 uint64_t fn_acquire,fn_submit,fn_present,fn_idle,fn_sleep;
 uint64_t commands[8],semaphores[8],pad[5];
 uint32_t image_count,submissions,error,observed_buttons;
 uint64_t input_samples,reserved;
} Ps1Display;
_Static_assert(sizeof(Ps1Display)==272,"native display ABI");
void ps1_display_pace(uint64_t completed_frames);
uint32_t ps1_display_input(void);
int ps1_display_take_menu_request(void);
uint32_t ps1_display_menu_buttons(void);
int ps1_display_present(void);
#endif
