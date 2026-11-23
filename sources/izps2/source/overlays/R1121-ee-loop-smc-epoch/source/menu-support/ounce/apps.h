#pragma once
#include "../legacy/native/abi.h"

typedef struct {char path[512],arguments[516];u8 header[4096];size_t header_bytes;} IzOunceLaunch;
void iz_ounce_apps_init(const char *self);
u32 iz_ounce_apps_scan(IzPlatformApp *apps,u32 capacity,u32 *count);
u32 iz_ounce_apps_prepare(u32 token,IzOunceLaunch *launch);
u32 iz_ounce_apps_remove(u32 token);
u32 iz_ounce_apps_page(IzPlatformApp *apps,u32 capacity,u32 offset,u32 *count,u32 *total);
