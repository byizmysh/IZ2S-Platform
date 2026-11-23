#ifdef PS2_HOST_CODE_BRIDGE
#include "code_host.h"
#include "ee_publication_transaction.h"
#include "ps2_mtvu_lifecycle_private.h"
#include <cstdlib>
#endif
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0

#include "arm64/AsmHelpers.h"
#include "EeSoftmemColdContext.h"

#include "common/Assertions.h"
#include "common/BitUtils.h"
#include "common/Console.h"
#include "common/HostSys.h"
#include "common/Timer.h"

#include <algorithm>

#ifdef __APPLE__
#include "common/Darwin/DarwinMisc.h"
#endif
#ifdef __SWITCH2__
#include "common/Switch2/CodeCache.h"
#endif

const vixl::aarch64::Register& armWRegister(int n)
{
	using namespace vixl::aarch64;
	static constexpr const Register* regs[32] = {&w0, &w1, &w2, &w3, &w4, &w5, &w6, &w7, &w8, &w9, &w10,
		&w11, &w12, &w13, &w14, &w15, &w16, &w17, &w18, &w19, &w20, &w21, &w22, &w23, &w24, &w25, &w26, &w27, &w28,
		&w29, &w30, &w31};
	pxAssert(static_cast<size_t>(n) < std::size(regs));
	return *regs[n];
}

const vixl::aarch64::Register& armXRegister(int n)
{
	using namespace vixl::aarch64;
	static constexpr const Register* regs[32] = {&x0, &x1, &x2, &x3, &x4, &x5, &x6, &x7, &x8, &x9, &x10,
		&x11, &x12, &x13, &x14, &x15, &x16, &x17, &x18, &x19, &x20, &x21, &x22, &x23, &x24, &x25, &x26, &x27, &x28,
		&x29, &x30, &x31};
	pxAssert(static_cast<size_t>(n) < std::size(regs));
	return *regs[n];
}

const vixl::aarch64::VRegister& armSRegister(int n)
{
	using namespace vixl::aarch64;
	static constexpr const VRegister* regs[32] = {&s0, &s1, &s2, &s3, &s4, &s5, &s6, &s7, &vixl::aarch64::s8, &s9, &s10,
		&s11, &s12, &s13, &s14, &s15, &vixl::aarch64::s16, &s17, &s18, &s19, &s20, &s21, &s22, &s23, &s24, &s25, &s26, &s27, &s28,
		&s29, &s30, &s31};
	pxAssert(static_cast<size_t>(n) < std::size(regs));
	return *regs[n];
}

const vixl::aarch64::VRegister& armDRegister(int n)
{
	using namespace vixl::aarch64;
	static constexpr const VRegister* regs[32] = {&d0, &d1, &d2, &d3, &d4, &d5, &d6, &d7, &d8, &d9, &d10,
		&d11, &d12, &d13, &d14, &d15, &d16, &d17, &d18, &d19, &d20, &d21, &d22, &d23, &d24, &d25, &d26, &d27, &d28,
		&d29, &d30, &d31};
	pxAssert(static_cast<size_t>(n) < std::size(regs));
	return *regs[n];
}

const vixl::aarch64::VRegister& armQRegister(int n)
{
	using namespace vixl::aarch64;
	static constexpr const VRegister* regs[32] = {&q0, &q1, &q2, &q3, &q4, &q5, &q6, &q7, &q8, &q9, &q10,
		&q11, &q12, &q13, &q14, &q15, &q16, &q17, &q18, &q19, &q20, &q21, &q22, &q23, &q24, &q25, &q26, &q27, &q28,
		&q29, &q30, &q31};
	pxAssert(static_cast<size_t>(n) < std::size(regs));
	return *regs[n];
}

// Opt-in only (matches origin/master): uncomment to compile vixl's
// PrintDisassembler/Decoder for armDisassembleAndDumpCode. Off by default so
// the disassembler TUs and statics don't ship in normal builds.
//#define INCLUDE_DISASSEMBLER

#ifdef INCLUDE_DISASSEMBLER
#include "vixl/aarch64/disasm-aarch64.h"
#endif

namespace a64 = vixl::aarch64;

thread_local a64::MacroAssembler* armAsm;
thread_local u8* armAsmPtr;
thread_local size_t armAsmCapacity;
thread_local ArmConstantPool* armConstantPool;
thread_local ArmAddressRecorder* armAddressRecorder;

#ifdef INCLUDE_DISASSEMBLER
static std::mutex armDisasmMutex;
static std::unique_ptr<a64::PrintDisassembler> armDisasm;
static std::unique_ptr<a64::Decoder> armDisasmDecoder;
#endif

void armSetAsmPtr(void* ptr, size_t capacity, ArmConstantPool* pool)
{
	pxAssert(!armAsm);
	armAsmPtr = static_cast<u8*>(ptr);
	armAsmCapacity = capacity;
	armConstantPool = pool;
}

// Align to 16 bytes, apparently ARM likes that.
void armAlignAsmPtr()
{
	static constexpr uintptr_t ALIGNMENT = 16;
	u8* new_ptr = reinterpret_cast<u8*>((reinterpret_cast<uintptr_t>(armAsmPtr) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1));
	pxAssert(static_cast<size_t>(new_ptr - armAsmPtr) <= armAsmCapacity);
	armAsmCapacity -= (new_ptr - armAsmPtr);
	armAsmPtr = new_ptr;
}

// Placement-new the per-block MacroAssembler into a thread_local buffer
// instead of heap new/delete per JIT block: one fewer alloc/free pair on
// every block compile, and it sidesteps Android scudo tag/header corruption
// seen on this exact pattern (ARMSX2, Tyler Bochard, 1c1d0b880). The
// pxAssert(!armAsm) single-active-per-thread invariant (armAsm itself is
// thread_local — MTVU compiles VU1 on its own thread) makes the single
// buffer safe. (AX-10)
#if PS2_EE_SOFTMEM_COLD_OOL && defined(PS2_HOST_CODE_BRIDGE)
static thread_local size_t s_arm_softmem_hot_tail_bytes = 0;
static void armGuardSoftmemPrimaryEmission(a64::MacroAssembler* masm, size_t amount);
class SoftmemPrimaryAssembler final : public a64::MacroAssembler
{
public:
	using MacroAssembler::MacroAssembler;
	void EnsureEmitPoolsFor(size_t amount) override
	{
		// EE only: IOP/VU and ordinary AsmHelpers sessions keep baseline checks.
		if (s_arm_softmem_hot_tail_bytes && armAsm == this)
			armGuardSoftmemPrimaryEmission(this, amount);
		MacroAssembler::EnsureEmitPoolsFor(amount);
	}
};
static_assert(sizeof(SoftmemPrimaryAssembler) == sizeof(a64::MacroAssembler));
static_assert(alignof(SoftmemPrimaryAssembler) == alignof(a64::MacroAssembler));
#endif
alignas(vixl::aarch64::MacroAssembler) static thread_local u8 s_armAsmStorage[sizeof(vixl::aarch64::MacroAssembler)];

