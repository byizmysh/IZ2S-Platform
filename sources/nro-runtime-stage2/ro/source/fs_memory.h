// SPDX-License-Identifier: GPL-2.0-only
// RO's private ro:dmnt endpoint (../../common/ro_dmnt.h): reads and writes
// of the FS process's code memory for kaiHEN, and RO's diagnostics.
#pragma once

/// Answers one request on an ro:dmnt session. The reply is left in @p message.
void roFsMemoryDispatch(void* message);
