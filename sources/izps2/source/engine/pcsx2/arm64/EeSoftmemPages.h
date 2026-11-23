// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <cstdint>

inline constexpr unsigned EeSoftmemIndirectBit = 0;
inline constexpr uintptr_t EeSoftmemIndirectPage = uintptr_t{1} << EeSoftmemIndirectBit;

// The JIT adds the full zero-extended guest address to the delta using the
// memory instruction's register offset. Anything tagged uses the canonical map.
constexpr uintptr_t EncodeEeSoftmemPage(uintptr_t delta, bool handler)
{
	return (handler || (delta & EeSoftmemIndirectPage)) ? EeSoftmemIndirectPage : delta;
}

#ifdef PS2_EE_SOFTMEM_TAGGED_PAGES
extern uintptr_t g_eeSoftmemPages[];
extern bool g_eeSoftmemUseCanonicalMap;
#endif