// iOS W^X (ported from ARMSX2 master's aR* recompilers): under the iOS 26
// dual-map JIT modes (DarwinMisc::JitMode::LuckTXM / LuckNoTXM) the code
// region is mapped twice — RX at the address we execute and hand out, RW at
// rx + g_code_rw_offset. Every byte written into the region must go through
// the RW alias; all displacement math, recorded pointers, and icache flushes
// stay in RX space. On Legacy-mode iOS and on macOS/Simulator the offset is 0
// and this is an identity (writability there comes from the mprotect toggle /
// pthread_jit_write_protect_np inside Begin/EndCodeWrite[Range]). Gated on
// __APPLE__ rather than TARGET_OS_IPHONE so a forced dual-map on macOS
// (ARMSX2_FORCE_DUAL_MAP=1, CI-only) exercises the alias paths; production
// macOS always has offset 0, so behavior there is unchanged.
u8* armGetWritableCodePtr(u8* rx_ptr)
{
#ifdef PS2_HOST_CODE_BRIDGE
	if (ps2_code_host_bound())
	{
		auto* const shadow = static_cast<u8*>(ps2_code_host_translate(rx_ptr, 1));
		if (!shadow)
			std::abort();
		return shadow;
	}
#endif
#ifdef __SWITCH2__
	return static_cast<u8*>(HostSys::GetWritableCodeAddress(rx_ptr));
#elif defined(__APPLE__)
	// The alias translation applies only to addresses inside the dual-mapped
	// JIT region (SetJitRange = the MmapCodeDualMap arena). Anything else —
	// the Arm64BaseBlocks link tests patch static buffers, for instance —
	// writes untranslated. GetJitBase() is 0 when no region exists, which
	// makes the range test fail and this an identity.
	const uintptr_t p = reinterpret_cast<uintptr_t>(rx_ptr);
	if (DarwinMisc::g_code_rw_offset != 0 && p >= DarwinMisc::GetJitBase() && p < DarwinMisc::GetJitEnd())
		return rx_ptr + DarwinMisc::g_code_rw_offset;
	return rx_ptr;
#else
	return rx_ptr;
#endif
}

// Write-window bookkeeping for iOS Legacy mode (mprotect-toggle W^X):
// BeginCodeWriteRange flips only [start, start+size) to RW so the rest of the
// region — including the JIT frames we will return into — stays executable.
// A no-op on every other platform/mode. Window size mirrors ARMSX2 master.
static thread_local u8* s_arm_block_start = nullptr;
static thread_local size_t s_arm_block_write_size = 0;
static constexpr size_t ARM64_CODE_WRITE_WINDOW = 1024 * 1024;

// Explicit pool append batch, only for the currently emitting block's pool.
// Existing nested old-code patches still publish immediately. No public class
// layout/API changes: these fields belong only to this emitting thread.
#ifdef PS2_SWITCH2_POOL_BATCH
#if !defined(__SWITCH2__) || defined(PS2_HOST_CODE_BRIDGE)
#error This pool publication path requires the Switch 2 dual-alias provider
#endif
static thread_local bool s_batchPoolPublication = true;
void armSetPoolPublicationBatchForTest(bool enabled)
{
	s_batchPoolPublication = enabled;
}
#endif
#if defined(PS2_HOST_CODE_BRIDGE) || defined(PS2_SWITCH2_POOL_BATCH)
static thread_local struct {
 ArmConstantPool* owner;
 u8* start;
 size_t capacity;
 size_t used;
} s_arm_pool_batch{};
#endif
static void armRequirePoolIdle(const ArmConstantPool* pool)
{
#if defined(PS2_HOST_CODE_BRIDGE) || defined(PS2_SWITCH2_POOL_BATCH)
 if (s_arm_pool_batch.owner == pool)
  std::abort();
#endif
}
static bool armBeginPoolAppend(ArmConstantPool* pool, u8* ptr, size_t window, size_t available)
{
#if defined(PS2_HOST_CODE_BRIDGE) || defined(PS2_SWITCH2_POOL_BATCH)
#ifdef PS2_HOST_CODE_BRIDGE
 const bool batch = ps2_code_host_bound();
#else
 const bool batch = s_batchPoolPublication;
#endif
 if (batch && armAsm && pool == armConstantPool && window)
 {
  if (!s_arm_pool_batch.owner)
  {
   if (!available)
    std::abort();
   HostSys::BeginCodeWriteRange(ptr, available);
   s_arm_pool_batch = {pool, ptr, available, 0};
  }
  if (s_arm_pool_batch.owner != pool || ptr < s_arm_pool_batch.start ||
      static_cast<size_t>(ptr - s_arm_pool_batch.start) >= s_arm_pool_batch.capacity)
   std::abort();
  return true;
 }
#endif
 HostSys::BeginCodeWriteRange(ptr, window);
 return false;
}
static void armEndPoolAppend(ArmConstantPool* pool, u8* ptr, size_t bytes, bool batched)
{
#if defined(PS2_HOST_CODE_BRIDGE) || defined(PS2_SWITCH2_POOL_BATCH)
 if (batched)
 {
  if (s_arm_pool_batch.owner != pool || !bytes || ptr < s_arm_pool_batch.start)
   std::abort();
  const size_t offset = static_cast<size_t>(ptr - s_arm_pool_batch.start);
  if (offset >= s_arm_pool_batch.capacity || bytes > s_arm_pool_batch.capacity - offset)
   std::abort();
  s_arm_pool_batch.used = std::max(s_arm_pool_batch.used, offset + bytes);
  return;
 }
#endif
 HostSys::EndCodeWriteRange(ptr, bytes);
#ifdef __SWITCH2__
 HostSys::FlushInstructionCache(ptr, static_cast<u32>(bytes));
#endif
}
static void armFinishPoolAppendBatch()
{
#if defined(PS2_HOST_CODE_BRIDGE) || defined(PS2_SWITCH2_POOL_BATCH)
 if (s_arm_pool_batch.owner)
 {
  if (s_arm_pool_batch.owner != armConstantPool || !s_arm_pool_batch.used)
   std::abort();
  // The pool scope is nested inside the main block, but outside any temporary
  // patch scope. Publish pool data/trampolines before publishing their consumer.
  HostSys::EndCodeWriteRange(s_arm_pool_batch.start, s_arm_pool_batch.used);
#ifdef PS2_SWITCH2_POOL_BATCH
  HostSys::FlushInstructionCache(s_arm_pool_batch.start, static_cast<u32>(s_arm_pool_batch.used));
#endif
  s_arm_pool_batch = {};
 }
#endif
}

