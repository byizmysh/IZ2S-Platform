// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <switch/types.h>
#include <switch/result.h>

// Private connection owned by the RO process, independent of an ldr:ro client.
Result roControlAdopt(Handle copied_session, u64* out_hen_pid);
Result roControlCreateHomebrewSession(Handle process, Handle* out_client);
