// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <bit>
#include <cstddef>
#include <cstring>
#include <type_traits>

// Private to owners 186/191/199. No shared-header or class-layout change.
// Production remains opt-in until the affected ARM matrix and Switch run pass.
#ifndef PS2_EE_SOFTMEM_COLD_OOL
#define PS2_EE_SOFTMEM_COLD_OOL 0
#endif
#ifndef PS2_EE_SOFTMEM_BLOCK_TAIL
#define PS2_EE_SOFTMEM_BLOCK_TAIL 0
#endif

template <typename Apply>
static void eeSoftmemForEachRegister(u32 mask, Apply apply)
{
	while (mask)
	{
		apply(std::countr_zero(mask));
		mask &= mask - 1;
	}
}

#if (PS2_EE_SOFTMEM_COLD_OOL && defined(PS2_HOST_CODE_BRIDGE)) || PS2_EE_SOFTMEM_BLOCK_TAIL
inline constexpr size_t kEeSoftmemColdBodyCapacity = 8192;
inline constexpr size_t kEeSoftmemColdGenerationQuota = 256 * 1024;
inline constexpr size_t kEeSoftmemColdExitHeadroom = 128 * 1024;
inline constexpr size_t kEeSoftmemColdMaxOpsPerBlock = 32;

struct EeSoftmemColdEmitter
{
	void* ramContext;
	void (*emitRAM)(void*);
	void* mmioContext;
	void (*emitMMIO)(void*);
	size_t mmioContextBytes;
	u32 dirtyGprs;
	u32 dirtyNeon;
	bool tagged;
};

__attribute__((visibility("hidden"))) bool armCanEmitSoftmemCold(size_t hotTailBytes);
__attribute__((visibility("hidden"))) void armSetSoftmemColdHotTail(size_t bytes);
__attribute__((visibility("hidden"))) bool armSoftmemColdActive();
__attribute__((visibility("hidden"))) size_t armEmitSoftmemCold(
	u8* target, size_t capacity, u8* resume, void* context, void (*emitMMIO)(void*));
__attribute__((visibility("hidden"))) bool recTryEmitSoftmemCold(const EeSoftmemColdEmitter& emitter);
#endif

template <typename RAM, typename MMIO>
static bool eeSoftmemTryCold(RAM& emitRAM, MMIO& emitMMIO, u32 dirtyGprs, u32 dirtyNeon, bool tagged)
{
#if (PS2_EE_SOFTMEM_COLD_OOL && defined(PS2_HOST_CODE_BRIDGE)) || PS2_EE_SOFTMEM_BLOCK_TAIL
	static_assert(std::is_trivially_copyable_v<MMIO> && alignof(MMIO) <= alignof(std::max_align_t));
	return recTryEmitSoftmemCold({&emitRAM,
		[](void* p) { (*static_cast<RAM*>(p))(); }, &emitMMIO,
		[](void* p) { (*static_cast<MMIO*>(p))(); }, sizeof(MMIO), dirtyGprs, dirtyNeon, tagged});
#else
	(void)emitRAM;
	(void)emitMMIO;
	(void)dirtyGprs;
	(void)dirtyNeon;
	(void)tagged;
	return false;
#endif
}
