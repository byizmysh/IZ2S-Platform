#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IZ_MENU_ABI 1
#define IZ_MENU_REPORT_BYTES 45

typedef bool (*IzMenuRead)(void *context,void *data,size_t bytes);
void iz_menu_update_init(const uint8_t factory[16]);
bool iz_menu_factory(void);
const char *iz_menu_image(void);
bool iz_ounce_menu_frame(bool idle);
const char *iz_menu_romfs(void);
void iz_menu_report(uint8_t wire[IZ_MENU_REPORT_BYTES]);
bool iz_menu_receive(const uint8_t version[16],uint32_t bytes,IzMenuRead read,void *context);
bool iz_menu_pending(void);
bool iz_menu_begin_trial(void);
bool iz_menu_trial(void);
bool iz_menu_accept(void);
void iz_menu_reject(uint32_t error);