#if PS2_EE_SOFTMEM_COLD_OOL && defined(PS2_HOST_CODE_BRIDGE)
static thread_local bool s_arm_softmem_cold_active = false;

static void armGuardSoftmemPrimaryEmission(a64::MacroAssembler* masm, size_t amount)
{
	const size_t remaining = masm->GetBuffer()->GetRemainingBytes();
	const size_t pools = masm->GetLiteralPoolMaxSize() + masm->GetVeneerPoolMaxSize();
	if (!ps2_code_host_ee_transaction_active() || amount > remaining ||
		pools > remaining - amount || s_arm_softmem_hot_tail_bytes > remaining - amount - pools)
		ps2_mtvu_retain_owner("softmem primary reserved tail bound", __LINE__);
}

void armSetSoftmemColdHotTail(size_t bytes)
{
	if (bytes > kEeSoftmemColdMaxOpsPerBlock * 4 || s_arm_softmem_cold_active)
		ps2_mtvu_retain_owner("softmem primary invalid tail", __LINE__);
	const size_t previous = s_arm_softmem_hot_tail_bytes;
	s_arm_softmem_hot_tail_bytes = std::max(bytes, previous);
	if (s_arm_softmem_hot_tail_bytes) armGuardSoftmemPrimaryEmission(armAsm, 0);
	s_arm_softmem_hot_tail_bytes = bytes;
}

bool armSoftmemColdActive()
{
	return s_arm_softmem_cold_active;
}

bool armCanEmitSoftmemCold(size_t hotTailBytes)
{
	if (!ps2_code_host_bound() || !ps2_code_host_ee_transaction_active() ||
		!armAsm || s_arm_softmem_cold_active || armAddressRecorder ||
		!s_arm_block_start || s_arm_block_write_size > ARM64_CODE_WRITE_WINDOW ||
		armAsm->GetBuffer()->GetCapacity() != s_arm_block_write_size)
		return false;
#ifdef VIXL_DEBUG
	// This VIXL diagnostic flag is initialized only in debug builds.
	if (!armAsm->AllowMacroInstructions()) return false;
#endif
	const size_t used = armAsm->GetSizeOfCodeGenerated();
	const size_t pools = armAsm->GetLiteralPoolMaxSize() + armAsm->GetVeneerPoolMaxSize();
	// Every near label is bound in this same <=1 MiB primary write window.
	return used <= s_arm_block_write_size && pools <= s_arm_block_write_size - used &&
		hotTailBytes <= s_arm_block_write_size - used - pools;
}

namespace {
class SoftmemColdAssembler final : public a64::MacroAssembler
{
public:
	SoftmemColdAssembler(vixl::byte* ptr, size_t size) : MacroAssembler(ptr, size) {}
	void EnsureEmitPoolsFor(size_t amount) override
	{
		// EmissionCheckScope reaches this before EnsureSpaceFor or any write.
		// An unmanaged VIXL buffer must never reach its Grow() implementation.
		if (amount > GetBuffer()->GetRemainingBytes() ||
			!GetLiteralPool()->IsEmpty() || GetNumberOfPotentialVeneers() != 0)
			ps2_mtvu_retain_owner("softmem cold emission bound/pool", __LINE__);
		MacroAssembler::EnsureEmitPoolsFor(amount);
	}
};
} // anonymous namespace

size_t armEmitSoftmemCold(u8* target, size_t capacity, u8* resume,
	void* context, void (*emitMMIO)(void*))
{
	if (!armCanEmitSoftmemCold(4) || !target || !resume || !emitMMIO ||
		capacity != kEeSoftmemColdBodyCapacity || (reinterpret_cast<uptr>(target) & 15))
		ps2_mtvu_retain_owner("softmem cold invalid admission", __LINE__);

	// The primary placement object and its label/literal/scratch scopes stay
	// alive. Only the thread's current-emitter view is temporarily replaced.
	a64::MacroAssembler* const primary = armAsm;
	u8* const primaryPtr = armAsmPtr;
	const size_t primaryCapacity = armAsmCapacity;
	ArmConstantPool* const primaryPool = armConstantPool;
	ArmAddressRecorder* const primaryRecorder = armAddressRecorder;
	u8* const primaryWindow = s_arm_block_start;
	const size_t primaryWindowSize = s_arm_block_write_size;
	const auto primaryBatch = s_arm_pool_batch;
	const size_t primaryTail = s_arm_softmem_hot_tail_bytes;
	size_t emitted = 0;
	HostSys::BeginCodeWriteRange(target, capacity); // LIFO above any hot pool batch
	{
		SoftmemColdAssembler isolated(static_cast<vixl::byte*>(armGetWritableCodePtr(target)), capacity);
		*isolated.GetScratchRegisterList() = *primary->GetScratchRegisterList();
		*isolated.GetScratchVRegisterList() = *primary->GetScratchVRegisterList();
		*isolated.GetScratchPRegisterList() = *primary->GetScratchPRegisterList();
		isolated.SetStackPointer(primary->StackPointer());
		isolated.SetCPUFeatures(*primary->GetCPUFeatures());
		isolated.SetGenerateSimulatorCode(primary->GenerateSimulatorCode());
		// The outer scope belongs to primary. The copied available lists are
		// authoritative; isolated inner scratch scopes must have their own chain.
		isolated.SetCurrentScratchRegisterScope(nullptr);
		armAsm = &isolated;
		armAsmPtr = target;
		armAsmCapacity = capacity;
		armConstantPool = nullptr;
		armAddressRecorder = nullptr;
		s_arm_block_start = target;
		s_arm_block_write_size = capacity;
		s_arm_pool_batch = {};
		s_arm_softmem_cold_active = true;
		{
			a64::BlockPoolsScope noPools(&isolated);
			emitMMIO(context); // immediate: the exact current allocator is live
			const ptrdiff_t delta = resume - armGetCurrentCodePointer();
			if ((delta & 3) || delta <= -(ptrdiff_t{1} << 27) || delta >= (ptrdiff_t{1} << 27))
				ps2_mtvu_retain_owner("softmem cold return range", __LINE__);
			a64::SingleEmissionCheckScope one(&isolated);
			isolated.b(delta >> 2);
		}
		if (!isolated.GetLiteralPool()->IsEmpty() || isolated.GetNumberOfPotentialVeneers() ||
			isolated.GetCurrentScratchRegisterScope() || s_arm_pool_batch.owner)
			ps2_mtvu_retain_owner("softmem cold unclosed context", __LINE__);
		isolated.FinalizeCode();
		emitted = isolated.GetSizeOfCodeGenerated();
		if (!emitted || emitted > capacity)
			ps2_mtvu_retain_owner("softmem cold final size", __LINE__);
	}
	// Close the nested cold scope before restoring the pending primary batch.
	HostSys::EndCodeWriteRange(target, emitted);
	HostSys::FlushInstructionCache(target, emitted); // deferred in same EE transaction
	armAsm = primary;
	armAsmPtr = primaryPtr;
	armAsmCapacity = primaryCapacity;
	armConstantPool = primaryPool;
	armAddressRecorder = primaryRecorder;
	s_arm_block_start = primaryWindow;
	s_arm_block_write_size = primaryWindowSize;
	s_arm_pool_batch = primaryBatch;
	s_arm_softmem_hot_tail_bytes = primaryTail;
	s_arm_softmem_cold_active = false;
	return emitted;
}
#endif


