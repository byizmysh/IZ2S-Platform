#pragma once
#include <stdint.h>
uint32_t iz_ounce_font_open(const char *path);
void iz_ounce_font_close(void);
void iz_ounce_title(void *tile,const char *name);
void iz_ounce_label(uint8_t *bitmap,unsigned width,unsigned height,const char *text,float size);
