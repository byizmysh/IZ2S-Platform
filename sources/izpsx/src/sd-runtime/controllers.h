#ifndef IZPSX_CONTROLLERS_H
#define IZPSX_CONTROLLERS_H
#include <stdint.h>
#include <stddef.h>
#include "settings.h"
enum {PS1_PAD_FULL=1,PS1_PAD_HANDHELD=2,PS1_PAD_DUAL=4,PS1_PAD_LEFT=8,PS1_PAD_RIGHT=16};
typedef struct {
 uint64_t buttons,sample;
 int32_t axes[4];
 uint32_t connected,id,attributes,style,shortcut_latched;
 uint64_t physical_buttons;
} Ps1Controller;
typedef struct {
 uintptr_t functions[9];
 Ps1Controller player[PS1_PLAYER_COUNT],device[9],menu;
 uint32_t assignment[PS1_PLAYER_COUNT];
 uint32_t configured,configuration_error,polls,style_error,original_styles,restore_styles;
} Ps1Controllers;
/* Borrow only the public environment's SDK mapping. No supervisor layout. */
uint32_t ps1_controllers_bind(Ps1Controllers*,uintptr_t sdk,uint64_t bytes);
uint32_t ps1_controllers_enable(Ps1Controllers*);
uint32_t ps1_controllers_poll(Ps1Controllers*);
uint32_t ps1_controllers_restore(Ps1Controllers*);
const char*ps1_controller_style_name(unsigned);
uint64_t ps1_controller_menu_buttons(const Ps1Controller*);
void ps1_controllers_assign(Ps1Controllers*,const uint32_t assignment[PS1_PLAYER_COUNT]);
uint32_t ps1_controllers_disconnect(Ps1Controllers*,unsigned player);
uint32_t ps1_controller_buttons(uint64_t native_buttons);
#endif