u8* armStartBlock()
{
#if defined(PS2_HOST_CODE_BRIDGE) || defined(PS2_SWITCH2_POOL_BATCH)
 if (s_arm_pool_batch.owner) std::abort();
#endif
	armAlignAsmPtr();

	s_arm_block_start = armAsmPtr;
	s_arm_block_write_size = (armAsmCapacity < ARM64_CODE_WRITE_WINDOW) ? armAsmCapacity : ARM64_CODE_WRITE_WINDOW;
	HostSys::BeginCodeWriteRange(s_arm_block_start, s_arm_block_write_size);

	pxAssert(!armAsm);
	// The MacroAssembler's buffer is the WRITABLE alias; armAsmPtr stays the
	// RX address, so armGetCurrentCodePointer() (= armAsmPtr + cursor) and
	// every displacement computed from it remain in execute space.
#if PS2_EE_SOFTMEM_COLD_OOL && defined(PS2_HOST_CODE_BRIDGE)
	armAsm = new (s_armAsmStorage) SoftmemPrimaryAssembler(
#else
	armAsm = new (s_armAsmStorage) vixl::aarch64::MacroAssembler(
#endif
		static_cast<vixl::byte*>(armGetWritableCodePtr(armAsmPtr)),
#ifdef PS2_HOST_CODE_BRIDGE
		ps2_code_host_bound() ? s_arm_block_write_size :
#endif
		armAsmCapacity);
	armAsm->GetScratchVRegisterList()->Remove(31);
	armAsm->GetScratchRegisterList()->Remove(RSCRATCHADDR.GetCode());
	return armAsmPtr;
}

static u8* armFinalizeBlock(bool publish)
{
	pxAssert(armAsm);

	armAsm->FinalizeCode();

	const u32 size = static_cast<u32>(armAsm->GetSizeOfCodeGenerated());
	pxAssert(size < armAsmCapacity);

#if PS2_EE_SOFTMEM_COLD_OOL && defined(PS2_HOST_CODE_BRIDGE)
	if (s_arm_softmem_hot_tail_bytes)
		ps2_mtvu_retain_owner("softmem primary unbound tail", __LINE__);
	static_cast<SoftmemPrimaryAssembler*>(armAsm)->~SoftmemPrimaryAssembler();
#else
	armAsm->~MacroAssembler();
#endif
	armAsm = nullptr;

 armFinishPoolAppendBatch();

#ifdef PS2_HOST_CODE_BRIDGE
	HostSys::EndCodeWriteRange(s_arm_block_start, ps2_code_host_bound() ? std::max<size_t>(size, 1) : s_arm_block_write_size);
#else
	HostSys::EndCodeWriteRange(s_arm_block_start, s_arm_block_write_size);
#endif

	if (publish)
		HostSys::FlushInstructionCache(armAsmPtr, size);

	s_arm_block_start = nullptr;
	s_arm_block_write_size = 0;
	armAsmPtr = armAsmPtr + size;
	armAsmCapacity -= size;
	return armAsmPtr;
}

u8* armEndBlock()
{
	return armFinalizeBlock(true);
}

#if defined(PS2_EE_DEFER_HOT_PUBLICATION)
u8* armEndBlockUnpublished()
{
	return armFinalizeBlock(false);
}
#endif

// Patch one instruction word at `site`, handling W^X itself instead of
// trusting the caller to hold a write scope — link sites in earlier blocks
// sit in windows armStartBlock never opened, which is a SIGBUS on iOS
// Legacy mode. Pages inside the open emit window are stored to directly
// (the previous block's link site often shares a page with the current
// block's start, and RX-flipping that mid-emit would kill the compile);
// anything else gets its own page-granular Begin/EndCodeWriteRange.
// Aligned 4-byte stores are atomic on AArch64, and mprotect is fine from
// the Mach exception-handler thread, so the fastmem-fault path can use it.
void armPatchCodeWord(void* site, u32 instr)
{
	u8* const rx_site = static_cast<u8*>(site);

	bool in_open_window = false;
	if (s_arm_block_start)
	{
		static const uintptr_t page_mask = []() {
			size_t page_size = HostSys::GetRuntimePageSize();
			if (page_size == 0)
				page_size = 4096;
			return ~(static_cast<uintptr_t>(page_size) - 1);
		}();

		const uintptr_t site_page = reinterpret_cast<uintptr_t>(rx_site) & page_mask;
		const uintptr_t first_page = reinterpret_cast<uintptr_t>(s_arm_block_start) & page_mask;
		const uintptr_t last_page =
			(reinterpret_cast<uintptr_t>(s_arm_block_start) + s_arm_block_write_size - 1) & page_mask;
		in_open_window = (site_page >= first_page && site_page <= last_page);
	}

	if (!in_open_window)
		HostSys::BeginCodeWriteRange(rx_site, sizeof(u32));
#ifdef PS2_HOST_CODE_BRIDGE
    // Admit/queue before store even inside the open window: End may publish
    // only a smaller emitted prefix, while this patch changes an older word.
    if(ps2_code_host_bound())ps2_ee_publication_note_patch(rx_site,sizeof(u32));
#endif
	*reinterpret_cast<volatile u32*>(armGetWritableCodePtr(rx_site)) = instr;
	if (!in_open_window)
		HostSys::EndCodeWriteRange(rx_site, sizeof(u32));
}

// Granule for the cache-maintenance ops, from CTR_EL0: the smaller of IminLine
// (bits 3:0) and DminLine (bits 19:16), each log2 of a count of 4-byte words.
// The smaller one lets a single address list serve both the D and I loops; a
// granule finer than the real line only issues a redundant CMO. This is not
// HostSys::GetRuntimeCacheLineSize(), which reports the largest data line for
// false-sharing padding; using that here would skip lines.
//
// Darwin: XNU does not expose CTR_EL0 to EL0, so the read is an illegal
// instruction. The Darwin flusher only builds ranges for sys_icache_invalidate,
// so any granule covering the patched word works; 64 is the smallest line an
// Apple core has shipped with.
static size_t armCodePatchGranule()
{
#ifdef __APPLE__
	return 64;
#else
	static const size_t granule = []() -> size_t {
		u64 ctr = 0;
		__asm__ __volatile__("mrs %0, ctr_el0" : "=r"(ctr));
		const size_t dline = static_cast<size_t>(4) << ((ctr >> 16) & 0xf);
		const size_t iline = static_cast<size_t>(4) << (ctr & 0xf);
		return std::min(dline, iline);
	}();
	return granule;
#endif
}

void ArmCodePatchFlusher::Note(const void* site)
{
	const uintptr_t line = reinterpret_cast<uintptr_t>(site) & ~(armCodePatchGranule() - 1);
	// Sites arrive in increasing code order within an arena, and the ones a
	// single block emits sit a few instructions apart, so checking only the
	// previous entry collapses most of the repeats. A duplicate that slips
	// through costs one redundant CMO pair, not correctness.
	if (!m_lines.empty() && m_lines.back() == line)
		return;
	m_lines.push_back(line);
}

void ArmCodePatchFlusher::Finish()
{
	if (m_lines.empty())
		return;
#ifdef PS2_HOST_CODE_BRIDGE
    if(ps2_code_host_ee_transaction_active()) {
        // Route raw dc/ic through the same deferred flush admission too.
        for(const uintptr_t line:m_lines)
            HostSys::FlushInstructionCache(reinterpret_cast<void*>(line),
                                          static_cast<u32>(armCodePatchGranule()));
        m_lines.clear();
        return;
    }
#endif

#if defined(__APPLE__) || defined(__SWITCH2__)
	// Use the platform publication hook for each run of touched cache lines.
	// A call also covers the gaps inside the run, hence the page-sized gap cap.
	std::sort(m_lines.begin(), m_lines.end());
	const size_t granule = armCodePatchGranule();
	constexpr uintptr_t kMaxGap = 4096;

	uintptr_t lo = m_lines[0];
	uintptr_t hi = m_lines[0] + granule;
	for (size_t i = 1; i < m_lines.size(); i++)
	{
		if (m_lines[i] <= hi || m_lines[i] - hi <= kMaxGap)
		{
			hi = m_lines[i] + granule;
			continue;
		}
		HostSys::FlushInstructionCache(reinterpret_cast<void*>(lo), static_cast<u32>(hi - lo));
		lo = m_lines[i];
		hi = m_lines[i] + granule;
	}
	HostSys::FlushInstructionCache(reinterpret_cast<void*>(lo), static_cast<u32>(hi - lo));
#else
	// The `dsb ish` between the two loops is required: the clean has to
	// complete before the invalidate.
	for (const uintptr_t line : m_lines)
		__asm__ __volatile__("dc cvau, %0" : : "r"(line) : "memory");
	__asm__ __volatile__("dsb ish" : : : "memory");
	for (const uintptr_t line : m_lines)
		__asm__ __volatile__("ic ivau, %0" : : "r"(line) : "memory");
	__asm__ __volatile__("dsb ish\n\tisb" : : : "memory");
#endif

	m_lines.clear();
}

void armDisassembleAndDumpCode(const void* ptr, size_t size)
{
#ifdef INCLUDE_DISASSEMBLER
	std::unique_lock lock(armDisasmMutex);
	if (!armDisasm)
	{
		std::FILE* logFile = Log::GetFileLogHandle();
		armDisasm = std::make_unique<a64::PrintDisassembler>(logFile ? logFile : stderr);
		armDisasmDecoder = std::make_unique<a64::Decoder>();
		armDisasmDecoder->AppendVisitor(armDisasm.get());
	}

	const auto* start = reinterpret_cast<const vixl::aarch64::Instruction*>(ptr);
	const auto* end = reinterpret_cast<const vixl::aarch64::Instruction*>(static_cast<const u8*>(ptr) + size);
	armDisasmDecoder->Decode(start, end);
#else
	Console.Error("Not compiled with INCLUDE_DISASSEMBLER");
#endif
}

u32 armMovRunWidth(u64 value)
{
	u32 width = 1;
	for (u32 hw = 1; hw < 4; hw++)
	{
		if ((value >> (16 * hw)) & 0xFFFFu)
			width = hw + 1;
	}
	return width;
}

void armEmitMovRun(const vixl::aarch64::Register& reg, u64 value, u32 width)
{
	pxAssert(reg.IsX() && width >= 1 && width <= 4);
	pxAssert(width == 4 || (value >> (16 * width)) == 0);
	vixl::ExactAssemblyScope guard(armAsm, width * 4);
	armAsm->movz(reg, value & 0xFFFF, 0);
	for (u32 hw = 1; hw < width; hw++)
		armAsm->movk(reg, (value >> (16 * hw)) & 0xFFFF, 16 * hw);
}

// Recorder path for an address materialized as an immediate. Returns false
// when the recorder cannot relocate it (it has then failed the episode) and
// the caller should emit its default form.
static bool armEmitRecordedMovRun(const vixl::aarch64::Register& reg, const void* addr)
{
	const ArmAddressRecorder::AddressClass c = armAddressRecorder->ClassifyAddress(addr);
	if (!c.relocatable)
		return false;
	const u64 v = reinterpret_cast<uintptr_t>(addr);
	const u32 width = std::max(c.minMovWidth, armMovRunWidth(v));
	armEmitMovRun(reg, v, width);
	armAddressRecorder->OnMovRun(armGetCurrentCodePointer() - width * 4, width, addr);
	return true;
}

void armEmitJmp(const void* ptr, bool force_inline)
{
	// VIXL may insert a literal or veneer pool before the instruction.
	armAsm->EnsureEmitPoolsFor(a64::kInstructionSize);
	s64 displacement = GetPCDisplacement(armGetCurrentCodePointer(), ptr);
	bool use_blr = !vixl::IsInt26(displacement);
	if (use_blr && armConstantPool && !force_inline)
	{
		if (u8* trampoline = armConstantPool->GetJumpTrampoline(ptr); trampoline)
		{
			displacement = GetPCDisplacement(armGetCurrentCodePointer(), trampoline);
			use_blr = !vixl::IsInt26(displacement);
		}
	}

	if (use_blr)
	{
		if (!armAddressRecorder || !armEmitRecordedMovRun(RXVIXLSCRATCH, ptr))
			armAsm->Mov(RXVIXLSCRATCH, reinterpret_cast<uintptr_t>(ptr));
		armAsm->Br(RXVIXLSCRATCH);
	}
	else
	{
		{
			a64::SingleEmissionCheckScope guard(armAsm);
			armAsm->b(displacement);
		}
		// Record after emission: the scope entry may flush a pending vixl
		// literal pool, so the insn address is only known once it's out.
		if (armAddressRecorder)
			armAddressRecorder->OnBranch(armGetCurrentCodePointer() - 4, ptr, ArmAddressRecorder::BranchForm::Rel26);
	}
}

void armEmitJmpPtr(void* code_address, const void* target, bool flush_icache)
{
	// Same single-word B rewrite + cache maintenance protocol as
	// Arm64BaseBlocks::PatchAtomic / recPatchIslandB: a 4-byte aligned word
	// store is atomic on AArch64, so concurrent execution of the old branch
	// is safe.
	const intptr_t off = reinterpret_cast<intptr_t>(target) - reinterpret_cast<intptr_t>(code_address);
	pxAssertRel((off & 3) == 0, "armEmitJmpPtr: branch offset not 4-byte aligned");
	const intptr_t imm26 = off >> 2;
	pxAssertRel(imm26 >= -(1 << 25) && imm26 < (1 << 25), "armEmitJmpPtr: branch offset out of B imm26 range");
	armPatchCodeWord(code_address, 0x14000000u | (static_cast<u32>(imm26) & 0x03FFFFFFu));
	if (flush_icache)
		HostSys::FlushInstructionCache(code_address, 4);
}

void armEmitCall(const void* ptr, bool force_inline)
{
	// VIXL may insert a literal or veneer pool before the instruction.
	armAsm->EnsureEmitPoolsFor(a64::kInstructionSize);
	s64 displacement = GetPCDisplacement(armGetCurrentCodePointer(), ptr);
	bool use_blr = !vixl::IsInt26(displacement);
	if (use_blr && armConstantPool && !force_inline)
	{
		if (u8* trampoline = armConstantPool->GetJumpTrampoline(ptr); trampoline)
		{
			displacement = GetPCDisplacement(armGetCurrentCodePointer(), trampoline);
			use_blr = !vixl::IsInt26(displacement);
		}
	}

	if (use_blr)
	{
		if (!armAddressRecorder || !armEmitRecordedMovRun(RXVIXLSCRATCH, ptr))
			armAsm->Mov(RXVIXLSCRATCH, reinterpret_cast<uintptr_t>(ptr));
		armAsm->Blr(RXVIXLSCRATCH);
	}
	else
	{
		{
			a64::SingleEmissionCheckScope guard(armAsm);
			armAsm->bl(displacement);
		}
		if (armAddressRecorder)
			armAddressRecorder->OnBranch(armGetCurrentCodePointer() - 4, ptr, ArmAddressRecorder::BranchForm::Rel26);
	}
}

void armEmitCbnz(const vixl::aarch64::Register& reg, const void* ptr)
{
	// VIXL may insert a literal or veneer pool before the instruction.
	armAsm->EnsureEmitPoolsFor(a64::kInstructionSize);
	const s64 jump_distance =
		static_cast<s64>(reinterpret_cast<intptr_t>(ptr) - reinterpret_cast<intptr_t>(armGetCurrentCodePointer()));
	//pxAssert(Common::IsAligned(jump_distance, 4));
	if (a64::Instruction::IsValidImmPCOffset(a64::CompareBranchType, jump_distance >> 2))
	{
		{
			a64::SingleEmissionCheckScope guard(armAsm);
			armAsm->cbnz(reg, jump_distance >> 2);
		}
		if (armAddressRecorder)
			armAddressRecorder->OnBranch(armGetCurrentCodePointer() - 4, ptr, ArmAddressRecorder::BranchForm::Rel19);
	}
	else
	{
		a64::MacroEmissionCheckScope guard(armAsm);
		a64::Label branch_not_taken;
		armAsm->cbz(reg, &branch_not_taken);

		const s64 new_jump_distance =
			static_cast<s64>(reinterpret_cast<intptr_t>(ptr) - reinterpret_cast<intptr_t>(armGetCurrentCodePointer()));
		armAsm->b(new_jump_distance >> 2);
		if (armAddressRecorder)
			armAddressRecorder->OnBranch(armGetCurrentCodePointer() - 4, ptr, ArmAddressRecorder::BranchForm::Rel26);
		armAsm->bind(&branch_not_taken);
	}
}

void armEmitCondBranch(a64::Condition cond, const void* ptr)
{
	// VIXL may insert a literal or veneer pool before the instruction.
	armAsm->EnsureEmitPoolsFor(a64::kInstructionSize);
	const s64 jump_distance =
		static_cast<s64>(reinterpret_cast<intptr_t>(ptr) - reinterpret_cast<intptr_t>(armGetCurrentCodePointer()));
	//pxAssert(Common::IsAligned(jump_distance, 4));

	// A recorded B.cond keeps its short form: the recorder logs it and the
	// hydrator re-checks the ±1MB reach at the new placement.
	if (a64::Instruction::IsValidImmPCOffset(a64::CondBranchType, jump_distance >> 2))
	{
		{
			a64::SingleEmissionCheckScope guard(armAsm);
			armAsm->b(jump_distance >> 2, cond);
		}
		if (armAddressRecorder)
			armAddressRecorder->OnBranch(armGetCurrentCodePointer() - 4, ptr, ArmAddressRecorder::BranchForm::Rel19);
	}
	else
	{
		a64::MacroEmissionCheckScope guard(armAsm);
		a64::Label branch_not_taken;
		armAsm->b(&branch_not_taken, a64::InvertCondition(cond));

		const s64 new_jump_distance =
			static_cast<s64>(reinterpret_cast<intptr_t>(ptr) - reinterpret_cast<intptr_t>(armGetCurrentCodePointer()));
		armAsm->b(new_jump_distance >> 2);
		if (armAddressRecorder)
			armAddressRecorder->OnBranch(armGetCurrentCodePointer() - 4, ptr, ArmAddressRecorder::BranchForm::Rel26);
		armAsm->bind(&branch_not_taken);
	}
}

void armMoveAddressToReg(const vixl::aarch64::Register& reg, const void* addr)
{
	// psxAsm->Mov(reg, static_cast<u64>(reinterpret_cast<uintptr_t>(addr)));
	pxAssert(reg.IsX());

	// A recorded address that ADRP may not carry (a heap object: its low 12
	// bits move too) goes straight to the patchable MOVZ/MOVK run.
	if (armAddressRecorder)
	{
		const ArmAddressRecorder::AddressClass c = armAddressRecorder->ClassifyAddress(addr);
		if (c.relocatable && !c.allowAdrp)
		{
			armEmitRecordedMovRun(reg, addr);
			return;
		}
	}

	// A pending pool can move ADRP onto a different 4 KiB code page.
	armAsm->EnsureEmitPoolsFor(a64::kInstructionSize);
	const void* current_code_ptr_page = reinterpret_cast<const void*>(
		reinterpret_cast<uintptr_t>(armGetCurrentCodePointer()) & ~static_cast<uintptr_t>(0xFFF));
	const void* ptr_page =
		reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(addr) & ~static_cast<uintptr_t>(0xFFF));
	const s64 page_displacement = GetPCDisplacement(current_code_ptr_page, ptr_page) >> 10;
	const u32 page_offset = static_cast<u32>(reinterpret_cast<uintptr_t>(addr) & 0xFFFu);
	if (vixl::IsInt21(page_displacement) && a64::Assembler::IsImmAddSub(page_offset))
	{
		{
			a64::SingleEmissionCheckScope guard(armAsm);
			armAsm->adrp(reg, page_displacement);
		}
		if (armAddressRecorder)
			armAddressRecorder->OnAdrp(armGetCurrentCodePointer() - 4, addr);
		armAsm->Add(reg, reg, page_offset);
	}
	else if (vixl::IsInt21(page_displacement) && a64::Assembler::IsImmLogical(page_offset, 64))
	{
		{
			a64::SingleEmissionCheckScope guard(armAsm);
			armAsm->adrp(reg, page_displacement);
		}
		if (armAddressRecorder)
			armAddressRecorder->OnAdrp(armGetCurrentCodePointer() - 4, addr);
		armAsm->Orr(reg, reg, page_offset);
	}
	else
	{
		if (!armAddressRecorder || !armEmitRecordedMovRun(reg, addr))
			armAsm->Mov(reg, reinterpret_cast<uintptr_t>(addr));
	}
}

