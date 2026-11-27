/* SPDX-License-Identifier: ISC
 * Adapted from nx-hbloader/source/main.c at 82b95122c5ae8dc059bf23893ba7623c72c86773.
 * Copyright 2017-2018 nx-hbloader Authors. See ../LICENSE.md.
 */
#ifndef NX_HBLOADER_SWITCH2_CONFIG_H
#define NX_HBLOADER_SWITCH2_CONFIG_H

#include <stddef.h>
#include <stdint.h>

typedef struct HbloaderConfigEntry {
    uint32_t key;
    uint32_t flags;
    uint64_t value[2];
} HbloaderConfigEntry;

enum {
    HBLOADER_CONFIG_ENTRY_COUNT = 19,
    HBLOADER_ENTRY_MAIN_THREAD = 1,
    HBLOADER_ENTRY_NEXT_LOAD = 2,
    HBLOADER_ENTRY_OVERRIDE_HEAP = 3,
    HBLOADER_ENTRY_OVERRIDE_SERVICE = 4,
    HBLOADER_ENTRY_ARGV = 5,
    HBLOADER_ENTRY_SYSCALL_HINT = 6,
    HBLOADER_ENTRY_APPLET_TYPE = 7,
    HBLOADER_ENTRY_PROCESS_HANDLE = 10,
    HBLOADER_ENTRY_LAST_RESULT = 11,
    HBLOADER_ENTRY_RANDOM_SEED = 14,
    HBLOADER_ENTRY_USER_ID_STORAGE = 15,
    HBLOADER_ENTRY_HOS_VERSION = 16,
    HBLOADER_ENTRY_SYSCALL_HINT_2 = 17,
    /* Private same-process diagnostic borrow from the game-role loader.
     * The clean loader passes it empty (handle 0, socket -1): it holds no
     * BSD client while an NRO runs. Readers look it up by its key. */
    HBLOADER_ENTRY_DEBUG_BSD = 0x4f444247,
    /* libounce's DYNLINK_CONFIG_ENTRY_KEY: the images of a dynamically linked
     * program, for the runtime library that the loader enters. Value[0] is
     * the address of a DynlinkLoadInfo. */
    HBLOADER_ENTRY_MODULES = 0x4f4d4f44,
    HBLOADER_ENTRY_END = 0,
    HBLOADER_ENTRY_MANDATORY = 1,
    HBLOADER_APPLET_TYPE_LIBRARY_APPLET = 2,
    HBLOADER_APPLET_TYPE_SYSTEM_APPLICATION = 4,
    HBLOADER_APPLET_APPLICATION_OVERRIDE = 1,
};

/* HosVersion Value[1] marker 'OUNCEHEN', as Atmosphere's hbloader passes
 * 'ATMOSPHR' (0x41544d4f53504852): libounce-nx's env.c reads it as
 * hosversionIsKaiHEN(). */
#define HBLOADER_OUNCEHEN_MAGIC UINT64_C(0x4f554e434548454e)

#define HBLOADER_FSP_SRV_NAME UINT64_C(0x007672732d707366)
#define HBLOADER_LDR_RO_NAME UINT64_C(0x00006f723a72646c)
#define HBLOADER_SM_NAME UINT64_C(0x00000000003a6d73)
#define HBLOADER_FSP_LDR_NAME UINT64_C(0x0072646c2d707366)

typedef struct HbloaderConfigInput {
    uint32_t main_thread_handle;
    uint32_t process_handle;
    uint32_t fs_root_handle;
    uint32_t ro_handle;
    uint32_t sm_handle;
    /* A clone of HEN's fsp-ldr root for a Switch 2 program, or 0. */
    uint32_t fs_ldr_handle;
    uint32_t debug_bsd_handle;
    int32_t debug_socket;
    uint64_t heap_address;
    uint64_t heap_size;
    const char *argv;
    char *next_path;
    char *next_argv;
    uint32_t last_result;
    uint64_t syscall_hints[3];
    uint64_t random_seed[2];
    void *user_id_storage;
    uint32_t hos_version;
    uint8_t is_application;
    /* The DynlinkLoadInfo of a dynamically linked program, or 0. */
    uint64_t modules_info;
    const char *notice;
    size_t notice_size;
} HbloaderConfigInput;

/* Builds the upstream homebrew ABI entry order for one NRO launch. Every
 * pointer in the input and output must remain live until the NRO returns.
 * Runtime acquisition of handles, heap and firmware fields is separate.
 * A service override with handle 0 is omitted, and so is the module entry
 * of a statically linked program. The NRO opens other services, such as
 * set:sys, through the sm override.
 * The NRO opens the SD card through its own clone of the fsp-srv root, so
 * that the files it leaves open close when it closes that session. */
int hbloaderSwitch2BuildConfig(const HbloaderConfigInput *input,
                               HbloaderConfigEntry entries[HBLOADER_CONFIG_ENTRY_COUNT]);

#endif
