/* SPDX-License-Identifier: ISC
 * Adapted from nx-hbloader/source/main.c at 82b95122c5ae8dc059bf23893ba7623c72c86773.
 * Copyright 2017-2018 nx-hbloader Authors. See ../LICENSE.md.
 */
#include "config.h"

#include <stdint.h>

_Static_assert(sizeof(HbloaderConfigEntry) == 24, "homebrew configuration ABI size");

int hbloaderSwitch2BuildConfig(const HbloaderConfigInput *input,
                               HbloaderConfigEntry entries[HBLOADER_CONFIG_ENTRY_COUNT]) {
    if (!input || !entries || !input->main_thread_handle || !input->process_handle ||
        !input->heap_address || !input->heap_size ||
        !input->argv || !input->next_path ||
        !input->next_argv || !input->notice || !input->notice_size) {
        return -1;
    }

    const uint64_t applet_flags = input->is_application ? HBLOADER_APPLET_APPLICATION_OVERRIDE : 0;
    const HbloaderConfigEntry built[HBLOADER_CONFIG_ENTRY_COUNT] = {
        {HBLOADER_ENTRY_MAIN_THREAD, 0, {input->main_thread_handle, 0}},
        {HBLOADER_ENTRY_PROCESS_HANDLE, 0, {input->process_handle, 0}},
        {HBLOADER_ENTRY_APPLET_TYPE,
         0,
         {input->is_application ? HBLOADER_APPLET_TYPE_SYSTEM_APPLICATION
                                : HBLOADER_APPLET_TYPE_LIBRARY_APPLET,
          applet_flags}},
        {HBLOADER_ENTRY_OVERRIDE_HEAP,
         HBLOADER_ENTRY_MANDATORY,
         {input->heap_address, input->heap_size}},
        {HBLOADER_ENTRY_ARGV, 0, {0, (uint64_t)(uintptr_t)input->argv}},
        {HBLOADER_ENTRY_NEXT_LOAD,
         0,
         {(uint64_t)(uintptr_t)input->next_path, (uint64_t)(uintptr_t)input->next_argv}},
        {HBLOADER_ENTRY_LAST_RESULT, 0, {input->last_result, 0}},
        {HBLOADER_ENTRY_SYSCALL_HINT,
         0,
         {input->syscall_hints[0], input->syscall_hints[1]}},
        {HBLOADER_ENTRY_SYSCALL_HINT_2, 0, {input->syscall_hints[2], 0}},
        {HBLOADER_ENTRY_RANDOM_SEED, 0, {input->random_seed[0], input->random_seed[1]}},
        {HBLOADER_ENTRY_USER_ID_STORAGE,
         0,
         {(uint64_t)(uintptr_t)input->user_id_storage, 0}},
        {HBLOADER_ENTRY_HOS_VERSION, 0, {input->hos_version, HBLOADER_OUNCEHEN_MAGIC}},
        {HBLOADER_ENTRY_DEBUG_BSD,
         0,
         {input->debug_bsd_handle, (uint32_t)input->debug_socket}},
        {HBLOADER_ENTRY_OVERRIDE_SERVICE, 0,
         {HBLOADER_FSP_SRV_NAME, input->fs_root_handle}},
        {HBLOADER_ENTRY_OVERRIDE_SERVICE, 0,
         {HBLOADER_LDR_RO_NAME, input->ro_handle}},
        {HBLOADER_ENTRY_OVERRIDE_SERVICE, 0,
         {HBLOADER_SM_NAME, input->sm_handle}},
        {HBLOADER_ENTRY_OVERRIDE_SERVICE, 0,
         {HBLOADER_FSP_LDR_NAME, input->fs_ldr_handle}},
        {HBLOADER_ENTRY_MODULES, 0, {input->modules_info, 0}},
        {HBLOADER_ENTRY_END,
         0,
         {(uint64_t)(uintptr_t)input->notice, input->notice_size}},
    };
    /* As upstream, a service override is passed only for a session the
     * loader holds; the entries after it move up and END stays last. The
     * module entry is passed only for a dynamically linked program. */
    size_t count = 0;
    for (size_t i = 0; i < HBLOADER_CONFIG_ENTRY_COUNT; ++i) {
        if (built[i].key == HBLOADER_ENTRY_OVERRIDE_SERVICE && !built[i].value[1])
            continue;
        if (built[i].key == HBLOADER_ENTRY_MODULES && !built[i].value[0])
            continue;
        entries[count++] = built[i];
    }
    while (count < HBLOADER_CONFIG_ENTRY_COUNT)
        entries[count++] = (HbloaderConfigEntry){0};
    return 0;
}