void armLoadPtr(const vixl::aarch64::CPURegister& reg, const void* addr)
{
	armMoveAddressToReg(RSCRATCHADDR, addr);
	armAsm->Ldr(reg, a64::MemOperand(RSCRATCHADDR));
}

void armStorePtr(const vixl::aarch64::CPURegister& reg, const void* addr)
{
	armMoveAddressToReg(RSCRATCHADDR, addr);
	armAsm->Str(reg, a64::MemOperand(RSCRATCHADDR));
}

void armBeginStackFrame(bool save_fpr)
{
	// save x19 through x28, x29 could also be used
	armAsm->Sub(a64::sp, a64::sp, save_fpr ? 192 : 144);
	armAsm->Stp(a64::x19, a64::x20, a64::MemOperand(a64::sp, 32));
	armAsm->Stp(a64::x21, a64::x22, a64::MemOperand(a64::sp, 48));
	armAsm->Stp(a64::x23, a64::x24, a64::MemOperand(a64::sp, 64));
	armAsm->Stp(a64::x25, a64::x26, a64::MemOperand(a64::sp, 80));
	armAsm->Stp(a64::x27, a64::x28, a64::MemOperand(a64::sp, 96));
	armAsm->Stp(a64::x29, a64::lr, a64::MemOperand(a64::sp, 112));
	if (save_fpr)
	{
		armAsm->Stp(a64::d8, a64::d9, a64::MemOperand(a64::sp, 128));
		armAsm->Stp(a64::d10, a64::d11, a64::MemOperand(a64::sp, 144));
		armAsm->Stp(a64::d12, a64::d13, a64::MemOperand(a64::sp, 160));
		armAsm->Stp(a64::d14, a64::d15, a64::MemOperand(a64::sp, 176));
	}
}

