#ifndef IZPSX_SETTINGS_MENU_H
#define IZPSX_SETTINGS_MENU_H
#include "native-menu.h"
typedef struct {unsigned category,row,previous,held,armed,pending_close,confirm_reset,closed,player,confirm_disconnect,disconnect_request,disconnected;uint32_t error,action_error;} Ps1SettingsMenu;
void ps1_settings_menu_init(Ps1SettingsMenu*,uint32_t held_buttons);
void ps1_settings_menu_step(Ps1SettingsMenu*,Ps1Settings*,uint32_t buttons);
void ps1_settings_menu_draw(const Ps1SettingsMenu*,const Ps1Settings*,const Ps1MenuConfig*);
#endif
