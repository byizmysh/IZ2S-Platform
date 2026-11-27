/* SPDX-License-Identifier: ISC
 * Adapted from nx-hbloader/source/trampoline.s at
 * 82b95122c5ae8dc059bf23893ba7623c72c86773.
 * Copyright 2017-2018 nx-hbloader Authors. See ../LICENSE.md.
 */
#ifndef NX_HBLOADER_SWITCH2_ENTRY_H
#define NX_HBLOADER_SWITCH2_ENTRY_H

#include "config.h"

#include <stdint.h>

/* Enters one NRO using its loader ABI: x0=entries, x1=return handle,
 * x30=the return address immediately after blr. Restores the loader's stack,
 * x18-x30 and d8-d15 when the NRO returns normally or through libnx exit.
 * The caller owns a separate, 16-byte-aligned stack_top and retains the
 * entries, argv and heap until return. Exactly one NRO may be active. */
uint32_t hbloaderSwitch2Enter(const HbloaderConfigEntry *entries,
                              uint64_t return_handle,
                              uint64_t entry,
                              void *stack_top);

#endif