void armEndStackFrame(bool save_fpr)
{
	if (save_fpr)
	{
		armAsm->Ldp(a64::d14, a64::d15, a64::MemOperand(a64::sp, 176));
		armAsm->Ldp(a64::d12, a64::d13, a64::MemOperand(a64::sp, 160));
		armAsm->Ldp(a64::d10, a64::d11, a64::MemOperand(a64::sp, 144));
		armAsm->Ldp(a64::d8, a64::d9, a64::MemOperand(a64::sp, 128));
	}
	armAsm->Ldp(a64::x29, a64::lr, a64::MemOperand(a64::sp, 112));
	armAsm->Ldp(a64::x27, a64::x28, a64::MemOperand(a64::sp, 96));
	armAsm->Ldp(a64::x25, a64::x26, a64::MemOperand(a64::sp, 80));
	armAsm->Ldp(a64::x23, a64::x24, a64::MemOperand(a64::sp, 64));
	armAsm->Ldp(a64::x21, a64::x22, a64::MemOperand(a64::sp, 48));
	armAsm->Ldp(a64::x19, a64::x20, a64::MemOperand(a64::sp, 32));
	armAsm->Add(a64::sp, a64::sp, save_fpr ? 192 : 144);
}

bool armIsCalleeSavedRegister(int reg)
{
	// same on both linux and windows
	return (reg >= 19);
}

