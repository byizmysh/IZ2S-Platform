/* SPDX-License-Identifier: ISC
 * Port of nx-hbloader's loadNro image lifecycle at
 * 82b95122c5ae8dc059bf23893ba7623c72c86773.
 * Copyright 2017-2018 nx-hbloader Authors. See ../LICENSE.md.
 */
#ifndef NX_HBLOADER_SWITCH2_CYCLE_H
#define NX_HBLOADER_SWITCH2_CYCLE_H

#include "config.h"

#include <stdint.h>

enum {
    HBLOADER_NEXT_PATH_BYTES = 512,
    HBLOADER_ARGV_BYTES = 2048,
};

typedef struct HbloaderMappedImage {
    uint64_t entry;
    void *owner;
} HbloaderMappedImage;

typedef struct HbloaderCycle {
    char next_path[HBLOADER_NEXT_PATH_BYTES];
    char next_argv[HBLOADER_ARGV_BYTES];
    char argv[HBLOADER_ARGV_BYTES];
    uint32_t last_result;
    HbloaderMappedImage mapped;
    uint8_t has_mapping;
} HbloaderCycle;

typedef enum HbloaderCycleStage {
    HBLOADER_CYCLE_OK,
    HBLOADER_CYCLE_INVALID,
    HBLOADER_CYCLE_UNLOAD,
    HBLOADER_CYCLE_MAP,
    HBLOADER_CYCLE_CONFIG,
    HBLOADER_CYCLE_ENTRY,
} HbloaderCycleStage;

typedef struct HbloaderCycleResult {
    HbloaderCycleStage stage;
    uint32_t result;
} HbloaderCycleResult;

/* These callbacks are the Switch 2 platform boundary. map_image must read
 * the selected SD path and map it through the owned ldr:ro session. enter
 * must use the loader stack/return trampoline; a direct C call is only
 * suitable for host tests. Its function Result reports a loader failure;
 * out_app_result is the NRO's own return value, which may be nonzero.
 * unload_image must retain its owner on failure. When map_image fails for a
 * program that the menu selected, the step succeeds without entering
 * anything, and the next step loads the menu. */
typedef struct HbloaderCyclePlatform {
    void *context;
    uint32_t (*map_image)(void *context, const char *path, HbloaderMappedImage *out);
    uint32_t (*unload_image)(void *context, const HbloaderMappedImage *image);
    uint32_t (*prepare_config)(void *context, HbloaderConfigInput *out);
    uint32_t (*enter)(void *context,
                      uint64_t entry,
                      const HbloaderConfigEntry *entries,
                      uint64_t return_handle,
                      uint32_t *out_app_result);
} HbloaderCyclePlatform;

int hbloaderSwitch2Select(HbloaderCycle *cycle, const char *path, const char *argv);
HbloaderCycleResult hbloaderSwitch2Step(HbloaderCycle *cycle,
                                        const HbloaderCyclePlatform *platform,
                                        const char *default_menu_path);

#endif
