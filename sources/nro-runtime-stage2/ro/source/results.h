// SPDX-License-Identifier: GPL-2.0-only
// The results that RO returns.
#pragma once

#include <switch/result.h>

// RO module (22) results, as stock RO returns them for the same failures.
// The names are those of Atmosphere's ro results.
constexpr u32 Module_Ro = 22;
constexpr Result RoResult_OutOfAddressSpace = MAKERESULT(Module_Ro, 2);   // 0x416
constexpr Result RoResult_AlreadyLoaded = MAKERESULT(Module_Ro, 3);       // 0x616
constexpr Result RoResult_InvalidNro = MAKERESULT(Module_Ro, 4);          // 0x816
constexpr Result RoResult_TooManyNro = MAKERESULT(Module_Ro, 7);          // 0xe16
constexpr Result RoResult_TooManyNrr = MAKERESULT(Module_Ro, 8);          // 0x1016
constexpr Result RoResult_InvalidAddress = MAKERESULT(Module_Ro, 1025);   // 0x80216
constexpr Result RoResult_NotLoaded = MAKERESULT(Module_Ro, 1028);        // 0x80816
constexpr Result RoResult_NotRegistered = MAKERESULT(Module_Ro, 1029);    // 0x80a16

// OS module (3) results that RO returns for kernel mapping failures.
constexpr u32 Module_Os = 3;
constexpr Result RoResult_OsOutOfMemory = MAKERESULT(Module_Os, 9);                   // 0x1203
constexpr Result RoResult_OsInvalidCurrentMemoryState = MAKERESULT(Module_Os, 513);   // 0x40203

// A request that RO does not accept: a malformed message, a client that is
// not initialized for its process, or arguments that name nothing RO
// mapped. The private homebrew commands and the NRR checks use it.
constexpr Result RoResult_BadRequest = MAKERESULT(Module_Libnx, LibnxError_BadInput);