vixl::aarch64::MemOperand armOffsetMemOperand(const vixl::aarch64::MemOperand& op, s64 offset)
{
	pxAssert(op.GetBaseRegister().IsValid() && op.GetAddrMode() == vixl::aarch64::Offset && op.GetShift() == vixl::aarch64::NO_SHIFT);
	return vixl::aarch64::MemOperand(op.GetBaseRegister(), op.GetOffset() + offset, op.GetAddrMode());
}

void armGetMemOperandInRegister(const vixl::aarch64::Register& addr_reg, const vixl::aarch64::MemOperand& op, s64 extra_offset /*= 0*/)
{
	pxAssert(addr_reg.IsX());
	pxAssert(op.GetBaseRegister().IsValid() && op.GetAddrMode() == vixl::aarch64::Offset && op.GetShift() == vixl::aarch64::NO_SHIFT);
	armAsm->Add(addr_reg, op.GetBaseRegister(), op.GetOffset() + extra_offset);
}

void armLoadConstant128(const vixl::aarch64::VRegister& reg, const void* ptr)
{
	u64 low, high;
	memcpy(&low, ptr, sizeof(low));
	memcpy(&high, static_cast<const u8*>(ptr) + sizeof(low), sizeof(high));
	armAsm->Ldr(reg, high, low);
}

