/* SPDX-License-Identifier: ISC
 * Adapted from nx-hbloader/source/main.c loadNro at
 * 82b95122c5ae8dc059bf23893ba7623c72c86773.
 * Copyright 2017-2018 nx-hbloader Authors. See ../LICENSE.md.
 */
#include "cycle.h"

#include <stddef.h>

static int copy_string(char *destination, size_t capacity, const char *source) {
    if (!destination || !capacity || !source)
        return -1;
    size_t i = 0;
    while (i + 1 < capacity && source[i]) {
        destination[i] = source[i];
        ++i;
    }
    if (source[i])
        return -1;
    destination[i] = 0;
    return 0;
}

int hbloaderSwitch2Select(HbloaderCycle *cycle, const char *path, const char *argv) {
    if (!cycle || !path || !path[0] || !argv)
        return -1;
    char candidate_path[HBLOADER_NEXT_PATH_BYTES] = {0};
    char candidate_argv[HBLOADER_ARGV_BYTES] = {0};
    if (copy_string(candidate_path, sizeof candidate_path, path) ||
        copy_string(candidate_argv, sizeof candidate_argv, argv))
        return -1;
    for (size_t i = 0; i < sizeof cycle->next_path; ++i)
        cycle->next_path[i] = candidate_path[i];
    for (size_t i = 0; i < sizeof cycle->next_argv; ++i)
        cycle->next_argv[i] = candidate_argv[i];
    return 0;
}

HbloaderCycleResult hbloaderSwitch2Step(HbloaderCycle *cycle,
                                        const HbloaderCyclePlatform *platform,
                                        const char *default_menu_path) {
    if (!cycle || !platform || !platform->map_image || !platform->unload_image ||
        !platform->prepare_config || !platform->enter || !default_menu_path ||
        !default_menu_path[0])
        return (HbloaderCycleResult){HBLOADER_CYCLE_INVALID, 0};

    if (cycle->has_mapping) {
        uint32_t result = platform->unload_image(platform->context, &cycle->mapped);
        if (result)
            return (HbloaderCycleResult){HBLOADER_CYCLE_UNLOAD, result};
        cycle->mapped = (HbloaderMappedImage){0};
        cycle->has_mapping = 0;
    }

    const int is_menu = !cycle->next_path[0];
    if (is_menu && hbloaderSwitch2Select(cycle, default_menu_path, default_menu_path))
        return (HbloaderCycleResult){HBLOADER_CYCLE_INVALID, 0};

    for (size_t i = 0; i < sizeof cycle->argv; ++i)
        cycle->argv[i] = cycle->next_argv[i];

    HbloaderMappedImage mapped = {0};
    uint32_t result = platform->map_image(platform->context, cycle->next_path, &mapped);
    /* map_image keeps owner non-null only if a failed load could not be
     * unmapped. No subsequent image may reuse that staging memory. */
    if (result && mapped.owner)
        return (HbloaderCycleResult){HBLOADER_CYCLE_UNLOAD, result};
    if (result && !is_menu) {
        /* A program that cannot be loaded, for example one whose shared
         * library is missing, does not end the process: the next step loads
         * the menu, which gets the failure as the last result. */
        cycle->next_path[0] = 0;
        cycle->last_result = result;
        return (HbloaderCycleResult){HBLOADER_CYCLE_OK, result};
    }
    if (result)
        return (HbloaderCycleResult){HBLOADER_CYCLE_MAP, result};
    if (!mapped.entry || !mapped.owner)
        return (HbloaderCycleResult){HBLOADER_CYCLE_MAP, 0};
    cycle->mapped = mapped;
    cycle->has_mapping = 1;

    /* Upstream clears this before entry so a returning app reloads hbmenu.
     * A running NRO may fill next_path/next_argv through ConfigEntry 2. */
    cycle->next_path[0] = 0;

    HbloaderConfigInput input = {0};
    result = platform->prepare_config(platform->context, &input);
    if (result)
        return (HbloaderCycleResult){HBLOADER_CYCLE_CONFIG, result};
    input.argv = cycle->argv;
    input.next_path = cycle->next_path;
    input.next_argv = cycle->next_argv;
    input.last_result = cycle->last_result;
    HbloaderConfigEntry entries[HBLOADER_CONFIG_ENTRY_COUNT] = {0};
    if (hbloaderSwitch2BuildConfig(&input, entries))
        return (HbloaderCycleResult){HBLOADER_CYCLE_CONFIG, 0};

    uint32_t app_result = 0;
    result = platform->enter(platform->context, mapped.entry, entries, UINT64_MAX, &app_result);
    if (result)
        return (HbloaderCycleResult){HBLOADER_CYCLE_ENTRY, result};
    cycle->last_result = app_result;
    return (HbloaderCycleResult){HBLOADER_CYCLE_OK, app_result};
}
