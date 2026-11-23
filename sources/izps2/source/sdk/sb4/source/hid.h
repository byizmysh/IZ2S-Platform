#pragma once
#include <stdint.h>

#define SB4_SERVICE_ARGS uint64_t a, uint64_t b, uint64_t c, uint64_t d, \
                         uint64_t e, uint64_t f, uint64_t g, uint64_t h

uint64_t sb4HidGetSupportedStyles(SB4_SERVICE_ARGS);
uint64_t sb4HidSetSupportedStyles(SB4_SERVICE_ARGS);
uint64_t sb4HidSetSupportedIds(SB4_SERVICE_ARGS);
uint64_t sb4HidDisconnectPad(SB4_SERVICE_ARGS);