void armEmitVTBL(const vixl::aarch64::VRegister& dst, const vixl::aarch64::VRegister& src1, const vixl::aarch64::VRegister& src2, const vixl::aarch64::VRegister& tbl)
{
	pxAssert(src1.GetCode() != RQSCRATCH.GetCode() && src2.GetCode() != RQSCRATCH2.GetCode());
	pxAssert(tbl.GetCode() != RQSCRATCH.GetCode() && tbl.GetCode() != RQSCRATCH2.GetCode());

	// must be consecutive
	if (src2.GetCode() == (src1.GetCode() + 1))
	{
		armAsm->Tbl(dst.V16B(), src1.V16B(), src2.V16B(), tbl.V16B());
		return;
	}

	armAsm->Mov(RQSCRATCH.Q(), src1.Q());
	armAsm->Mov(RQSCRATCH2.Q(), src2.Q());
	armAsm->Tbl(dst.V16B(), RQSCRATCH.V16B(), RQSCRATCH2.V16B(), tbl.V16B());
}

void ArmConstantPool::Init(void* ptr, u32 capacity)
{
 armRequirePoolIdle(this);
	m_base_ptr = static_cast<u8*>(ptr);
	m_capacity = capacity;
	m_used = 0;
	m_jump_targets.clear();
	m_literals.clear();
}

void ArmConstantPool::Destroy()
{
 armRequirePoolIdle(this);
	m_base_ptr = nullptr;
	m_capacity = 0;
	m_used = 0;
	m_jump_targets.clear();
	m_literals.clear();
}

void ArmConstantPool::Reset()
{
 armRequirePoolIdle(this);
	m_used = 0;
	m_jump_targets.clear();
	m_literals.clear();
}

u8* ArmConstantPool::GetJumpTrampoline(const void* target)
{
	auto it = m_jump_targets.find(target);
	if (it != m_jump_targets.end())
		return m_base_ptr + it->second;

	// align to 16 bytes?
	const u32 offset = Common::AlignUpPow2(m_used, 16);

	// 4 movs plus a jump
	if (offset > m_capacity || m_capacity - offset < 24)
	{
		Console.Error("Ran out of space in constant pool");
		return nullptr;
	}

	u8* const trampoline_ptr = m_base_ptr + offset;
	static constexpr size_t TRAMPOLINE_WRITE_WINDOW = 64;
	const bool batched = armBeginPoolAppend(this, trampoline_ptr, TRAMPOLINE_WRITE_WINDOW, m_capacity - offset);
	// Emit into the RW alias; trampoline_ptr (RX) is what callers branch to.
	a64::MacroAssembler masm(static_cast<vixl::byte*>(armGetWritableCodePtr(trampoline_ptr)), m_capacity - offset);
	masm.bti(a64::EmitBTI_jc);
	masm.Mov(RXVIXLSCRATCH, reinterpret_cast<intptr_t>(target));
	masm.Br(RXVIXLSCRATCH);
	masm.FinalizeCode();

	pxAssert(masm.GetSizeOfCodeGenerated() <= 24);
	m_jump_targets.emplace(target, offset);
	m_used = offset + static_cast<u32>(masm.GetSizeOfCodeGenerated());

	armEndPoolAppend(this, trampoline_ptr, m_used - offset, batched);
#ifndef __SWITCH2__
	if (!batched)
		HostSys::FlushInstructionCache(reinterpret_cast<void*>(trampoline_ptr), m_used - offset);
#endif

	return trampoline_ptr;
}

u8* ArmConstantPool::GetLiteral(u64 value)
{
	return GetLiteral(u128::From64(value));
}

u8* ArmConstantPool::GetLiteral(const u128& value)
{
	auto it = m_literals.find(value);
	if (it != m_literals.end())
		return m_base_ptr + it->second;

	const u32 offset = Common::AlignUpPow2(m_used, 16);
	if (offset > m_capacity || m_capacity - offset < sizeof(value))
		return nullptr;
	u8* const literal_ptr = &m_base_ptr[offset];
	const bool batched = armBeginPoolAppend(this, literal_ptr, sizeof(value), m_capacity - offset);
	std::memcpy(armGetWritableCodePtr(literal_ptr), &value, sizeof(value));
	armEndPoolAppend(this, literal_ptr, sizeof(value), batched);
	m_literals.emplace(value, offset);
	m_used = offset + sizeof(value);
	return literal_ptr;
}

u8* ArmConstantPool::GetLiteral(const u8* bytes, size_t len)
{
	pxAssertMsg(len <= 16, "literal length is less than 16 bytes");
	u128 table_u128 = {};
	std::memcpy(table_u128._u8, bytes, len);
	return GetLiteral(table_u128);
}

u8* ArmConstantPool::GetBlob(const u8* bytes, size_t len)
{
	const u32 offset = Common::AlignUpPow2(m_used, 8);
	if (offset > m_capacity || len > m_capacity - offset)
		return nullptr;

	u8* const blob_ptr = &m_base_ptr[offset];
	const bool batched = armBeginPoolAppend(this, blob_ptr, len, m_capacity - offset);
	std::memcpy(armGetWritableCodePtr(blob_ptr), bytes, len);
	armEndPoolAppend(this, blob_ptr, len, batched);
	m_used = offset + static_cast<u32>(len);
	return blob_ptr;
}

void ArmConstantPool::EmitLoadLiteral(const vixl::aarch64::CPURegister& reg, const u8* literal) const
{
	armMoveAddressToReg(RXVIXLSCRATCH, literal);
	armAsm->Ldr(reg, a64::MemOperand(RXVIXLSCRATCH));
}


#ifdef PS2_HOST_CODE_BRIDGE
/* Candidate-private observation, no write/publication or SDK operation. */
__attribute__((visibility("hidden"))) bool ps2ArmCodeWriteWindowActive()
{
 return armAsm || s_arm_block_start || s_arm_pool_batch.owner;
}
#endif

#ifdef PS2_HOST_CODE_BRIDGE
void armBeginEEPublicationTransaction(void *base,size_t bytes)
{
    if(!ps2_code_host_bound())return;
    if(armAsm || s_arm_block_start || s_arm_pool_batch.owner)
        ps2_mtvu_retain_owner("EE transaction begins inside emitter",__LINE__);
    ps2_ee_publication_begin(base,bytes);
}
void armCommitEEPublicationTransaction()
{
    if(!ps2_code_host_bound())return;
    if(armAsm || s_arm_block_start || s_arm_pool_batch.owner)
        ps2_mtvu_retain_owner("EE transaction commits before emitter finalization",__LINE__);
    ps2_ee_publication_commit();
}
#endif
