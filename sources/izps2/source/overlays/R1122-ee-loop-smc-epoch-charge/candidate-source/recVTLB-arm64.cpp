// SPDX-FileCopyrightText: 2026 yaps2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// ARM64 EE VTLB Dynamic Code Generation
//
// Two paths for load/store instructions:
//
// 1. Fastmem (primary): Single LDR/STR via RFASTMEMBASE (x19).
//    The fastmem area is a 4GB region mapped to PS2 memory. If the page
//    is unmapped (MMIO, etc.), the SIGSEGV handler backpatches the faulting
//    instruction with a branch to a slow-path thunk.
//
// 2. Softmem (fallback): Inline VTLB page table lookup. Used when fastmem
//    is disabled or when a PC has previously faulted (vtlb_IsFaultingPC).

#include "arm64/iR5900-arm64.h"
#include "arm64/AsmHelpers.h"
#include "EeSoftmemColdContext.h"
#include "EeSoftmemPages.h"
#include "EeSoftmemWriteTag.h"
#include "EeWriteShadow.h"
#include "vtlb.h"
#include "VU.h"
#include "Hw.h"
#include "Memory.h"
#include "common/Assertions.h"
#include <bit>

extern void vu0Sync();

namespace a64 = vixl::aarch64;

using namespace vtlb_private;

#if defined(PS2_HOST_CODE_BRIDGE) || PS2_EE_SOFTMEM_SHARED_CALLS
namespace
{
const void* s_eeMemoryCall[2][5]{};

const void* const s_eeMemoryHandler[2][5] = {
	{(void*)vtlb_memRead<mem8_t>, (void*)vtlb_memRead<mem16_t>, (void*)vtlb_memRead<mem32_t>,
	 (void*)vtlb_memRead<mem64_t>, (void*)vtlb_memRead128},
	{(void*)vtlb_memWrite<mem8_t>, (void*)vtlb_memWrite<mem16_t>, (void*)vtlb_memWrite<mem32_t>,
	 (void*)vtlb_memWrite<mem64_t>, (void*)vtlb_memWrite128}
};
}

__attribute__((visibility("hidden"))) void recEmitEeMemoryCalls()
{
	for (unsigned write = 0; write < 2; ++write)
	{
		for (unsigned width = 0; width < 5; ++width)
		{
			armAsm->EnsureEmitPoolsFor(4);
			s_eeMemoryCall[write][width] = armGetCurrentCodePointer();
			armAsm->bti(a64::EmitBTI_jc);
			armAsm->Str(a64::x30, a64::MemOperand(a64::sp, -16, a64::PreIndex));
			armFlushCycleDelta();
			armFlushEEPinsBeforePreserveMostCall();
			armEmitCall(s_eeMemoryHandler[write][width]);
			armReloadCycleDelta();
			armReloadEEPinsAfterPreserveMostCall();
			armAsm->Ldr(a64::x30, a64::MemOperand(a64::sp, 16, a64::PostIndex));
			armAsm->Ret();
		}
	}
}

static bool emitSharedEeMemoryCall(bool write, unsigned width)
{
	// A recording emitter retains its original external-call relocation.
	if (!s_eeMemoryCall[write][width] || armAddressRecorder)
		return false;
	armEmitCall(s_eeMemoryCall[write][width]);
	return true;
}
#else
static constexpr bool emitSharedEeMemoryCall(bool, unsigned) { return false; }
#endif




// =====================================================================================================
//  Softmem — Inline VTLB Lookup (fallback for faulting PCs)
// =====================================================================================================

// x19 contains vmap only in the globally softmem mode. With fastmem
// enabled, an individually faulted PC still needs the original lookup:
// x19 then contains fastmem_base and must not be used as a table pointer.
// Code-generation-only opt-in; retains the actual current PTE on every access.
#ifndef PS2_EE_SOFTMEM_OPERAND_FOLD
#define PS2_EE_SOFTMEM_OPERAND_FOLD 0
#endif
#ifndef PS2_EE_SOFTMEM_SOURCE_OPERANDS
#define PS2_EE_SOFTMEM_SOURCE_OPERANDS 0
#endif
#ifndef PS2_EE_SOFTMEM_DIRECT_ALLOC_LOAD
#define PS2_EE_SOFTMEM_DIRECT_ALLOC_LOAD 0
#endif
#ifndef PS2_EE_SOFTMEM_DIRECT_FPR_LOAD
#define PS2_EE_SOFTMEM_DIRECT_FPR_LOAD 0
#endif
// A local safe-home whitelist, not a new allocator mask. Only already-live
// typed scalar homes may qualify; caller4..7/14/15 are saved by the live scope,
// x28 is AAPCS callee-saved. Every address/PTE/ABI/system/pin register is out.
#ifndef PS2_EE_SOFTMEM_DIRECT_SOURCE_PIN_READ
#define PS2_EE_SOFTMEM_DIRECT_SOURCE_PIN_READ 0
#endif
#ifdef IZPS2_TEST_SOFTMEM_AB
static bool s_test_softmem_optimizations = false;

void recSetSoftmemOptimizationsForTest(bool enabled)
{
	s_test_softmem_optimizations = enabled;
}
#endif

static inline bool UseSoftmemOptimization(bool enabled)
{
#ifdef IZPS2_TEST_SOFTMEM_AB
	return enabled && s_test_softmem_optimizations;
#else
	return enabled;
#endif
}

static constexpr u32 kSoftmemDirectScalarHomeMask = 0x1000c0f0u;
static_assert((kSoftmemDirectScalarHomeMask & 0x0fff3f03u) == 0);
static bool vtlbSoftmemUsesTaggedPages()
{
#ifdef PS2_EE_SOFTMEM_TAGGED_PAGES
	return !CHECK_FASTMEM && !g_eeSoftmemUseCanonicalMap;
#else
	return false;
#endif
}

// The RAM load/store can use an unsigned scaled offset or a signed unscaled offset.
static constexpr bool vtlbSoftmemCanFoldRamOffset([[maybe_unused]] u32 address, [[maybe_unused]] u32 bytes)
{
#ifdef PS2_EE_SOFTMEM_FOLD_RAM_OFFSET
	return address <= 255u || ((address & (bytes - 1)) == 0 && address / bytes <= 4095u);
#else
	return false;
#endif
}

// Every encodable virtual address also has an encodable scaled PTE index.
static bool vtlbSoftmemCanUseImmediate(bool known, u32 address, u32 bytes)
{
	return UseSoftmemOptimization(PS2_EE_SOFTMEM_OPERAND_FOLD) && !CHECK_FASTMEM && known &&
		(address <= 0xfffu || (address <= 0xfff000u && (address & 0xfffu) == 0) ||
		 (vtlbSoftmemUsesTaggedPages() && vtlbSoftmemCanFoldRamOffset(address, bytes)));
}

static void vtlbSoftmemPrepareGpr(int guest)
{
	if (GPR_IS_CONST1(guest))
		return;

	if (!CHECK_FASTMEM)
	{
		const bool quad = _hasNEONreg(NEONTYPE_GPRREG, guest);
#ifdef PS2_EE_SOFTMEM_RETAIN_QUAD_SOURCE
		// Source extraction already reads a lone current quad's low lane.
		// Preserve both lanes for MMIO writeback, leaving RAM free of it.
		// A competing scalar home still requires the original reconciliation.
		if (UseSoftmemOptimization(true) && quad &&
			_hasNEONreg(NEONTYPE_GPRREG, guest, MODE_WRITE) &&
			!_hasArm64GPR(ARM64TYPE_GPR, guest))
			return;
#endif
		if (!quad && !armEEPinForGPR(guest))
		{
			constexpr int readWrite = MODE_READ | MODE_WRITE;
			for (const auto& slot : arm64gprs)
			{
				if (slot.inuse && slot.type == ARM64TYPE_GPR && slot.reg == guest &&
					(slot.mode & readWrite) == readWrite)
					return;
			}
		}
	}
	_flushEEreg(guest, false);
}

static void vtlbSoftmemLoadPageEntry(a64::MacroAssembler* masm, bool known_address = false, u32 known_value = 0, unsigned address_register = 9, bool canonical_read = false)
{
	a64::Register base = RFASTMEMBASE;
	if (canonical_read)
	{
		// The guarded plane is pinned. The unguarded mapping precedes it.
		static_assert((1u << (32 - VTLB_PAGE_BITS)) * sizeof(uptr) == (8u << 20));
		masm->Sub(a64::x17, base, 8u << 20);
		base = a64::x17;
	}
	// AArch64 unsigned scaled LDR immediate: 12 bits * sizeof(current PTE).
	// This folds a virtual page index, never a physical address or RAM mask.
	// CHECK_FASTMEM still needs the baseline materialized vmap pointer.
	if (UseSoftmemOptimization(PS2_EE_SOFTMEM_OPERAND_FOLD) && !CHECK_FASTMEM &&
		known_address && (known_value >> VTLB_PAGE_BITS) <= 4095u)
	{
		masm->Ldr(a64::x8, a64::MemOperand(base,
			static_cast<u64>(known_value >> VTLB_PAGE_BITS) * 8u));
		return;
	}
	masm->Lsr(a64::w8, armWRegister(address_register), VTLB_PAGE_BITS);
	if (CHECK_FASTMEM)
	{
		armMoveAddressToReg(RSCRATCHADDR, vtlbdata.vmap);
		masm->Ldr(a64::x8, a64::MemOperand(RSCRATCHADDR, a64::x8, a64::LSL, 3));
	}
	else
	{
		masm->Ldr(a64::x8, a64::MemOperand(base, a64::x8, a64::LSL, 3));
	}
}

static void vtlbSoftmemAddAddress(a64::MacroAssembler* masm, unsigned address_register, bool immediate, u32 address)
{
	// Both forms test the sign of the full 64-bit PTE-plus-address sum.
	if (immediate)
		masm->Adds(a64::x0, a64::x8, a64::Operand(static_cast<u64>(address)));
	else
		masm->Adds(a64::x0, a64::x8, a64::Operand(armWRegister(address_register), a64::UXTW));
}

static a64::MemOperand vtlbSoftmemTranslate(a64::MacroAssembler* masm, unsigned address_register, bool known, u32 address, bool immediate, bool tagged, u32 bytes, bool canonical_read = false)
{
	vtlbSoftmemLoadPageEntry(masm, known, address, address_register, canonical_read);
	if (tagged && !immediate)
		return a64::MemOperand(a64::x8, armWRegister(address_register), a64::UXTW);
	if (tagged && immediate && vtlbSoftmemCanFoldRamOffset(address, bytes))
		return a64::MemOperand(a64::x8, address);

	vtlbSoftmemAddAddress(masm, address_register, immediate, address);
	return a64::MemOperand(a64::x0);
}

template <typename Load>
static void emitEeGuardedRamRead(a64::MacroAssembler* masm, unsigned address_register,
	bool known, u32 address, bool immediate, u32 bytes, const Load& load, a64::Label& done)
{
	a64::Label handler;
	masm->Tbz(a64::x8, EeWriteGuardTagBit, &handler);
	const auto memory = vtlbSoftmemTranslate(masm, address_register, known, address, immediate, true, bytes, true);
	load(masm, memory);
	masm->B(&done);
	masm->Bind(&handler);
}

static bool eeCanEmitDataStore(bool tagged, bool known, u32 address, u32 bytes)
{
	return tagged && eeWriteRoutingActive() && (!known || (address & (EeWriteChunkBytes - 1)) <= EeWriteChunkBytes - bytes);
}

// The write tag and the canonical read mapping are updated together on the
// EE owner. Check after the indirect branch so normal stores pay no extra
// test. Any possible chunk crossing uses the original handler.
template<typename Store>
static void emitEeDataStore(a64::MacroAssembler* masm, unsigned address_register,
	bool known, u32 address, bool immediate, u32 bytes, const Store& store, a64::Label& done)
{
	a64::Label handler;
	masm->Tbz(a64::x8, EeWriteGuardTagBit, &handler);
	if (known)
	{
		const u32 chunk = (address >> EeWriteChunkShift) & ((1u << EeWriteChunkIndexBits) - 1u);
		masm->Tbnz(a64::x8, EeWriteCodeMaskBit + chunk, &handler);
	}
	else
	{
		const auto guest_address = armWRegister(address_register);
		if (bytes > 1)
		{
			masm->And(a64::w0, guest_address, EeWriteChunkBytes - 1);
			masm->Cmp(a64::w0, EeWriteChunkBytes - bytes);
			masm->B(&handler, a64::hi);
		}
		masm->Ubfx(a64::w17, guest_address, EeWriteChunkShift, EeWriteChunkIndexBits);
		masm->Add(a64::w17, a64::w17, EeWriteCodeMaskBit);
		masm->Lsr(a64::x0, a64::x8, a64::x17);
		masm->Tbnz(a64::x0, 0, &handler);
	}
	const auto memory = vtlbSoftmemTranslate(masm, address_register, known, address, immediate, true, bytes, true);
	store(memory);
	masm->B(&done);
	masm->Bind(&handler);
}

static void emitEeScalarStore(a64::MacroAssembler* masm, const a64::MemOperand& memory,
	u32 bits, a64::Register valueW, a64::Register valueX, int source_fpr)
{
	if (source_fpr >= 0)
	{
		masm->Str(armSRegister(source_fpr), memory);
		return;
	}
	switch (bits)
	{
		case 8:  masm->Strb(valueW, memory); break;
		case 16: masm->Strh(valueW, memory); break;
		case 32: masm->Str(valueW, memory); break;
		case 64: masm->Str(valueX, memory); break;
	}
}

static void vtlbSoftmemSetHandlerAddress(a64::MacroAssembler* masm, bool immediate, u32 address)
{
	if (immediate)
		masm->Mov(a64::w0, address);
	else
		masm->Mov(a64::w0, a64::w9);
}

#ifdef PS2_EE_SOFTMEM_RAM_FALLTHROUGH
static bool s_softmem_ram_fallthrough = true;

void recSetSoftmemRamFallthroughForTest(bool enabled)
{
	s_softmem_ram_fallthrough = enabled;
}
#endif

template <typename RAM, typename MMIO>
static void emitInlineSoftmemAccess(a64::MacroAssembler* masm, const RAM& emitRAM, const MMIO& emitMMIO, bool tagged)
{
	a64::Label alternative, done;
#ifdef PS2_EE_SOFTMEM_RAM_FALLTHROUGH
	if (s_softmem_ram_fallthrough && !CHECK_FASTMEM)
	{
		// Put the MMIO arm before RAM so RAM falls through to the next guest op.
		if (tagged)
			masm->Tbz(a64::x8, EeSoftmemIndirectBit, &alternative);
		else
			masm->B(&alternative, a64::pl);
		emitMMIO();
		masm->B(&done);
		masm->Bind(&alternative);
		emitRAM();
		masm->Bind(&done);
		return;
	}
#endif
	if (tagged)
		masm->Tbnz(a64::x8, EeSoftmemIndirectBit, &alternative);
	else
		masm->B(&alternative, a64::mi);
	emitRAM();
	masm->B(&done);
	masm->Bind(&alternative);
	emitMMIO();
	masm->Bind(&done);
}


// Scalar softmem retains the compiler allocator on BOTH runtime arms.
// The rare arm follows RecStubs' bounded live-register save contract, and
// emits canonical writebacks without changing MODE_WRITE or map ownership.
// x0 is retired before operand capture: even a direct STORE uses x0 as ppf.
namespace {
static_assert(NUM_ARM_GPR_REGS <= 31 && NUM_ARM_NEON_REGS <= 32);
struct ScalarSoftmemLiveScope
{
	bool enabled;
	u8 gprs[16], fprs[32];
	u32 ng = 0, nf = 0, stack = 0;
	u32 dirtyGprs = 0, dirtyNeon = 0;
	explicit ScalarSoftmemLiveScope(bool retain, int pending_gpr = -1, int replaced_neon = -1) : enabled(retain)
	{
		if (!enabled) return;
		pxAssert(!arm64gprs[0].inuse);
		for (u32 i = 0; i < NUM_ARM_GPR_REGS; i++)
		{
			if (!arm64gprs[i].inuse || static_cast<int>(i) == pending_gpr)
				continue;
			if (i > 0 && i < 16 && i != 8)
				gprs[ng++] = static_cast<u8>(i);
			if (arm64gprs[i].mode & MODE_WRITE)
				dirtyGprs |= u32{1} << i;
		}
		const bool clamp = cop2ClampConstsValid();
		for (u32 i = 0; i < NUM_ARM_NEON_REGS; i++)
		{
			if (static_cast<int>(i) != replaced_neon && (arm64neon[i].inuse || (clamp &&
				(i == NEON_RESERVED_COP2_CLAMPMAX || i == NEON_RESERVED_COP2_CLAMPMIN))))
				fprs[nf++] = static_cast<u8>(i);
			// MMIO still sees the old destination, if it was dirty.
			if (arm64neon[i].inuse && (arm64neon[i].mode & MODE_WRITE))
				dirtyNeon |= u32{1} << i;
		}
		stack = (ng * 8 + nf * 16 + 15u) & ~15u;
		pxAssert(stack <= 640);
	}
	void SaveAndCanonicalize() const
	{
		if (!enabled) return;
		auto* const masm = armAsm;
		if (stack) masm->Sub(a64::sp, a64::sp, stack);
		u32 i = 0;
		for (; i + 1 < ng; i += 2)
			masm->Stp(armXRegister(gprs[i]), armXRegister(gprs[i + 1]), a64::MemOperand(a64::sp, i * 8));
		if (i < ng)
			masm->Str(armXRegister(gprs[i]), a64::MemOperand(a64::sp, i * 8));

		const u32 vector_base = (ng * 8 + 15u) & ~15u;
		i = 0;
		for (; i + 1 < nf; i += 2)
			masm->Stp(armQRegister(fprs[i]), armQRegister(fprs[i + 1]), a64::MemOperand(a64::sp, vector_base + i * 16));
		if (i < nf)
			masm->Str(armQRegister(fprs[i]), a64::MemOperand(a64::sp, vector_base + i * 16));
		// Same canonical order as iFlushCall: scalar homes before quad/FPR.
		// These existing writeback functions emit stores but mutate no maps.
		eeSoftmemForEachRegister(dirtyGprs, _writebackArm64GPR);
		_writebackNEONregs(dirtyNeon);
		// Do not mark the compile-time flags flushed: RAM skips these stores.
		masm->Mov(RWSCRATCH, pc);
		masm->Str(RWSCRATCH, armCpuRegMem(&cpuRegs.pc));
		masm->Mov(RWSCRATCH, cpuRegs.code);
		masm->Str(RWSCRATCH, armCpuRegMem(&cpuRegs.code));
	}
	void Restore() const
	{
		if (!enabled || !stack) return;
		auto* const masm = armAsm;
		u32 i = 0;
		for (; i + 1 < ng; i += 2)
			masm->Ldp(armXRegister(gprs[i]), armXRegister(gprs[i + 1]), a64::MemOperand(a64::sp, i * 8));
		if (i < ng)
			masm->Ldr(armXRegister(gprs[i]), a64::MemOperand(a64::sp, i * 8));

		const u32 vector_base = (ng * 8 + 15u) & ~15u;
		i = 0;
		for (; i + 1 < nf; i += 2)
			masm->Ldp(armQRegister(fprs[i]), armQRegister(fprs[i + 1]), a64::MemOperand(a64::sp, vector_base + i * 16));
		if (i < nf)
			masm->Ldr(armQRegister(fprs[i]), a64::MemOperand(a64::sp, vector_base + i * 16));
		masm->Add(a64::sp, a64::sp, stack);
	}
};
} // anonymous namespace

// Generates inline vtlb read code. Result in w0/x0.
//
// Algorithm:
//   vmv = vmap[addr >> VTLB_PAGE_BITS]    (page table lookup)
//   ppf = addr + vmv                       (combine with mapping)
//   if (ppf >= 0) result = *(DataType*)ppf (fast path: direct read)
//   else call vtlb_memRead(addr)           (slow path: handler dispatch)
//
// addr_reg: ARM64 W register index containing the EE virtual address
// Clobbers: w0, x0, x8, x9, x17
// Result: in w0/x0
static void vtlbSoftmemRead(int addr_wreg, u32 bits, bool sign, bool retain_allocator = false,
	int result_reg = 0, bool known_address = false, u32 known_value = 0,
	int result_fpr = -1, bool direct_source_register = false, bool result_pending = false)
{
	auto* const masm = armAsm;
	pxAssert(bits == 8 || bits == 16 || bits == 32 || bits == 64);
	pxAssert(!result_pending || (result_reg > 0 && result_reg < NUM_ARM_GPR_REGS &&
		arm64gprs[result_reg].mode == 0));
	// A pending GPR slot has no value to save. MMIO sees the old guest GPR
	// in canonical memory and writes the result after the restore.
	const ScalarSoftmemLiveScope live(retain_allocator, result_pending ? result_reg : -1);
	pxAssert(result_reg == 0 || (retain_allocator &&
		((result_reg >= 19 && result_reg <= 29) ||
		 (PS2_EE_SOFTMEM_SOURCE_OPERANDS && result_reg >= 11 && result_reg <= 13) ||
		 (PS2_EE_SOFTMEM_DIRECT_ALLOC_LOAD && result_reg > 0 && result_reg < NUM_ARM_GPR_REGS &&
		  (kSoftmemDirectScalarHomeMask & (1u << result_reg)) &&
		  arm64gprs[result_reg].inuse && arm64gprs[result_reg].type == ARM64TYPE_GPR &&
		  arm64gprs[result_reg].reg == _Rt_ && (result_pending || (arm64gprs[result_reg].mode & MODE_READ))))));
	pxAssert(result_fpr < 0 || (PS2_EE_SOFTMEM_DIRECT_FPR_LOAD && retain_allocator &&
		bits == 32 && !sign && result_reg == 0 && result_fpr < NUM_ARM_NEON_REGS &&
		arm64neon[result_fpr].inuse && arm64neon[result_fpr].type == NEONTYPE_FPREG &&
		arm64neon[result_fpr].reg == _Rt_));
	const a64::Register resultX = armXRegister(result_reg);
	const a64::Register resultW = armWRegister(result_reg);

	// Save the original address in w9 (needed for slow path)
	if (!direct_source_register && addr_wreg != 9)
		masm->Mov(a64::w9, armWRegister(addr_wreg));

	const bool immediate = vtlbSoftmemCanUseImmediate(known_address, known_value, bits / 8);
	const bool tagged = vtlbSoftmemUsesTaggedPages();
	const auto memory = vtlbSoftmemTranslate(masm, direct_source_register ? addr_wreg : 9, known_address, known_value, immediate, tagged, bits / 8);

	const auto emitLoad = [=](a64::MacroAssembler* assembler, const a64::MemOperand& target)
	{
		// Direct scalar/FPR destinations retain the exact current PTE test.
		// A new FPR home is reserved MODE0: the MMIO arm must still observe
		// the old architectural ft, never an unwritten MODE_WRITE value.
		if (result_fpr >= 0)
		{
			assembler->Ldr(armSRegister(result_fpr), target);
			if (CHECK_FPU_FULL)
				armEmitEeFprFromS(armDRegister(result_fpr), armSRegister(result_fpr), RXSCRATCH);
			return;
		}
		// --- Fast path: direct memory read from host pointer ppf ---
		switch (bits)
		{
			case 8:
				if (sign)
					assembler->Ldrsb(resultX, target);
				else
					assembler->Ldrb(resultW, target);
				break;
			case 16:
				if (sign)
					assembler->Ldrsh(resultX, target);
				else
					assembler->Ldrh(resultW, target);
				break;
			case 32:
				if (sign)
					assembler->Ldrsw(resultX, target);
				else
					assembler->Ldr(resultW, target);
				break;
			case 64:
				assembler->Ldr(resultX, target);
				break;
		}
	};
	auto emitRAM = [&]() { emitLoad(masm, memory); };
	auto emitMMIO = [=]()
	{
		// This callback may run after the allocator switches to the cold assembler.
		auto* const cold_masm = armAsm;
		a64::Label done;
		if (tagged && eeWriteRoutingActive())
			emitEeGuardedRamRead(cold_masm, direct_source_register ? addr_wreg : 9, known_address, known_value, immediate, bits / 8, emitLoad, done);
		// A resident source register remains untouched on the RAM arm until the
		// address has been consumed. Capture the original address only on
		// MMIO, BEFORE any canonicalization or handler can overwrite it.
		if (direct_source_register) cold_masm->Mov(a64::w9, armWRegister(addr_wreg));
		live.SaveAndCanonicalize();
		vtlbSoftmemSetHandlerAddress(cold_masm, immediate, known_value);
	
		// Spill/reload RECCYCLE around the handler call: page-0F INTC_STAT
		// reads invoke IntCHackCheck which mutates cpuRegs.cycle. Without
		// this the JIT's pinned x25 stays stale and block-end cycle compare
		// never trips on tight INTC polls.
		if (!emitSharedEeMemoryCall(false, std::countr_zero(bits) - 3))
		{
			armFlushCycleDelta();
			armFlushEEPinsBeforePreserveMostCall(); // lazy-dirty seam twin — emits nothing today
			switch (bits)
			{
				case 8:  armEmitCall((void*)vtlb_memRead<mem8_t>);  break;
				case 16: armEmitCall((void*)vtlb_memRead<mem16_t>); break;
				case 32: armEmitCall((void*)vtlb_memRead<mem32_t>); break;
				case 64: armEmitCall((void*)vtlb_memRead<mem64_t>); break;
			}
			armReloadCycleDelta();
			// vtlb_memRead is preserve_most: the spared x9-x15 range covers every
			// caller-saved pin (tier-2 x11/x12/x13), so this emits nothing — the
			// warm softmem seam carries zero pin traffic (x0 result untouched).
			armReloadEEPinsAfterPreserveMostCall();
		}
		// Restore the old destination first. The live scope leaves x0 alone,
		// so the conversion can write directly into the final register.
		live.Restore();
		if (result_fpr >= 0)
			armEmitEeFprSlotFromWord(armDRegister(result_fpr), a64::w0, RXSCRATCH);
		else if (sign && bits == 8)
			cold_masm->Sxtb(resultX, a64::w0);
		else if (sign && bits == 16)
			cold_masm->Sxth(resultX, a64::w0);
		else if (sign && bits == 32)
			cold_masm->Sxtw(resultX, a64::w0);
		else if (retain_allocator && bits < 64)
			cold_masm->Uxtw(resultX, a64::x0);
		else if (result_reg != 0)
			cold_masm->Mov(resultX, a64::x0);
		cold_masm->Bind(&done);
	};
	if (eeSoftmemTryCold(emitRAM, emitMMIO, live.dirtyGprs, live.dirtyNeon, tagged)) return;

	emitInlineSoftmemAccess(masm, emitRAM, emitMMIO, tagged);
}

// Generates inline vtlb write code.
// addr_reg: W register index with EE virtual address
// value_reg: W/X register index with value to write
// Clobbers: w0, x0, w1, x1, x8, x9, x17
static void vtlbSoftmemWrite(int addr_wreg, int value_reg, u32 bits, bool retain_allocator = false,
	bool known_address = false, u32 known_value = 0, bool direct_source_register = false, int source_fpr = -1)
{
	auto* const masm = armAsm;
	pxAssert(bits == 8 || bits == 16 || bits == 32 || bits == 64);
	pxAssert(source_fpr < 0 || (bits == 32 && retain_allocator &&
		source_fpr < NUM_ARM_NEON_REGS && !CHECK_FPU_FULL &&
		arm64neon[source_fpr].inuse && arm64neon[source_fpr].type == NEONTYPE_FPREG));
	const ScalarSoftmemLiveScope live(retain_allocator);

	if (!direct_source_register && addr_wreg != 9)
		masm->Mov(a64::w9, armWRegister(addr_wreg));
	int ram_value_reg = value_reg;
	// The legacy unaligned paths pass x0; it must still be captured before
	// ADDS overwrites x0. For resident scalar sources all lookup temporaries
	// are reserved x0/x8/x9/x17, so RAM can consume the source itself.
	if (!UseSoftmemOptimization(PS2_EE_SOFTMEM_SOURCE_OPERANDS) || value_reg == 0)
	{
		if (value_reg != 10)
		{
			if (bits <= 32)
				masm->Mov(a64::w10, armWRegister(value_reg));
			else
				masm->Mov(a64::x10, armXRegister(value_reg));
		}
		ram_value_reg = 10;
	}
	pxAssert(ram_value_reg != 0 && ram_value_reg != 8 && ram_value_reg != 9 && ram_value_reg != 17);
	const a64::Register valueX = ram_value_reg == 31 ? a64::xzr : armXRegister(ram_value_reg);
	const a64::Register valueW = ram_value_reg == 31 ? a64::wzr : armWRegister(ram_value_reg);

	const bool immediate = vtlbSoftmemCanUseImmediate(known_address, known_value, bits / 8);
	const bool tagged = vtlbSoftmemUsesTaggedPages();
	const unsigned addr = direct_source_register ? addr_wreg : 9;
	const bool data_store = eeCanEmitDataStore(tagged, known_address, known_value, bits / 8);
	const auto memory = vtlbSoftmemTranslate(masm, addr, known_address, known_value, immediate, tagged, bits / 8);

	auto emitRAM = [&]()
	{
		emitEeScalarStore(masm, memory, bits, valueW, valueX, source_fpr);
	};
	auto emitMMIO = [=]()
	{
		// This callback may run after the allocator switches to the cold assembler.
		auto* const cold_masm = armAsm;
		a64::Label done;
		if (data_store)
		{
			emitEeDataStore(cold_masm, addr, known_address, known_value, immediate, bits / 8,
				[&](const auto& destination) {
					emitEeScalarStore(cold_masm, destination, bits, valueW, valueX, source_fpr);
				}, done);
		}
		// Capture the original address before canonicalization can clobber it.
		if (direct_source_register) cold_masm->Mov(a64::w9, armWRegister(addr_wreg));
		live.SaveAndCanonicalize();
		vtlbSoftmemSetHandlerAddress(cold_masm, immediate, known_value);
		if (source_fpr >= 0)
			cold_masm->Fmov(a64::w1, armSRegister(source_fpr));
		else if (bits <= 32)
			cold_masm->Mov(a64::w1, valueW);
		else
			cold_masm->Mov(a64::x1, valueX);
	
		// Spill/reload RECCYCLE: write-side handlers are symmetric to reads —
		// any cycle-mutating handler reachable from MMIO must keep the JIT's
		// pinned x25 coherent. See vtlbSoftmemRead for full rationale.
		if (!emitSharedEeMemoryCall(true, std::countr_zero(bits) - 3))
		{
			armFlushCycleDelta();
			armFlushEEPinsBeforePreserveMostCall(); // lazy-dirty seam twin — emits nothing today
			switch (bits)
			{
				case 8:  armEmitCall((void*)vtlb_memWrite<mem8_t>);  break;
				case 16: armEmitCall((void*)vtlb_memWrite<mem16_t>); break;
				case 32: armEmitCall((void*)vtlb_memWrite<mem32_t>); break;
				case 64: armEmitCall((void*)vtlb_memWrite<mem64_t>); break;
			}
			armReloadCycleDelta();
			// preserve_most seam — emits nothing; see vtlbSoftmemRead.
			armReloadEEPinsAfterPreserveMostCall();
		}
	
		live.Restore();
		cold_masm->Bind(&done);
	};
	if (eeSoftmemTryCold(emitRAM, emitMMIO, live.dirtyGprs, live.dirtyNeon, tagged)) return;

	emitInlineSoftmemAccess(masm, emitRAM, emitMMIO, tagged);
}

// =====================================================================================================
//  Load/Store Instruction Implementations
// =====================================================================================================

namespace R5900 {
namespace Dynarec {
namespace OpcodeImpl {

namespace Interp = R5900::Interpreter::OpcodeImpl;

// =====================================================================================================
//  Fastmem helpers
// =====================================================================================================

// Build bitmasks of currently allocated ARM64 GPR and NEON registers.
// The backpatch thunk uses these to save/restore live registers around
// the vtlb C call, preventing corruption of JIT register allocator state.
static void vtlbGetLiveRegisterMasks(u32& gpr_bitmask, u32& fpr_bitmask)
{
	gpr_bitmask = 0;
	for (int i = 0; i < NUM_ARM_GPR_REGS; i++)
	{
		if (arm64gprs[i].inuse)
			gpr_bitmask |= (1u << i);
	}

	fpr_bitmask = 0;
	for (int i = 0; i < NUM_ARM_NEON_REGS; i++)
	{
		if (arm64neon[i].inuse)
			fpr_bitmask |= (1u << i);
	}

	// SL-13: the q25/q26 clamp-constant broadcasts are not allocator state,
	// but when compile-time valid they must survive a backpatched slowmem
	// thunk's C call like any live register — clamp sites after this access
	// were compiled without re-materialization.
	if (cop2ClampConstsValid())
		fpr_bitmask |= (1u << NEON_RESERVED_COP2_CLAMPMAX) | (1u << NEON_RESERVED_COP2_CLAMPMIN);
}

// Emit a single fastmem load instruction and register backpatch info.
// addr_wreg: W register index holding the 32-bit guest virtual address
// dest_reg: register index where the result goes (W or X based on bits)
// Result is in dest_reg after the load (or after backpatch thunk on fault).
static void vtlbFastmemRead(int addr_wreg, int dest_reg, u32 bits, bool sign)
{
	u32 gpr_bitmask, fpr_bitmask;
	vtlbGetLiveRegisterMasks(gpr_bitmask, fpr_bitmask);

	const u8* codeStart = armGetCurrentCodePointer();

	a64::MemOperand mem(RFASTMEMBASE, armWRegister(addr_wreg), a64::UXTW);
	switch (bits)
	{
		case 8:
			if (sign)
				armAsm->Ldrsb(armXRegister(dest_reg), mem);
			else
				armAsm->Ldrb(armWRegister(dest_reg), mem);
			break;
		case 16:
			if (sign)
				armAsm->Ldrsh(armXRegister(dest_reg), mem);
			else
				armAsm->Ldrh(armWRegister(dest_reg), mem);
			break;
		case 32:
			if (sign)
				armAsm->Ldrsw(armXRegister(dest_reg), mem);
			else
				armAsm->Ldr(armWRegister(dest_reg), mem);
			break;
		case 64:
			armAsm->Ldr(armXRegister(dest_reg), mem);
			break;
	}

	vtlb_AddLoadStoreInfo((uptr)codeStart, 4, pc, gpr_bitmask, fpr_bitmask,
		static_cast<u8>(addr_wreg), static_cast<u8>(dest_reg),
		static_cast<u8>(bits), sign, true, false);
}

// Emit a single fastmem store instruction and register backpatch info.
static void vtlbFastmemWrite(int addr_wreg, int value_reg, u32 bits)
{
	u32 gpr_bitmask, fpr_bitmask;
	vtlbGetLiveRegisterMasks(gpr_bitmask, fpr_bitmask);

	const u8* codeStart = armGetCurrentCodePointer();

	a64::MemOperand mem(RFASTMEMBASE, armWRegister(addr_wreg), a64::UXTW);
	switch (bits)
	{
		case 8:  armAsm->Strb(armWRegister(value_reg), mem); break;
		case 16: armAsm->Strh(armWRegister(value_reg), mem); break;
		case 32: armAsm->Str(armWRegister(value_reg), mem);  break;
		case 64: armAsm->Str(armXRegister(value_reg), mem);  break;
	}

	vtlb_AddLoadStoreInfo((uptr)codeStart, 4, pc, gpr_bitmask, fpr_bitmask,
		static_cast<u8>(addr_wreg), static_cast<u8>(value_reg),
		static_cast<u8>(bits), false, false, false);
}

// Emit a single 128-bit fastmem load (LDR Q<n>, [RFASTMEMBASE, w_addr, UXTW]).
// dest_qreg 0 = the legacy q0-temp shape (LQC2, LQ-to-r0 — caller must have
// iFlushCall'd so q0 is allocator-detached); GE-14 passes an allocated
// NEONTYPE_GPRREG slot and the backpatch thunk lands the slow-path result
// there. Mirrors x86 PCSX2's alloc-callback MOVAPS pattern (ix86-32/recVTLB.cpp).
static void vtlbFastmemRead128(int addr_wreg, int dest_qreg)
{
	u32 gpr_bitmask, fpr_bitmask;
	vtlbGetLiveRegisterMasks(gpr_bitmask, fpr_bitmask);

	const u8* codeStart = armGetCurrentCodePointer();
	armAsm->Ldr(a64::QRegister(dest_qreg),
		a64::MemOperand(RFASTMEMBASE, armWRegister(addr_wreg), a64::UXTW));

	vtlb_AddLoadStoreInfo((uptr)codeStart, 4, pc, gpr_bitmask, fpr_bitmask,
		static_cast<u8>(addr_wreg), static_cast<u8>(dest_qreg),
		/*size_in_bits*/ 128, /*is_signed*/ false, /*is_load*/ true, /*is_fpr*/ true);
}

// Emit a single 32-bit fastmem load straight into an allocated FPR NEON slot
// (LDR S<n>, [RFASTMEMBASE, w_addr, UXTW]) — GE-10, the x86-master/4248
// dest-alloc-callback model. The backpatch thunk's is_fpr load tail already
// lands the slow-path C-handler result with Fmov S<n>, w0, and its save-mask
// logic skips the load dest (RecStubs.cpp).
static void vtlbFastmemReadFPR32(int addr_wreg, int dest_neonreg)
{
	u32 gpr_bitmask, fpr_bitmask;
	vtlbGetLiveRegisterMasks(gpr_bitmask, fpr_bitmask);

	const u8* codeStart = armGetCurrentCodePointer();
	armAsm->Ldr(a64::SRegister(dest_neonreg),
		a64::MemOperand(RFASTMEMBASE, armWRegister(addr_wreg), a64::UXTW));

	vtlb_AddLoadStoreInfo((uptr)codeStart, 4, pc, gpr_bitmask, fpr_bitmask,
		static_cast<u8>(addr_wreg), static_cast<u8>(dest_neonreg),
		/*size_in_bits*/ 32, /*is_signed*/ false, /*is_load*/ true, /*is_fpr*/ true);
}

// Emit a single 128-bit fastmem store (STR Q<n>, [RFASTMEMBASE, w_addr, UXTW]).
// src_qreg 0 = the legacy q0-temp shape (SQC2 — caller iFlushCall'd first);
// GE-14 passes the allocator-resident source quad and the backpatch thunk's
// slow path hands it to the C write handler.
static void vtlbFastmemWrite128(int addr_wreg, int src_qreg)
{
	u32 gpr_bitmask, fpr_bitmask;
	vtlbGetLiveRegisterMasks(gpr_bitmask, fpr_bitmask);

	const u8* codeStart = armGetCurrentCodePointer();
	armAsm->Str(a64::QRegister(src_qreg),
		a64::MemOperand(RFASTMEMBASE, armWRegister(addr_wreg), a64::UXTW));

	vtlb_AddLoadStoreInfo((uptr)codeStart, 4, pc, gpr_bitmask, fpr_bitmask,
		static_cast<u8>(addr_wreg), static_cast<u8>(src_qreg),
		/*size_in_bits*/ 128, /*is_signed*/ false, /*is_load*/ false, /*is_fpr*/ true);
}

// =====================================================================================================
//  Address computation helpers
// =====================================================================================================

// Compute load/store address (rs + imm) into w9.
// Does NOT flush — reads from wherever the value currently lives
// (const propagation, ARM64 GPR, NEON register, or cpuRegs memory).
// For const Rs, the full address is computed at compile time.
// Must run before any iFlushCall: the flush frees the slot the fold reads from.
// `imm` is the instruction's own offset except in the unaligned pair-fusion
// paths, which address off their partner's.
static void recComputeAddr(s32 imm)
{
	if (GPR_IS_CONST1(_Rs_))
	{
		armAsm->Mov(a64::w9, g_cpuConstRegs[_Rs_].UL[0] + imm);
	}
	else if (imm != 0)
	{
		// The base is read where it already lives, so the offset add doubles
		// as the move into w9.
		const a64::Register base = _eeGetGPRSourceReg(a64::w9, _Rs_);
		armAsm->Add(a64::w9, base, imm);
	}
	else
	{
		_eeMoveGPRtoR(a64::w9, _Rs_);
	}
}

static void recComputeAddr() { recComputeAddr(_Imm_); }

// The address as a fastmem index register. With a zero offset that is the
// register the base already lives in (a pin or an allocator slot) and nothing
// is emitted; otherwise the address is formed in w9 as recComputeAddr does.
// The same contract as _eeGetGPRSourceReg: consume the register in the access
// that follows, before anything can move pins or allocator slots.
static a64::Register recAddrOperand()
{
	if (_Imm_ == 0 && !GPR_IS_CONST1(_Rs_))
		return _eeGetGPRSourceReg(a64::w9, _Rs_);
	recComputeAddr();
	return a64::w9;
}

// w9 = (Rs + imm) & mask, read from wherever Rs lives (a constant base folds
// the mask at compile time). Same flush ordering rule as recComputeAddr.
static void recComputeAddrMasked(u32 mask)
{
	if (GPR_IS_CONST1(_Rs_))
	{
		armAsm->Mov(a64::w9, (g_cpuConstRegs[_Rs_].UL[0] + _Imm_) & mask);
		return;
	}
	const a64::Register base = (_Imm_ == 0) ? _eeGetGPRSourceReg(a64::w9, _Rs_) : (recComputeAddr(), a64::w9);
	armAsm->And(a64::w9, base, mask);
}

// Prepare store value in w10/x10.
// Does NOT flush — reads from wherever the value currently lives.
static void recPrepStoreValue(u32 bits)
{
	_eeMoveGPRtoR(bits <= 32 ? a64::w10 : a64::x10, _Rt_);
}

// Store the load result to guest register Rt.
// Invalidates any existing allocation for Rt (const/GPR/NEON) and
// writes the result to cpuRegs memory. `result` defaults to x0 (the C-call /
// softmem convention); the fastmem pinned-dest path (WS-C4) loads straight
// into the pin and passes it here — armStoreEERegPtr then emits just the
// canonical Str (the mirror Mov self-skips when src IS the pin).
static void recStoreLoadResult(const a64::Register& result = a64::x0)
{
	if (_Rt_)
	{
		_deleteEEreg(_Rt_, 0);
		GPR_DEL_CONST(_Rt_);
		_eeStoreGPRDestReg(_Rt_, result);
	}
}

// =====================================================================================================
//  Load implementations
// =====================================================================================================

// Const-paddr MMIO shortcut. When Rs is constant and the resolved page is a
// handler (MMIO), emit a direct BL to the registered handler instead of going
// through fastmem-fault → backpatch thunk → vtlb_memRead → page-table dispatch.
// Mirrors x86 vtlb_DynGenReadNonQuad_Const (ix86-32/recVTLB.cpp).
//
// Direct (RAM-backed) const-paddr loads stay on the fastmem path — a single
// LDR off RFASTMEMBASE is already optimal for those.
//
// Returns true if the shortcut emitted the load; caller should bail out.
static bool recLoadConstPaddrMMIOShortcut(u32 bits, bool sign, bool forceEventTest)
{
	if (!GPR_IS_CONST1(_Rs_))
		return false;

	const u32 addr_const = g_cpuConstRegs[_Rs_].UL[0] + _Imm_;
	const auto vmv = vtlbdata.vmap[addr_const >> VTLB_PAGE_BITS];
	if (!vmv.isHandler(addr_const))
		return false;

	const u32 paddr = vmv.assumeHandlerGetPAddr(addr_const);

	// FLUSH_VTLB, not FLUSH_INTERPRETER: the registered handler is the same
	// callee set the softmem slow path dispatches to, and it cannot write guest
	// GPRs. So const marks survive and an MMIO burst (consecutive
	// const-address accesses off one LUI) keeps taking this shortcut instead
	// of degrading to the dynamic path after the first access. x86 parity:
	// its const-MMIO sites flush nothing at all (FLUSH_FULLVTLB).
	//
	// FLUSH_PC only when the const address resolves to an UNMAPPED handler:
	// its vtlb_Miss path reads the cpuRegs.pc this seam flushes. Hardware
	// handlers never read cpuRegs.pc and tolerate a stale one (x86 parity
	// again: it never flushes pc at MMIO sites), so their per-access
	// mov/movk/str pc store would be dead weight in MMIO bursts.
	//
	// EXCEPTION: forceEventTest (EE-counter reads). The caller ends the block
	// with the g_branch=2 event-test exit, whose FLUSH_EVERYTHING does NOT
	// include FLUSH_PC (0x200 vs 0x1ff) — this seam's pc store (pc is already
	// advanced past the load) is what DispatcherReg resumes from.
	//
	// Note this reads handler identity at COMPILE time to decide a RUNTIME
	// property, but adds no new assumption: the shortcut already resolves the
	// callee from the same compile-time mapping and hard-codes a direct BL to
	// it. If a remap could invalidate the handler identity here it would
	// already be branching to the wrong callee, so the pc decision is exactly
	// as sound as the shortcut itself.
	const bool needPcFlush =
		vtlb_IsUnmappedHandlerID(vmv.assumeHandlerGetID()) || forceEventTest;
	iFlushCall(needPcFlush ? (FLUSH_VTLB | FLUSH_PC) : FLUSH_VTLB);

	// INTC_STAT inline-load when the speedhack is disabled. With it on
	// (the default), fall through to a direct BL of the registered
	// hwRead32_page_0F_INTC_HACK handler.
	if (bits == 32 && !EmuConfig.Speedhacks.IntcStat && paddr == INTC_STAT)
	{
		armLoadPtr(a64::w0, &psHu32(INTC_STAT));
		if (sign)
			armAsm->Sxtw(a64::x0, a64::w0);
		recStoreLoadResult();
		return true;
	}

	int szidx = 0;
	switch (bits)
	{
		case 8:  szidx = 0; break;
		case 16: szidx = 1; break;
		case 32: szidx = 2; break;
		case 64: szidx = 3; break;
	}
	armAsm->Mov(a64::w0, paddr);
	// Spill/reload RECCYCLE around the registered handler: the const-paddr
	// MMIO shortcut targets the same handler set as vtlbSoftmemRead's slow
	// path, including page-0F INTC_STAT → IntCHackCheck which mutates
	// cpuRegs.cycle. See vtlbSoftmemRead for full rationale.
	armFlushCycleDelta();
	armFlushEEClobberedPins(); // lazy-dirty seam: pairs with the reload below
	armEmitCall(vmv.assumeHandlerGetRaw(szidx, false));
	armReloadCycleDelta();
	// Raw registered handlers are plain AAPCS (not preserve_most like the
	// vtlb_memRead/Write dispatchers) and write no guest GPRs — restore the
	// caller-saved pins they clobbered. x0 (the handler result) is untouched.
	armReloadEEClobberedPins();

	// Extend handler return value into x0 for the 64-bit cpuRegs.GPR store.
	// AAPCS64 leaves the upper bits of x0 unspecified for sub-word returns.
	if (bits < 64)
	{
		if (sign)
		{
			switch (bits)
			{
				case 8:  armAsm->Sxtb(a64::x0, a64::w0); break;
				case 16: armAsm->Sxth(a64::x0, a64::w0); break;
				case 32: armAsm->Sxtw(a64::x0, a64::w0); break;
			}
		}
		else
		{
			// Both operands X, unlike the Sxt* above. Those lower to sbfm, which
			// takes a narrow source; uxtw lowers to ubfm, whose only 64-bit form
			// is UBFM Xd,Xn,#0,#31 — there is no W-source encoding for it to
			// select, so the width is carried by the destination alone. A W
			// source encodes identically (the field holds the register number,
			// and sf comes from Rd) but trips vixl's equal-width assert, which
			// is live in every Debug build and compiled out of Release: correct
			// code that aborts on the first unsigned MMIO-page load whenever
			// asserts are on.
			armAsm->Uxtw(a64::x0, a64::x0);
		}
	}

	recStoreLoadResult();
	return true;
}

// Generic load: fastmem (primary) with softmem fallback for faulting PCs.
//
// Fastmem path: no flush — the backpatch thunk in RecStubs.cpp saves/
// restores live regs around the slow-path C call.
//
// Softmem path: FLUSH_VTLB only. iFlushCall already evicts all caller-saved
// GPRs + NEON unconditionally; constants must additionally be written back
// because the softmem paths re-read guest GPRs from cpuRegs memory post-flush
// and vtlb exception paths expect memory to be current.
// The MARKS survive (FLUSH_CONST_KEEP): vtlb handlers cannot write guest GPRs,
// so the tracking stays valid and later const-address accesses keep folding.
// PC and CODE are not essential as vtlb_memRead<T> doesn't read them, and
// exception handlers that fire from the slow path stash their own PC.
static int vtlbSoftmemLoadHome(int guest, bool& pending)
{
	for (u32 i = 1; i < NUM_ARM_GPR_REGS; i++)
	{
		const auto& slot = arm64gprs[i];
		if (slot.inuse && slot.type == ARM64TYPE_GPR && slot.reg == guest)
			return ((kSoftmemDirectScalarHomeMask & (1u << i)) && (slot.mode & MODE_READ)) ? static_cast<int>(i) : -1;
	}

#ifdef PS2_EE_SOFTMEM_PENDING_LOAD_HOME
	const int host = _tryAllocPendingLoadGPR(guest, kSoftmemDirectScalarHomeMask);
	pending = host >= 0;
	return host;
#else
	(void)pending;
	return -1;
#endif
}

static void recLoad(u32 bits, bool sign)
{
	const bool softmemKnownAddress = GPR_IS_CONST1(_Rs_);
	const u32 softmemAddressValue = softmemKnownAddress ? g_cpuConstRegs[_Rs_].UL[0] + _Imm_ : 0;
	// Force an event test on EE counter-range reads (the EE timers at
	// 0x10000000..0x10001FFF) to improve read + interrupt syncing — namely
	// ESPN Games. Follows the upstream x86-master fix
	// (iR5900LoadStore.cpp: needs_flush → iFlushCall(FLUSH_INTERPRETER) +
	// g_branch=2). Setting g_branch=2 makes the block finalizer end the block
	// with the event-test exit (it does the FLUSH_EVERYTHING + cycle accumulate +
	// nextEventCycle dispatch itself), so no explicit iFlushCall is needed here.
	bool forceEventTest = false;
	if (GPR_IS_CONST1(_Rs_) && bits <= 32)
	{
		const u32 srcadr = g_cpuConstRegs[_Rs_].UL[0] + _Imm_;
		forceEventTest = (srcadr & 0xFFFFE000) == 0x10000000;
	}

	if (recLoadConstPaddrMMIOShortcut(bits, sign, forceEventTest))
	{
		if (forceEventTest)
			g_branch = 2;
		return;
	}

	const bool useFastmem = CHECK_FASTMEM && !vtlb_IsFaultingPC(pc);

	unsigned addr_reg = 9;
	bool directSoftmemSourceRegister = false;
	if (useFastmem)
	{
		// GE-07: the fastmem path emits no C call, so the blanket
		// iFlushCall eviction (which clipped FPU/MMI allocator residency at
		// EVERY interleaved load) is not needed — the fault thunk
		// (vtlb_DynBackpatchLoadStore) saves/restores live registers per the
		// masks recorded at emit time, which become nonzero with this change.
		// What must remain:
		//   - _flushConstRegs: the VALUE writeback is integral at L/S sites;
		//     the marks are kept (delete_const=false) so the GPR_IS_CONST1
		//     fold below stays live - vtlb callees can't write guest GPRs.
		//   - Rs coherence: a dirty NEON dual-residence (or MODE_WRITE GPR)
		//     copy of Rs means the pin/memory value is stale — write it back
		//     (mapping kept). Same writeback today's eviction performed,
		//     minus the eviction of everything else.
		//   - x0 eviction below (the load result target when Rt is unpinned;
		//     x0 is an allocator pool member).
		// Rt dest invalidation is recStoreLoadResult's job (unchanged).
		_flushConstRegs(false);
		if (GPR_IS_CONST1(_Rs_))
		{
			armAsm->Mov(a64::w9, g_cpuConstRegs[_Rs_].UL[0] + _Imm_);
		}
		else
		{
			_flushEEreg(_Rs_, false);
			if (const a64::Register* pin = armEEPinForGPR(_Rs_))
			{
				if (_Imm_ != 0)
					armAsm->Add(a64::w9, pin->W(), _Imm_);
				else
					addr_reg = static_cast<unsigned>(pin->GetCode());
			}
			else
			{
				// A resident base is the index itself (nothing between here and
				// the access moves an allocator slot: the x0 free and the Rt quad
				// retirement below touch neither).
				addr_reg = static_cast<unsigned>(recAddrOperand().GetCode());
			}
		}
	}
	else
	{
		// Retire x0 before reading Rs: the PTE lookup overwrites it even
		// for stores; no source register may still refer to that old home.
		if (arm64gprs[0].inuse) _freeArm64GPR(0);
		_flushConstRegs(false);
		vtlbSoftmemPrepareGpr(_Rs_);
		if (UseSoftmemOptimization(PS2_EE_SOFTMEM_DIRECT_SOURCE_PIN_READ) &&
			!CHECK_FASTMEM && _Imm_ == 0 && !softmemKnownAddress)
		{
			addr_reg = _eeGetGPRSourceReg(a64::w9, _Rs_).GetCode();
			directSoftmemSourceRegister = addr_reg != 9;
		}
		else if (!vtlbSoftmemCanUseImmediate(softmemKnownAddress, softmemAddressValue, bits / 8))
			recComputeAddr();
		if (_Rt_) _deleteGPRtoNEONreg(_Rt_, DELETE_REG_FREE);
	}

	if (useFastmem)
	{
		// Pinned dest (WS-C4): load straight into the pin — the recorded
		// data_register makes the backpatch thunk reconstruct into it on a
		// fault (the thunk's Mov data_register<-x0 runs after its pin reload
		// seam, so the pin gets the load result last; with tier-2 pins in the
		// preserve_most-spared x11-x13 that seam emits nothing and the pin
		// simply rides through the call). Kills the Mov pin<-x0 mirror
		// refresh; the canonical Str in recStoreLoadResult stays. Not done
		// for softmem — its result comes back from a C call in x0.
		const a64::Register* dpin = _Rt_ ? armEEPinForGPR(_Rt_) : nullptr;
		// A dirty NEON quad for Rt (an LQ or MMI Rd dest) must be retired
		// BEFORE the Ldr-into-pin below: recStoreLoadResult's _deleteEEreg
		// would otherwise write it back AFTER the load, and
		// armStoreEEGPRQuad's pin refresh would clobber the just-loaded
		// result with the quad's stale lane 0. The unpinned path is
		// order-safe (writeback touches memory, not x0) but retiring here
		// keeps the quad out of the thunk's save mask either way.
		if (dpin)
			_deleteGPRtoNEONreg(_Rt_, DELETE_REG_FREE);
		// GE-07: with allocator state now live across fastmem, x0 (a pool
		// member) may hold a guest value — evict it before it becomes the
		// load result. Freed before mask capture so the thunk skips it too.
		if (!dpin && arm64gprs[0].inuse)
			_freeArm64GPR(0);
		vtlbFastmemRead(addr_reg, dpin ? static_cast<unsigned>(dpin->GetCode()) : 0, bits, sign);
		recStoreLoadResult(dpin ? *dpin : a64::Register(a64::x0));
	}
	else
	{
		const a64::Register* softmemPin = nullptr;
		if (UseSoftmemOptimization(PS2_EE_SOFTMEM_OPERAND_FOLD) && _Rt_)
		{
			const a64::Register* pin = armEEPinForGPR(_Rt_);
			if (pin && ((pin->GetCode() >= 19 && pin->GetCode() <= 29) ||
				(UseSoftmemOptimization(PS2_EE_SOFTMEM_SOURCE_OPERANDS) && pin->GetCode() >= 11 && pin->GetCode() <= 13)))
				softmemPin = pin;
		}
		int scalarHome = -1;
		bool pendingHome = false;
		if (UseSoftmemOptimization(PS2_EE_SOFTMEM_DIRECT_ALLOC_LOAD) && !softmemPin && _Rt_ &&
			!armEEPinForGPR(_Rt_) && EEINST_USEDTEST(_Rt_) && !GPR_IS_CONST1(_Rt_))
		{
			// Address captured and any dirty Rt quad retired above. Reserve
			// only an unused slot and publish its value at the join below.
			scalarHome = vtlbSoftmemLoadHome(_Rt_, pendingHome);
		}
		vtlbSoftmemRead(addr_reg, bits, sign, true,
			softmemPin ? softmemPin->GetCode() : (scalarHome >= 0 ? scalarHome : 0),
			softmemKnownAddress, softmemAddressValue, -1, directSoftmemSourceRegister, pendingHome);
		if (scalarHome >= 0)
		{
			GPR_DEL_CONST(_Rt_);
			// Existing helper marks READ|WRITE/needed/LRU and emits no Mov
			// when src already is the destination. Both runtime arms joined.
			_eeStoreGPRDestReg(_Rt_, armXRegister(scalarHome));
		}
		else if (softmemPin)
			recStoreLoadResult(*softmemPin);
		else if (_Rt_ && !armEEPinForGPR(_Rt_) && EEINST_USEDTEST(_Rt_))
		{
			// Both runtime arms have joined: x0 is the extended result and the
			// old w9 address is dead. Park the result before destination allocation
			// can emit an eviction/quad-retirement store. x9 is reserved from the
			// EE allocator; those emitters use x8/x17, never this address scratch.
			// Allocate only now: a new MODE_WRITE guest home before the handler
			// would make its pre-load canonicalization publish an undefined value.
			armAsm->Mov(a64::x9, a64::x0);
			(void)_eeGetGPRDestReg(_Rt_, a64::x9, true);
			_eeStoreGPRDestReg(_Rt_, a64::x9);
		}
		else
			recStoreLoadResult();
	}

	if (forceEventTest)
		g_branch = 2;
}

void recLB()  { recLoad(8,  true);  }
void recLBU() { recLoad(8,  false); }
void recLH()  { recLoad(16, true);  }
void recLHU() { recLoad(16, false); }
void recLW()  { recLoad(32, true);  }
void recLWU() { recLoad(32, false); }
void recLD()  { recLoad(64, false); }

// =====================================================================================================
//  Store implementations
// =====================================================================================================

// Symmetric to recLoadConstPaddrMMIOShortcut: when Rs is constant and the
// resolved page is a handler (MMIO), emit a direct BL to the registered write
// handler instead of going through fastmem-fault → backpatch thunk →
// vtlb_memWrite → page-table dispatch. Mirrors x86 vtlb_DynGenWrite_Const
// (ix86-32/recVTLB.cpp).
//
// Direct (RAM-backed) const-paddr stores stay on the fastmem path — a single
// STR off RFASTMEMBASE is already optimal for those.
//
// Returns true if the shortcut emitted the store; caller should bail out.
static bool recStoreConstPaddrMMIOShortcut(u32 bits)
{
	if (!GPR_IS_CONST1(_Rs_))
		return false;

	const u32 addr_const = g_cpuConstRegs[_Rs_].UL[0] + _Imm_;
	const auto vmv = vtlbdata.vmap[addr_const >> VTLB_PAGE_BITS];
	if (!vmv.isHandler(addr_const))
		return false;

	const u32 paddr = vmv.assumeHandlerGetPAddr(addr_const);

	// FLUSH_VTLB, plus FLUSH_PC only for unmapped handlers — see
	// recLoadConstPaddrMMIOShortcut for both halves (vtlb_Miss reads PC;
	// hardware handlers never do).
	// Keeping const marks is what lets a GS-priv store burst (5x sd to
	// 0x120000xx off one LUI) emit five direct BLs instead of one BL +
	// four dynamic-path stores.
	const bool isUnmapped = vtlb_IsUnmappedHandlerID(vmv.assumeHandlerGetID());
	iFlushCall(isUnmapped ? (FLUSH_VTLB | FLUSH_PC) : FLUSH_VTLB);

	int szidx = 0;
	switch (bits)
	{
		case 8:  szidx = 0; break;
		case 16: szidx = 1; break;
		case 32: szidx = 2; break;
		case 64: szidx = 3; break;
	}

	// AAPCS64: w0 = paddr, w1/x1 = value. _eeMoveGPRtoR reads Rt from its
	// coherent home (const / resident slot / pin / memory), handling Rt == 0.
	armAsm->Mov(a64::w0, paddr);
	_eeMoveGPRtoR(bits <= 32 ? a64::w1 : a64::x1, _Rt_);

	// RECCYCLE coherence — same rationale as recLoadConstPaddrMMIOShortcut.
	armFlushCycleDelta();
	armFlushEEClobberedPins(); // lazy-dirty seam: pairs with the reload below
	armEmitCall(vmv.assumeHandlerGetRaw(szidx, true));
	armReloadCycleDelta();
	// Caller-saved pin restore — same rationale as the read shortcut.
	armReloadEEClobberedPins();

	return true;
}

static void recStore(u32 bits)
{
	const bool softmemKnownAddress = GPR_IS_CONST1(_Rs_);
	const u32 softmemAddressValue = softmemKnownAddress ? g_cpuConstRegs[_Rs_].UL[0] + _Imm_ : 0;
	if (recStoreConstPaddrMMIOShortcut(bits))
		return;

	const bool useFastmem = CHECK_FASTMEM && !vtlb_IsFaultingPC(pc);

	if (useFastmem)
	{
		// GE-07: no blanket eviction on the fastmem path — see recLoad.
		// Coherence here: Rs (address) AND Rt (store value) each need any
		// dirty NEON/GPR copy written back (mappings kept); the store emit
		// itself clobbers no allocator pool member (w9/w10/x8/x17 are all
		// outside the pool).
		_flushConstRegs(false);

		// Both write-backs first (mappings kept), then the two operands from
		// where the values live: a pin or an allocator slot is used as it is,
		// so nothing can move between reading them and the access.
		if (!GPR_IS_CONST1(_Rt_))
			_flushEEreg(_Rt_, false);
		if (!GPR_IS_CONST1(_Rs_))
			_flushEEreg(_Rs_, false);

		unsigned addr_reg = 9;
		if (GPR_IS_CONST1(_Rs_))
		{
			armAsm->Mov(a64::w9, g_cpuConstRegs[_Rs_].UL[0] + _Imm_);
		}
		else if (const a64::Register* pin = armEEPinForGPR(_Rs_))
		{
			if (_Imm_ != 0)
				armAsm->Add(a64::w9, pin->W(), _Imm_);
			else
				addr_reg = static_cast<unsigned>(pin->GetCode());
		}
		else
		{
			addr_reg = static_cast<unsigned>(recAddrOperand().GetCode());
		}

		unsigned value_reg = 10;
		if (GPR_IS_CONST1(_Rt_))
		{
			if (bits <= 32)
				armAsm->Mov(a64::w10, g_cpuConstRegs[_Rt_].UL[0]);
			else
				armAsm->Mov(a64::x10, g_cpuConstRegs[_Rt_].UD[0]);
		}
		else if (const a64::Register* vpin = armEEPinForGPR(_Rt_))
		{
			value_reg = static_cast<unsigned>(vpin->GetCode());
		}
		else
		{
			value_reg = static_cast<unsigned>(_eeGetGPRSourceReg(bits <= 32 ? a64::w10 : a64::x10, _Rt_).GetCode());
		}

		vtlbFastmemWrite(addr_reg, value_reg, bits);
		return;
	}

	// Keep all other homes live. Both operands are captured into reserved
	// w9/x10 after x0 retirement, so handler args cannot destroy a source.
	if (arm64gprs[0].inuse) _freeArm64GPR(0);
	_flushConstRegs(false);
	vtlbSoftmemPrepareGpr(_Rt_);
	if (_Rt_ != _Rs_)
		vtlbSoftmemPrepareGpr(_Rs_);
	unsigned address_register = 9;
	if (!CHECK_FASTMEM && _Imm_ == 0 && !softmemKnownAddress)
		address_register = _eeGetGPRSourceReg(a64::w9, _Rs_).GetCode();
	else if (!vtlbSoftmemCanUseImmediate(softmemKnownAddress, softmemAddressValue, bits / 8))
		recComputeAddr();
	int value_reg = 10;
	if (UseSoftmemOptimization(PS2_EE_SOFTMEM_SOURCE_OPERANDS))
	{
		// x0 was retired before the lookup. The store can use ZR for $zero.
		value_reg = _Rt_ == 0 ? 31 :
			_eeGetGPRSourceReg(bits <= 32 ? a64::w10 : a64::x10, _Rt_).GetCode();
	}
	else
	{
		recPrepStoreValue(bits);
	}
	vtlbSoftmemWrite(address_register, value_reg, bits, true,
		softmemKnownAddress, softmemAddressValue, address_register != 9);
}

void recSB() { recStore(8);  }
void recSH() { recStore(16); }
void recSW() { recStore(32); }
void recSD() { recStore(64); }

// =====================================================================================================
//  LQ / SQ — 128-bit quad load/store
//  addr = (rs + imm) & ~0xF (silently aligned to 16 bytes)
// =====================================================================================================

// Inline VTLB 128-bit read. LQ can target its final register after saving
// the old architectural value on the handler path.
static void vtlbSoftmemRead128(int dest_qreg, bool known_address, u32 virtual_address, bool direct = false)
{
	auto* const masm = armAsm;
	const ScalarSoftmemLiveScope live(true, -1, direct ? dest_qreg : -1);
	const bool immediate = vtlbSoftmemCanUseImmediate(known_address, virtual_address, 16);
	const bool tagged = vtlbSoftmemUsesTaggedPages();
	const auto memory = vtlbSoftmemTranslate(masm, 9, known_address, virtual_address, immediate, tagged, 16);

	const auto emitLoad = [=](a64::MacroAssembler* assembler, const a64::MemOperand& target)
	{
		// Fast path: one NEON load, without evicting the live allocator.
		assembler->Ldr(armQRegister(dest_qreg), target);
	};
	auto emitRAM = [&]() { emitLoad(masm, memory); };
	auto emitMMIO = [=]()
	{
		// This callback may run after the allocator switches to the cold assembler.
		auto* const cold_masm = armAsm;
		a64::Label done;
		if (tagged && eeWriteRoutingActive())
			emitEeGuardedRamRead(cold_masm, 9, known_address, virtual_address, immediate, 16, emitLoad, done);
		live.SaveAndCanonicalize();
		vtlbSoftmemSetHandlerAddress(cold_masm, immediate, virtual_address);
		// See vtlbSoftmemRead for the RECCYCLE coherence rationale.
		if (!emitSharedEeMemoryCall(false, 4))
		{
			armFlushCycleDelta();
			armFlushEEPinsBeforePreserveMostCall(); // lazy-dirty seam twin — emits nothing today
			armEmitCall((void*)vtlb_memRead128);
			armReloadCycleDelta();
			// preserve_most seam — emits nothing; see vtlbSoftmemRead (q0 untouched).
			armReloadEEPinsAfterPreserveMostCall();
		}
		// q0 may be a live old FPR/quad. Install the result before Restore
		// reloads q0. The destination itself is outside the restore set.
		if (dest_qreg != 0)
			cold_masm->Mov(armQRegister(dest_qreg).V16B(), a64::q0.V16B());
		live.Restore();
		cold_masm->Bind(&done);
	};
	if (eeSoftmemTryCold(emitRAM, emitMMIO, live.dirtyGprs, live.dirtyNeon, tagged)) return;

	emitInlineSoftmemAccess(masm, emitRAM, emitMMIO, tagged);
}

// Inline VTLB 128-bit write. Legacy callers use q0; SQ supplies its
// allocator-resident source and canonicalizes/saves only on the handler arm.
static void vtlbSoftmemWrite128(int src_qreg, bool known_address, u32 virtual_address)
{
	auto* const masm = armAsm;
	const ScalarSoftmemLiveScope live(true);
	const bool immediate = vtlbSoftmemCanUseImmediate(known_address, virtual_address, 16);
	const bool tagged = vtlbSoftmemUsesTaggedPages();
	const bool data_store = eeCanEmitDataStore(tagged, known_address, virtual_address, 16);
	const auto memory = vtlbSoftmemTranslate(masm, 9, known_address, virtual_address, immediate, tagged, 16);

	auto emitRAM = [&]()
	{
		// Fast path: one NEON store directly from the retained source.
		masm->Str(armQRegister(src_qreg), memory);
	};
	auto emitMMIO = [=]()
	{
		// This callback may run after the allocator switches to the cold assembler.
		auto* const cold_masm = armAsm;
		a64::Label done;
		if (data_store)
		{
			emitEeDataStore(cold_masm, 9, known_address, virtual_address, immediate, 16,
				[&](const auto& destination) { cold_masm->Str(armQRegister(src_qreg), destination); },
				done);
		}
		live.SaveAndCanonicalize();
		// Only after the old live q0 has been saved/canonicalized is it safe
		// to install the ABI argument. The original source is restored below.
		if (src_qreg != 0)
			cold_masm->Mov(a64::q0.V16B(), armQRegister(src_qreg).V16B());
		vtlbSoftmemSetHandlerAddress(cold_masm, immediate, virtual_address);
		// See vtlbSoftmemRead for the RECCYCLE coherence rationale.
		if (!emitSharedEeMemoryCall(true, 4))
		{
			armFlushCycleDelta();
			armFlushEEPinsBeforePreserveMostCall(); // lazy-dirty seam twin — emits nothing today
			armEmitCall((void*)vtlb_memWrite128);
			armReloadCycleDelta();
			// preserve_most seam — emits nothing; see vtlbSoftmemRead.
			armReloadEEPinsAfterPreserveMostCall();
		}
		live.Restore();
		cold_masm->Bind(&done);
	};
	if (eeSoftmemTryCold(emitRAM, emitMMIO, live.dirtyGprs, live.dirtyNeon, tagged)) return;

	emitInlineSoftmemAccess(masm, emitRAM, emitMMIO, tagged);
}

void recLQ()
{
	const bool useFastmem = CHECK_FASTMEM && !vtlb_IsFaultingPC(pc);

	// GE-14 fastmem path: load straight into an allocated NEONTYPE_GPRREG
	// slot (x86 recLoadQuad's alloc-callback model) — the canonical
	// cpuRegs.GPR store is elided; dirty tracking writes it back at the next
	// seam (armStoreEEGPRQuad in _writebackNEONreg refreshes the pin, and
	// scalar readers either probe the slot via _eeMoveGPRtoR or force the
	// writeback via _deleteEEreg — the exact state an MMI Rd write creates).
	// No iFlushCall: GE-07's fault-thunk live-mask machinery saves/restores
	// allocator state, only constants must be memory-visible for the
	// slow-path C handler. Rt==0 keeps the legacy flush + q0-discard shape
	// (the load must still happen for MMIO side effects, and q0 is only
	// safe to clobber once the allocator is evicted).
	if (useFastmem && _Rt_)
	{
		// Address BEFORE the dest alloc (rt may alias rs, and the MODE_WRITE
		// alloc invalidates rt's prior GPR/const residency).
		// w9 is immune to both: the flush materializes into pins/RXSCRATCH and
		// the alloc's evictions are NEON stores.
		recComputeAddrMasked(~0xFu);
		_flushConstRegs(false);

		const int qd = _allocGPRtoNEONreg(_Rt_, MODE_WRITE);
		_validateRegs();
		vtlbFastmemRead128(9, qd);
		return;
	}

	if (!useFastmem)
	{
		// Keep every old guest home intact until the real memory access has
		// completed: the MMIO arm must see the pre-load Rt, not an undefined
		// MODE_WRITE destination. x0 is the PTE pointer/result scratch.
		if (arm64gprs[0].inuse) _freeArm64GPR(0);
		_flushConstRegs(false);
		const bool known_address = GPR_IS_CONST1(_Rs_);
		const u32 virtual_address = known_address ? (g_cpuConstRegs[_Rs_].UL[0] + _Imm_) & ~0xFu : 0;
		if (!vtlbSoftmemCanUseImmediate(known_address, virtual_address, 16))
			recComputeAddrMasked(~0xFu); // before a possible Rs==Rt destination
		static_assert(NUM_ARM_NEON_REGS < 30, "q30 must remain allocator-reserved");
		int direct = -1;
#ifdef PS2_EE_SOFTMEM_DIRECT_QUAD_LOAD
		if (_Rt_ && !CHECK_FASTMEM && UseSoftmemOptimization(true))
			direct = _tryAllocSoftmemQuadLoad(_Rt_);
#endif
		const int destination = direct >= 0 ? direct : RQSCRATCH.GetCode();
		vtlbSoftmemRead128(destination, known_address, virtual_address, direct >= 0);
		if (_Rt_)
		{
			// This allocation can evict old homes but never uses reserved q30.
			// Both runtime arms have joined and the returned full quad is live.
			const int qd = _allocGPRtoNEONreg(_Rt_, MODE_WRITE);
			pxAssert(direct < 0 || qd == direct);
			if (qd != destination)
				armAsm->Mov(armQRegister(qd).V16B(), RQSCRATCH.V16B());
			_validateRegs();
		}
		return;
	}

	// Fastmem Rt==0 retains its original flush/discard behavior.
	recComputeAddrMasked(~0xFu);
	iFlushCall(FLUSH_VTLB);
	vtlbFastmemRead128(9, 0);
}

void recSQ()
{
	const bool useFastmem = CHECK_FASTMEM && !vtlb_IsFaultingPC(pc);

	// GE-14 fastmem path: store from the allocator-resident quad (x86
	// recStore's _allocGPRtoXMMreg(_Rt_, MODE_READ) model). The allocator
	// handles every source state: already-resident (MMI Rd / prior LQ —
	// zero-emit reuse), const rt (128 load + Ins const lower), dual-dirty
	// scalar copy (Ins + free), and the lazy-dirty pin merge
	// (armMergeEEPinIntoQuad) on the fresh-load path. An rs==rt alias is
	// order-safe either way: the address materializes into w9 from wherever
	// rt currently lives before the alloc can move it.
	if (useFastmem)
	{
		// Address first (w9 is immune to the flush and alloc emissions — see
		// recLQ).
		recComputeAddrMasked(~0xFu);
		_flushConstRegs(false);
		const int qs = _allocGPRtoNEONreg(_Rt_, MODE_READ);
		_validateRegs();
		vtlbFastmemWrite128(9, qs);
		return;
	}

	if (arm64gprs[0].inuse) _freeArm64GPR(0);
	_flushConstRegs(false);
	const bool known_address = GPR_IS_CONST1(_Rs_);
	const u32 virtual_address = known_address ? (g_cpuConstRegs[_Rs_].UL[0] + _Imm_) & ~0xFu : 0;
	if (!vtlbSoftmemCanUseImmediate(known_address, virtual_address, 16))
		recComputeAddrMasked(~0xFu); // source allocation must not move an alias base
	const int qs = _allocGPRtoNEONreg(_Rt_, MODE_READ);
	// A pre-existing quad can coexist with a newer scalar/constant lower
	// half. Reconcile only that known newer source. Do not unconditionally
	// merge a pin: after LQ/MMI the dirty quad itself can be newer than it.
	if (GPR_IS_CONST1(_Rt_))
	{
		armAsm->Mov(RXSCRATCH, static_cast<s64>(g_cpuConstRegs[_Rt_].SD[0]));
		armAsm->Ins(armQRegister(qs).V2D(), 0, RXSCRATCH);
		arm64neon[qs].mode |= MODE_WRITE;
	}
	else
	{
		for (u32 i = 0; i < NUM_ARM_GPR_REGS; i++)
		{
			if (arm64gprs[i].inuse && arm64gprs[i].type == ARM64TYPE_GPR &&
				arm64gprs[i].reg == _Rt_ && (arm64gprs[i].mode & MODE_WRITE))
			{
				armAsm->Ins(armQRegister(qs).V2D(), 0, armXRegister(i));
				arm64neon[qs].mode |= MODE_WRITE;
				_freeArm64GPRWithoutWriteback(i);
				break;
			}
		}
	}
	_validateRegs();
	vtlbSoftmemWrite128(qs, known_address, virtual_address);
}

// =====================================================================================================
//  LWC1 / SWC1 — FPU 32-bit load/store
//  LWC1: fpr[ft] = mem32(rs + imm)
//  SWC1: mem32(rs + imm) = fpr[ft]
// =====================================================================================================

#ifdef PS2_EE_SOFTMEM_FPR_PAIRS
template <typename Slow>
static void emitTaggedFprPair(a64::Register address, a64::VRegister first, a64::VRegister second,
	bool write, const Slow& emitSlow)
{
	a64::Label slow, ram, done;
	// Eight-byte alignment keeps both words inside the same 4 KiB virtual page.
	armAsm->Tst(address, 7);
	armAsm->B(&slow, a64::ne);
	vtlbSoftmemLoadPageEntry(armAsm, false, 0, address.GetCode());
	armAsm->Tbz(a64::x8, EeSoftmemIndirectBit, &ram);

	armAsm->Bind(&slow);
	emitSlow();
	armAsm->B(&done);

	armAsm->Bind(&ram);
	armAsm->Add(a64::x8, a64::x8, a64::Operand(address, a64::UXTW));
	if (write)
		armAsm->Stp(first, second, a64::MemOperand(a64::x8));
	else
		armAsm->Ldp(first, second, a64::MemOperand(a64::x8));
	armAsm->Bind(&done);
}

// Set on the owner thread before its first EE compilation.
static bool s_fprMemoryPairing = true;

void recSetFprMemoryPairing(bool enabled)
{
    s_fprMemoryPairing = enabled;
}

bool recTryFprMemoryPair(u32 second)
{
	if (!s_fprMemoryPairing || !vtlbSoftmemUsesTaggedPages() || CHECK_FPU_FULL ||
		!UseSoftmemOptimization(PS2_EE_SOFTMEM_DIRECT_FPR_LOAD))
		return false;

	const u32 first = cpuRegs.code;
	const bool write = (first >> 26) == 0x39;
	const unsigned base = (first >> 21) & 31;
	const unsigned ft0 = (first >> 16) & 31;
	const unsigned ft1 = (second >> 16) & 31;
	const s32 offset = static_cast<s16>(first);
	if ((second >> 26) != (first >> 26) || ((second >> 21) & 31) != base ||
		static_cast<s16>(second) != offset + 4 || (offset & 3) ||
		(base == 29 && (offset & 7)) || GPR_IS_CONST1(base) || (!write && ft0 == ft1) ||
		!_hasNEONreg(NEONTYPE_FPREG, ft0) || !_hasNEONreg(NEONTYPE_FPREG, ft1))
		return false;

	cop2VfCacheFlush();
	pc += 4;
	++g_pCurInstInfo;
	if (arm64gprs[0].inuse)
		_freeArm64GPR(0);
	_flushConstRegs(false);
	vtlbSoftmemPrepareGpr(base);

	const int f0 = _checkNEONreg(NEONTYPE_FPREG, ft0, 0);
	const a64::Register address = recAddrOperand();
	const auto emitScalar = [write](a64::Register index, int slot) {
		const bool direct = index.GetCode() != 9;
		if (write)
			vtlbSoftmemWrite(index.GetCode(), 10, 32, true, false, 0, direct, slot);
		else
		{
			vtlbSoftmemRead(index.GetCode(), 32, false, true, 0, false, 0, slot, direct);
			arm64neon[slot].mode |= MODE_WRITE;
		}
	};
	const int f1 = _checkNEONreg(NEONTYPE_FPREG, ft1, 0);
	emitTaggedFprPair(address, armSRegister(f0), armSRegister(f1), write, [&] {
		emitScalar(address, f0);
		cpuRegs.code = second;
		pc += 4;
		++g_pCurInstInfo;
		emitScalar(recAddrOperand(), f1);
	});
	_validateRegs();
	return true;
}
#ifdef PS2_EE_SOFTMEM_FPR_PAGE_GROUP
bool recTryFprLoadPageGroup(const u32* ops, unsigned count, s32 lowest, s32 highest)
{
	const unsigned base = (ops[0] >> 21) & 31;
	if (!s_fprMemoryPairing || !vtlbSoftmemUsesTaggedPages() ||
		GPR_IS_CONST1(base) || !UseSoftmemOptimization(PS2_EE_SOFTMEM_DIRECT_FPR_LOAD))
		return false;

	// A cold destination may use an empty slot, but never evict another
	// FPR just to group loads. MODE 0 leaves the previous architectural
	// word in memory for handlers reached before this destination is written.
	unsigned cold = 0;
	for (unsigned i = 0; i < count; ++i)
		cold += !_hasNEONreg(NEONTYPE_FPREG, (ops[i] >> 16) & 31);
	unsigned free = 0;
	for (unsigned slot = NEON_CALLEE_SAVED_START; slot < NEON_CALLEE_SAVED_END; ++slot)
		free += slot != NEON_RESERVED_EEFPU_UNSCALE && !arm64neon[slot].inuse;
	if (cold > free)
		return false;

	cop2VfCacheFlush();
	if (arm64gprs[0].inuse)
		_freeArm64GPR(0);
	_flushConstRegs(false);
	vtlbSoftmemPrepareGpr(base);

	std::array<int, 4> slots{};
	for (unsigned i = 0; i < count; ++i)
		slots[i] = _allocFPtoNEONreg((ops[i] >> 16) & 31, 0);

	recComputeAddr(lowest);
	a64::Label slow, done;
	armAsm->Tst(a64::w9, 3);
	armAsm->B(&slow, a64::ne);
	armAsm->And(a64::w10, a64::w9, VTLB_PAGE_SIZE - 1);
	armAsm->Cmp(a64::w10, VTLB_PAGE_SIZE - (highest - lowest + 4));
	armAsm->B(&slow, a64::hi);
	vtlbSoftmemLoadPageEntry(armAsm);
	armAsm->Tbnz(a64::x8, EeSoftmemIndirectBit, &slow);
	armAsm->Add(a64::x8, a64::x8, a64::Operand(a64::w9, a64::UXTW));
	for (unsigned i = 0; i < count; ++i)
	{
		const s32 offset = static_cast<s16>(ops[i]) - lowest;
		if (i + 1 < count && offset <= 252 &&
			static_cast<s16>(ops[i + 1]) - lowest == offset + 4)
		{
			armAsm->Ldp(armSRegister(slots[i]), armSRegister(slots[i + 1]), a64::MemOperand(a64::x8, offset));
			++i;
		}
		else
			armAsm->Ldr(armSRegister(slots[i]), a64::MemOperand(a64::x8, offset));
	}
	// Full FPU mode uses scaled double-format slots. The direct loads
	// have all finished before the conversion uses x8 as scratch.
	if (CHECK_FPU_FULL)
	{
		for (unsigned i = 0; i < count; ++i)
			armEmitEeFprFromS(armDRegister(slots[i]), armSRegister(slots[i]), RXSCRATCH);
	}
	armAsm->B(&done);

	// Each original load retains its own PC, handler and pending destination.
	armAsm->Bind(&slow);
	for (unsigned i = 0; i < count; ++i)
	{
		cpuRegs.code = ops[i];
		pc += 4;
		++g_pCurInstInfo;
		const auto address = recAddrOperand();
		const int slot = slots[i];
		vtlbSoftmemRead(address.GetCode(), 32, false, true, 0, false, 0, slot, address.GetCode() != 9);
		arm64neon[slot].mode |= MODE_WRITE;
	}
	armAsm->Bind(&done);
	_validateRegs();
	return true;
}
#endif

#endif

void recLWC1()
{
	// On the fast path a single inline LDR off RFASTMEMBASE + backpatch,
	// no iFlushCall and no vtlb C call. Softmem stays as the faulting-PC
	// fallback.
	const bool useFastmem = CHECK_FASTMEM && !vtlb_IsFaultingPC(pc);

	if (useFastmem)
	{
		// GE-10: the inline LDR targets ft's allocated S register directly —
		// no w0 bounce, no fpr-memory store, and ft stays resident for the
		// following FPU op (the LWC1→arith idiom). MODE_WRITE only: LWC1
		// overwrites fpr[ft] wholesale, so a prior value in the slot is dead
		// (same rule as recMTC1); the dirty slot flushes at the next seam.
		// Alloc BEFORE the fastmem emit: its eviction writeback must not
		// split the LDR from its backpatch record, and the dest must be in
		// the live-mask snapshot taken inside the emitter (where the thunk's
		// is-load-dest skip excludes it from the save set).
		const int ftreg = _allocFPtoNEONreg(_Rt_, MODE_WRITE);
		// The address after the alloc, read from wherever the base lives.
		vtlbFastmemReadFPR32(recAddrOperand().GetCode(), ftreg);
		if (CHECK_FPU_FULL)
		{
			// The load lands the architectural word; on the double tier the
			// slot holds it relocated. Emitted after the backpatch site so it
			// runs on the thunk's return path too (the slow path ends in the
			// same Fmov S<n>, w0).
			armEmitEeFprFromS(armDRegister(ftreg), armSRegister(ftreg), RXSCRATCH);
		}
	}
	else
	{
		// Retain live homes on RAM; the handler arm alone canonicalizes/saves
		// them. Keep the OLD ft mapped until that arm has observed its state.
		if (arm64gprs[0].inuse) _freeArm64GPR(0);
		_flushConstRegs(false);
		vtlbSoftmemPrepareGpr(_Rs_);
		// Reserve a direct destination before emitting the address. The old
		// dirty value remains visible to the handler arm until it returns.
		const bool direct_load = UseSoftmemOptimization(PS2_EE_SOFTMEM_DIRECT_FPR_LOAD);
		const int direct_ft = direct_load ? _allocFPtoNEONreg(_Rt_, 0) : -1;
		const bool constant_address = GPR_IS_CONST1(_Rs_);
		const u32 virtual_address = constant_address ? g_cpuConstRegs[_Rs_].UL[0] + _Imm_ : 0;
		unsigned address = 9;
		if (CHECK_FASTMEM)
			recComputeAddr();
		else if (!vtlbSoftmemCanUseImmediate(constant_address, virtual_address, 4))
			address = recAddrOperand().GetCode();
		const bool direct_address = !CHECK_FASTMEM && address != 9;
		vtlbSoftmemRead(address, 32, false, true, 0, constant_address, virtual_address, direct_ft, direct_address);
		if (direct_load)
		{
			arm64neon[direct_ft].mode |= MODE_WRITE;
			_validateRegs();
		}
		else
		{
			// Park the word in w9 before FPR retirement or allocation emits spills.
			armAsm->Mov(a64::w9, a64::w0);
			_deleteFPtoNEONreg(_Rt_, DELETE_REG_FREE_NO_WRITEBACK);
			const int ftreg = _allocFPtoNEONreg(_Rt_, MODE_WRITE);
			armEmitEeFprSlotFromWord(armDRegister(ftreg), a64::w9, RXSCRATCH);
		}
	}
}

void recSWC1()
{
	const int fslot = _checkNEONreg(NEONTYPE_FPREG, _Rt_, 0);
	const bool useFastmem = CHECK_FASTMEM && !vtlb_IsFaultingPC(pc);
	// A normal-width resident FPR already contains the architectural word.
	// Store it directly on RAM; only the handler needs an integer argument.
	const int direct_fpr = !useFastmem && !CHECK_FPU_FULL ? fslot : -1;
	if (direct_fpr < 0)
	{
		if (fslot >= 0)
			armEmitEeFprWordFromSlot(a64::w10, armDRegister(fslot), RXSCRATCH);
		else
			armEmitEeFprWordFromSlotMem(a64::w10, armCpuRegMem(&fpuRegs.fpr[_Rt_]), RXSCRATCH);
	}

	if (useFastmem)
	{
		// Address from wherever the base lives.
		vtlbFastmemWrite(recAddrOperand().GetCode(), 10, 32);
	}
	else
	{
		// w10 already holds the architectural word, including double-tier
		// narrowing. These helpers use x8/x17, never the reserved w10 value.
		if (arm64gprs[0].inuse) _freeArm64GPR(0);
		_flushConstRegs(false);
		vtlbSoftmemPrepareGpr(_Rs_);
		const bool constant_address = GPR_IS_CONST1(_Rs_);
		const u32 virtual_address = constant_address ? g_cpuConstRegs[_Rs_].UL[0] + _Imm_ : 0;
		unsigned address = 9;
		if (CHECK_FASTMEM)
			recComputeAddr();
		else if (!vtlbSoftmemCanUseImmediate(constant_address, virtual_address, 4))
			address = recAddrOperand().GetCode();
		const bool direct_address = !CHECK_FASTMEM && address != 9;
		vtlbSoftmemWrite(address, 10, 32, true, constant_address, virtual_address, direct_address, direct_fpr);
	}
}

// =====================================================================================================
//  Unaligned load/store (LWL/LWR/LDL/LDR/SWL/SWR/SDL/SDR) — inline fastmem
//  read-modify-write codegen. Mirrors x86 master's REC_LOADS/REC_STORES paths:
//  the inline path needs only FLUSH_CONSTANT_REGS plus fastmem accesses
//  (no C call on the fast path), avoiding a full FLUSH_INTERPRETER eviction.
// =====================================================================================================

// Resolve Rt's 64-bit read-modify-write home for the unaligned-load merges.
//
// A pinned Rt must NOT get an allocator slot (GE-M2 I1): under the
// resident-ALU templates the slot persists past this op, and pin-preferring
// readers (_eeGetGPRSourceReg — every scalar-template ALU consumer) keep
// serving the PRE-load mirror value until the next seam writes the slot back
// through the pin (the UYA unaligned-load corruption, 2026-07-17). Instead the
// merge targets the pin itself: it already holds the current lower 64 (a live
// 128-bit home or pending const is reconciled into it first), and under
// lazy-dirty writing the pin IS the guest write — every seam flushes all pins.
//
// Unpinned Rt keeps the allocator slot. MODE_READ|MODE_WRITE (not WRITE-only)
// for both the fill (the merge's old-value bits) and the prior-dual-residence
// reconciliation — see the fusion-path comment below (the Black boot hang).
static vixl::aarch64::Register eeUnalignedRtWriteHome()
{
	if (const a64::Register* pin = armEEPinForGPR(_Rt_))
	{
		// A live 128-bit home is authoritative over the pin; its writeback
		// (armStoreEEGPRQuad) refreshes lane 0 into the mirror. A pending
		// const materializes into the pin via _flushConstReg.
		_deleteGPRtoNEONreg(_Rt_, DELETE_REG_FREE);
		if (GPR_IS_CONST1(_Rt_))
			_flushConstReg(_Rt_);
		GPR_DEL_CONST(_Rt_);
		return *pin;
	}
	return armXRegister(_allocArm64GPR(ARM64TYPE_GPR, _Rt_, MODE_READ | MODE_WRITE));
}

// Inline LWL/LWR codegen. Mirrors x86 recLWL/recLWR (ix86-32/iR5900LoadStore.cpp).
//
//   addr    = Rs + imm
//   shift8  = (addr & 3) * 8     // kept in a callee-saved temp across the read
//   aligned = addr & ~3
//   loaded  = mem32(aligned)
//
//   LWL: Rt = sign_ext_32_to_64( (Rt & (0xffffff   >> shift8)) | (loaded << (24 - shift8)) )
//   LWR: if shift8 == 0: Rt = sign_ext_32_to_64(loaded)
//        else           : Rt[31:0] = (Rt[31:0] & (0xffffff00 << (24 - shift8))) | (loaded >> shift8)
//                         Rt[63:32] preserved   (see interpreter LWL/LWR in R5900OpcodeImpl.cpp)
//
// Uses fastmem when available (the backpatch thunk spills the live temp around
// its slow-path C call); softmem path's slow-path C call obeys AAPCS so the
// callee-saved temp survives there too.
static void recUnalignedWord(bool is_lwl)
{
	const bool useFastmem = CHECK_FASTMEM && !vtlb_IsFaultingPC(pc);

	// Rs+imm into w9. Mirrors recLoad: form the address first, then flush.
	recComputeAddr();
	iFlushCall(FLUSH_VTLB);

	// shift8 is computed before the read and consumed after it, so it must survive
	// vtlb's slow-path C call (fastmem backpatch thunk OR softmem slow path) —
	// hence callee-saved. memTemp only parks the loaded word AFTER the read, so it
	// never crosses the call: a plain temp suffices, and _allocArm64GPR's auto-set
	// `needed` flag keeps the Rt alloc below from evicting it under pressure (the
	// hazard the callee-saved home used to cover: the alloc can spill a guest reg
	// or land Rt in x0, clobbering w0 between the read and the merge — same class
	// fixed in recUnalignedLoadDouble; single-op tests miss it). EE-SRA 3 Arm B
	// holds every unaligned handler to ONE callee-saved temp so the tier-1 re-home
	// can claim x26/x27.
	const int shift8 = _allocArm64GPR(ARM64TYPE_TEMP, 0, MODE_CALLEESAVED);
	const int memTemp = _allocArm64GPR(ARM64TYPE_TEMP, 0, 0);

	armAsm->And(armWRegister(shift8), a64::w9, 3);
	armAsm->Lsl(armWRegister(shift8), armWRegister(shift8), 3);
	armAsm->And(a64::w9, a64::w9, ~3u);

	// 32-bit aligned read; result in w0.
	if (useFastmem)
		vtlbFastmemRead(9, 0, 32, false);
	else
		vtlbSoftmemRead(9, 32, false);

	if (!_Rt_)
	{
		_freeArm64GPR(memTemp);
		_freeArm64GPR(shift8);
		return;
	}

	armAsm->Mov(armWRegister(memTemp), a64::w0);                       // park loaded (x0 unsafe across Rt alloc)

	const a64::Register rt = eeUnalignedRtWriteHome();

	if (is_lwl)
	{
		// mask = 0xffffff >> shift8
		armAsm->Mov(RWSCRATCH, 0xffffff);
		armAsm->Lsr(RWSCRATCH, RWSCRATCH, armWRegister(shift8));
		armAsm->And(rt.W(), rt.W(), RWSCRATCH);

		// shifted_loaded = loaded << (24 - shift8); reuse RWSCRATCH as shift amount.
		armAsm->Mov(RWSCRATCH, 24);
		armAsm->Sub(RWSCRATCH, RWSCRATCH, armWRegister(shift8));
		armAsm->Lsl(armWRegister(memTemp), armWRegister(memTemp), RWSCRATCH);

		// Merge and sign-extend the 32-bit result into the 64-bit guest reg.
		armAsm->Orr(rt.W(), rt.W(), armWRegister(memTemp));
		armAsm->Sxtw(rt, rt.W());
	}
	else
	{
		a64::Label nomask, done;
		armAsm->Cbz(armWRegister(shift8), &nomask);

		// mask = 0xffffff00 << (24 - shift8); held in RSCRATCHADDR.W() since RWSCRATCH carries the shift amount.
		armAsm->Mov(RWSCRATCH, 24);
		armAsm->Sub(RWSCRATCH, RWSCRATCH, armWRegister(shift8));
		armAsm->Mov(RSCRATCHADDR.W(), 0xffffff00u);
		armAsm->Lsl(RSCRATCHADDR.W(), RSCRATCHADDR.W(), RWSCRATCH);
		armAsm->And(RWSCRATCH, rt.W(), RSCRATCHADDR.W());

		armAsm->Lsr(armWRegister(memTemp), armWRegister(memTemp), armWRegister(shift8));
		armAsm->Orr(armWRegister(memTemp), armWRegister(memTemp), RWSCRATCH);

		// Per interp: when shift8 != 0, only Rt[31:0] changes; upper 32 preserved.
		armAsm->Bfi(rt, armXRegister(memTemp), 0, 32);
		armAsm->B(&done);

		// shift8 == 0 (aligned): straight sign-extend, full 64-bit overwrite.
		armAsm->Bind(&nomask);
		armAsm->Sxtw(rt, armWRegister(memTemp));

		armAsm->Bind(&done);
	}

	_freeArm64GPR(memTemp);
	_freeArm64GPR(shift8);
}

// Inline SWL/SWR codegen (32-bit unaligned store, read-modify-write). Mirrors
// x86 recSWL/recSWR (ix86-32/iR5900LoadStore.cpp) and interp R5900OpcodeImpl.cpp SWL/SWR.
//
//   addr = Rs + imm ; shift8 = (addr & 3) * 8 ; aligned = addr & ~3
//   mem  = mem32(aligned)
//   SWL: mem32(aligned) = (Rt >> (24 - shift8)) | (mem & (0xffffff00 << shift8))
//   SWR: mem32(aligned) = (Rt <<       shift8 ) | (mem & (0x00ffffff >> (24 - shift8)))
//
// aligned and shift8 live in callee-saved temps so they survive the read's
// slow-path C call (fastmem backpatch thunk OR softmem slow path). The Rt load
// and merged value never cross a call. The implementation always performs a
// full RMW (no shift8==24 full-overwrite skip): the aligned word is the same
// one written, so reading it is always safe, and the general shifts already
// collapse to "store Rt" at that alignment.
static void recUnalignedStoreWord(bool is_swl)
{
	const bool useFastmem = CHECK_FASTMEM && !vtlb_IsFaultingPC(pc);

	recComputeAddr();
	iFlushCall(FLUSH_VTLB);

	// EE-SRA 3 Arm B: the full effective address is the ONLY value that must
	// survive the read's slow-path C call (it re-addresses the write-back), so it
	// is the sole callee-saved temp. The aligned address is re-masked for the read
	// and the write, and shift8 = (addr & 3) * 8 is recomputed into w10 — the
	// store-value scratch, provably free between the read and the write (only
	// vtlbSoftmemWrite writes w10, after shift8's last use here). One callee-saved
	// temp per handler leaves room for the tier-1 pin re-home to claim x26/x27.
	const int addrCS = _allocArm64GPR(ARM64TYPE_TEMP, 0, MODE_CALLEESAVED);
	armAsm->Mov(armWRegister(addrCS), a64::w9);                        // full addr

	// 32-bit aligned read; mem -> w0.
	armAsm->And(a64::w9, armWRegister(addrCS), ~3u);                   // aligned addr
	if (useFastmem)
		vtlbFastmemRead(9, 0, 32, false);
	else
		vtlbSoftmemRead(9, 32, false);

	// Load Rt after the read (never crosses a call). Handles Rt==0 -> 0.
	_eeMoveGPRtoR(a64::w1, _Rt_);

	// shift8 recomputed from the surviving full address into w10 (post-read).
	armAsm->And(a64::w10, armWRegister(addrCS), 3);
	armAsm->Lsl(a64::w10, a64::w10, 3);                                // shift8

	if (is_swl)
	{
		armAsm->Mov(RWSCRATCH, 0xffffff00u);
		armAsm->Lsl(RWSCRATCH, RWSCRATCH, a64::w10);                   // 0xffffff00 << shift8
		armAsm->And(a64::w0, a64::w0, RWSCRATCH);                      // mem & mask
		armAsm->Mov(RWSCRATCH, 24);
		armAsm->Sub(RWSCRATCH, RWSCRATCH, a64::w10);                   // 24 - shift8
		armAsm->Lsr(a64::w1, a64::w1, RWSCRATCH);                      // Rt >> (24 - shift8)
	}
	else
	{
		armAsm->Mov(RWSCRATCH, 24);
		armAsm->Sub(RWSCRATCH, RWSCRATCH, a64::w10);                   // 24 - shift8
		armAsm->Mov(RSCRATCHADDR.W(), 0x00ffffffu);
		armAsm->Lsr(RSCRATCHADDR.W(), RSCRATCHADDR.W(), RWSCRATCH);    // 0x00ffffff >> (24 - shift8)
		armAsm->And(a64::w0, a64::w0, RSCRATCHADDR.W());               // mem & mask
		armAsm->Lsl(a64::w1, a64::w1, a64::w10);                       // Rt << shift8
	}

	armAsm->Orr(a64::w0, a64::w0, a64::w1);                            // merged -> w0

	armAsm->And(a64::w9, armWRegister(addrCS), ~3u);                   // aligned addr (write)
	if (useFastmem)
		vtlbFastmemWrite(9, 0, 32);
	else
		vtlbSoftmemWrite(9, 0, 32);

	_freeArm64GPR(addrCS);
}

// Inline LDL/LDR codegen (64-bit unaligned load). Mirrors x86 recLDL/recLDR and
// interp R5900OpcodeImpl.cpp LDL/LDR.
//
//   addr = Rs + imm ; s = addr & 7 ; shift8 = s * 8 ; aligned = addr & ~7
//   mem  = mem64(aligned)
//   LDL: Rt = (Rt & (~0 >> (shift8 + 8))) | (mem << (56 - shift8))   [s==7: Rt = mem]
//   LDR: Rt = (Rt & (~0 << (64 - shift8))) | (mem >> shift8)         [s==0: Rt = mem]
//
// The degenerate alignment (LDL s==7 / LDR s==0) needs a shift by 64, which the
// AArch64 variable-shift uses mod-64 — so those map to a straight Rt = mem and
// are branched out, exactly like x86's CMOVE/skip.
static void recUnalignedLoadDouble(bool is_ldl)
{
	if (!_Rt_)
		return;

	// --- LDL/LDR pair fusion -------------------------------------------------
	// Consume: this op is the trailing half of a pair already emitted as a single
	// 64-bit load by its predecessor — emit nothing.
	if (g_eeUnalignedFused)
	{
		g_eeUnalignedFused = false;
		return;
	}

	// Lead: an unaligned 64-bit load is emitted by the game as an LDL/LDR pair on
	// the same Rt/Rs whose offsets differ by 7 — together exactly mem64(Rs + lower
	// offset), which ARM64 loads in one (unaligned) LDR x. Either order occurs
	// (PS2 is little-endian; LDR-first is the common MIPSEL idiom). pc has been
	// advanced past THIS op, so it is the partner's guest address: memRead32(pc)
	// peeks it, pc is this load's fastmem-backpatch key, and pc < block-end
	// confirms the partner is in-block (so the flag is consumed this block). Gated
	// off faulting PCs (pc here, pc+4 = the partner) so the proven single-op
	// backpatch path is taken whenever either previously faulted.
	if (!g_recompilingDelaySlot && CHECK_FASTMEM && pc < recCurrentBlockEndPC() &&
		!vtlb_IsFaultingPC(pc) && !vtlb_IsFaultingPC(pc + 4))
	{
		const u32 partner = memRead32(pc);
		const u32 partnerOp = partner >> 26;
		const u32 partnerRt = (partner >> 16) & 0x1f;
		const u32 partnerRs = (partner >> 21) & 0x1f;
		const s32 partnerImm = static_cast<s16>(partner & 0xffff);
		const u32 wantOp = is_ldl ? 0x1bu : 0x1au; // LDL(0x1A) pairs with LDR(0x1B)
		const s32 ldlImm = is_ldl ? _Imm_ : partnerImm;
		const s32 ldrImm = is_ldl ? partnerImm : _Imm_;
		if (partnerOp == wantOp && partnerRt == static_cast<u32>(_Rt_) &&
			partnerRs == static_cast<u32>(_Rs_) && (ldlImm - ldrImm) == 7)
		{
			// One unaligned 64-bit fastmem load at Rs + ldrImm into Rt. mem is
			// parked in a needed-protected temp before the Rt alloc (the alloc can
			// spill via / land Rt in x0, clobbering the loaded value — the Black
			// LDL/LDR boot-hang bug). Rt uses MODE_READ|MODE_WRITE (not WRITE-only):
			// although the fused load fully overwrites Rt, WRITE-only on a GPR dest
			// fails to reconcile a prior dual-residence (NEON) copy under register
			// pressure, so a later reader (here the SDL/SDR store half of a memcpy)
			// reads the stale copy — observed as alternating-pair zeros in the
			// MultiRegUnalignedDwordCopyBlock test. READ|WRITE forces a coherent
			// residence; the redundant old-value load is free vs the bug.
			recComputeAddr(ldrImm);
			iFlushCall(FLUSH_VTLB);
			// memTemp only parks the loaded value AFTER the read, so it never
			// crosses a call — a plain (needed-protected) temp keeps the fused
			// path off the callee-saved pool entirely (EE-SRA 3 Arm B).
			const int memTemp = _allocArm64GPR(ARM64TYPE_TEMP, 0, 0);
			vtlbFastmemRead(9, 0, 64, false);                 // mem -> x0
			armAsm->Mov(armXRegister(memTemp), a64::x0);       // park (x0 unsafe across Rt alloc)
			const a64::Register rt = eeUnalignedRtWriteHome();
			armAsm->Mov(rt, armXRegister(memTemp));
			_freeArm64GPR(memTemp);
			g_eeUnalignedFused = true;
			g_eeUnalignedFuseCount++;
			return;
		}
	}
	// --- end fusion ----------------------------------------------------------

	const bool useFastmem = CHECK_FASTMEM && !vtlb_IsFaultingPC(pc);

	recComputeAddr();
	iFlushCall(FLUSH_VTLB);

	// s = addr & 7 is computed before the read and consumed after it, so it must
	// survive the read's slow-path C call — the sole callee-saved temp. memTemp
	// only parks the loaded value AFTER the read (never crossing the call), so a
	// plain (needed-protected) temp suffices and keeps the handler to one
	// callee-saved reg for the tier-1 pin re-home (EE-SRA 3 Arm B). The Rt alloc
	// below can still spill a guest reg or land Rt in x0 under pressure — the
	// `needed` flag on memTemp keeps its parked value safe (the Black boot-hang).
	const int sTemp = _allocArm64GPR(ARM64TYPE_TEMP, 0, MODE_CALLEESAVED);
	const int memTemp = _allocArm64GPR(ARM64TYPE_TEMP, 0, 0);
	armAsm->And(armWRegister(sTemp), a64::w9, 7);
	armAsm->And(a64::w9, a64::w9, ~7u);                                // aligned

	if (useFastmem)
		vtlbFastmemRead(9, 0, 64, false);                             // mem -> x0
	else
		vtlbSoftmemRead(9, 64, false);

	armAsm->Mov(armXRegister(memTemp), a64::x0);                       // park mem (x0 unsafe across Rt alloc)

	const a64::Register rt = eeUnalignedRtWriteHome();

	a64::Label special, done;
	armAsm->Cmp(armWRegister(sTemp), is_ldl ? 7 : 0);
	armAsm->B(&special, a64::eq);

	armAsm->Lsl(RWSCRATCH, armWRegister(sTemp), 3);                    // x8 = shift8 (<=56)

	if (is_ldl)
	{
		// value: mem << (56 - shift8)
		armAsm->Mov(RSCRATCHADDR, 56);
		armAsm->Sub(RSCRATCHADDR, RSCRATCHADDR, RXSCRATCH);           // 56 - shift8
		armAsm->Lsl(armXRegister(memTemp), armXRegister(memTemp), RSCRATCHADDR);
		// mask: Rt & (~0 >> (shift8 + 8))
		armAsm->Add(RXSCRATCH, RXSCRATCH, 8);                         // shift8 + 8
		armAsm->Mov(RSCRATCHADDR, UINT64_C(0xFFFFFFFFFFFFFFFF));
		armAsm->Lsr(RSCRATCHADDR, RSCRATCHADDR, RXSCRATCH);
		armAsm->And(rt, rt, RSCRATCHADDR);
	}
	else
	{
		// mask amount = 64 - shift8
		armAsm->Mov(RSCRATCHADDR, 64);
		armAsm->Sub(RSCRATCHADDR, RSCRATCHADDR, RXSCRATCH);           // 64 - shift8
		// value: mem >> shift8
		armAsm->Lsr(armXRegister(memTemp), armXRegister(memTemp), RXSCRATCH);
		// mask: Rt & (~0 << (64 - shift8))
		armAsm->Mov(RXSCRATCH, UINT64_C(0xFFFFFFFFFFFFFFFF));
		armAsm->Lsl(RXSCRATCH, RXSCRATCH, RSCRATCHADDR);
		armAsm->And(rt, rt, RXSCRATCH);
	}

	armAsm->Orr(rt, rt, armXRegister(memTemp));
	armAsm->B(&done);

	armAsm->Bind(&special);
	armAsm->Mov(rt, armXRegister(memTemp));                            // Rt = mem

	armAsm->Bind(&done);
	_freeArm64GPR(memTemp);
	_freeArm64GPR(sTemp);
}

// Inline SDL/SDR codegen (64-bit unaligned store, read-modify-write). Mirrors
// x86 recSDL/recSDR and interp R5900OpcodeImpl.cpp SDL/SDR.
//
//   addr = Rs + imm ; s = addr & 7 ; shift8 = s * 8 ; aligned = addr & ~7
//   mem  = mem64(aligned)
//   SDL: mem64(aligned) = (Rt >> (56 - shift8)) | (mem & (~0 << (shift8 + 8)))  [s==7: store Rt]
//   SDR: mem64(aligned) = (Rt <<       shift8 ) | (mem & (~0 >> (64 - shift8))) [s==0: store Rt]
//
// Fastmem compiles use one branch-free form for every alignment (below).
// Softmem compiles keep the address in a callee-saved temp across the read,
// and the degenerate alignment (SDL s==7 / SDR s==0) stores Rt whole and skips
// the read.
static void recUnalignedStoreDouble(bool is_sdl)
{
	// --- SDL/SDR pair fusion -------------------------------------------------
	// Consume: this op is the trailing half of a pair already emitted as a single
	// 64-bit store by its predecessor — emit nothing.
	if (g_eeUnalignedFused)
	{
		g_eeUnalignedFused = false;
		return;
	}

	// Lead: the game emits an unaligned 64-bit store as an SDL/SDR pair on the
	// same Rt/Rs whose offsets differ by 7 (the textbook MIPS idiom: SDL at D+7,
	// SDR at D) — together exactly mem64(Rs + D) = Rt, which ARM64 stores in one
	// (unaligned) STR x. Either order occurs (PS2 is little-endian; SDR-first is
	// the common MIPSEL idiom). pc has been advanced past THIS op, so it is the
	// partner's guest address. Gated off faulting PCs (pc here, pc+4 = the
	// partner) so the proven single-op backpatch path is taken whenever either
	// previously faulted. Mirrors the LDL/LDR load fusion above.
	if (!g_recompilingDelaySlot && CHECK_FASTMEM && pc < recCurrentBlockEndPC() &&
		!vtlb_IsFaultingPC(pc) && !vtlb_IsFaultingPC(pc + 4))
	{
		const u32 partner = memRead32(pc);
		const u32 partnerOp = partner >> 26;
		const u32 partnerRt = (partner >> 16) & 0x1f;
		const u32 partnerRs = (partner >> 21) & 0x1f;
		const s32 partnerImm = static_cast<s16>(partner & 0xffff);
		const u32 wantOp = is_sdl ? 0x2du : 0x2cu; // SDL(0x2C) pairs with SDR(0x2D)
		const s32 sdlImm = is_sdl ? _Imm_ : partnerImm;
		const s32 sdrImm = is_sdl ? partnerImm : _Imm_;
		if (partnerOp == wantOp && partnerRt == static_cast<u32>(_Rt_) &&
			partnerRs == static_cast<u32>(_Rs_) && (sdlImm - sdrImm) == 7)
		{
			// One unaligned 64-bit fastmem store of Rt at Rs + sdrImm. The address
			// is parked in a needed-protected temp because _eeMoveGPRtoR(Rt) clobbers
			// w9; Rt goes through valTemp into x0 (the fastmem write value reg),
			// exactly like the non-fused store's whole-Rt (special) path.
			// _eeMoveGPRtoR handles Rt==0 (store $zero).
			recComputeAddr(sdrImm);
			iFlushCall(FLUSH_VTLB);
			// Neither temp crosses a call (both are consumed into w9/x0 before the
			// fastmem write), so plain needed-protected temps suffice — the fused
			// store path uses no callee-saved reg at all (EE-SRA 3 Arm B).
			const int addrTemp = _allocArm64GPR(ARM64TYPE_TEMP, 0, 0);
			const int valTemp = _allocArm64GPR(ARM64TYPE_TEMP, 0, 0);
			armAsm->Mov(armWRegister(addrTemp), a64::w9);     // park addr
			_eeMoveGPRtoR(armXRegister(valTemp), _Rt_);        // Rt (64-bit)
			// Read the address into w9 BEFORE loading the value into x0: addrTemp is
			// now a plain temp that can itself be x0, and the value move would then
			// overwrite the address (the callee-saved home used to preclude x0).
			armAsm->Mov(a64::w9, armWRegister(addrTemp));       // addr -> w9
			armAsm->Mov(a64::x0, armXRegister(valTemp));        // value -> x0
			vtlbFastmemWrite(9, 0, 64);
			_freeArm64GPR(valTemp);
			_freeArm64GPR(addrTemp);
			g_eeUnalignedFused = true;
			g_eeUnalignedFuseCount++;
			return;
		}
	}
	// --- end fusion ----------------------------------------------------------

	const bool useFastmem = CHECK_FASTMEM && !vtlb_IsFaultingPC(pc);

	// A pinned base with no offset is read after the flush, from the pin (the
	// flush is what writes a dirty 128-bit copy back into it; see recLoad), and
	// used as it is. Any other base is formed in w9 before the flush, while its
	// slot is still allocated.
	const a64::Register* basePin = (useFastmem && _Imm_ == 0 && !GPR_IS_CONST1(_Rs_)) ? armEEPinForGPR(_Rs_) : nullptr;
	if (!basePin)
		recComputeAddr();
	iFlushCall(FLUSH_VTLB);

	// Fastmem: one branch-free merge for every alignment, with the shift as a
	// register operand.
	//   SDR: mem = (mem & ~(~0 << s)) | (Rt << s),  s = (addr & 7) * 8
	//   SDL: mem = (mem & ~(~0 >> s)) | (Rt >> s),  s = (7 - (addr & 7)) * 8
	// s == 0 is the degenerate alignment and gives Rt whole, as the two-path
	// form below does; unlike it, the doubleword is read in that case too,
	// which is what the interpreter does. A read of RAM has no effect, and an
	// address that is not RAM faults on its first access, after which the PC
	// compiles softmem, below, with the read skipped as before.
	// w10 holds s across the read: the inline access touches nothing, and the
	// load backpatch thunk touches only w9 (the address, unchanged), x8, x17
	// and x0, with its slow call preserve_most (x9-x15 spared). w9 keeps the
	// aligned address for the write the same way.
	if (useFastmem)
	{
		const a64::Register addr = basePin ? basePin->W() : a64::w9;
		if (is_sdl)
		{
			armAsm->Mvn(a64::w10, addr);
			armAsm->Ubfiz(a64::w10, a64::w10, 3, 3);                // s = (~addr & 7) * 8
		}
		else
		{
			armAsm->Ubfiz(a64::w10, addr, 3, 3);                    // s = (addr & 7) * 8
		}
		armAsm->And(a64::w9, addr, ~7u);                            // aligned
		vtlbFastmemRead(9, 0, 64, false);                          // mem -> x0
		const a64::Register rt = _eeGetGPRSourceReg(a64::x1, _Rt_); // handles Rt == 0
		if (is_sdl)
		{
			armAsm->Lsr(a64::x1, rt, a64::x10);
			armAsm->Mov(RXSCRATCH, UINT64_C(0xFFFFFFFFFFFFFFFF));
			armAsm->Lsr(RXSCRATCH, RXSCRATCH, a64::x10);
		}
		else
		{
			armAsm->Lsl(a64::x1, rt, a64::x10);
			armAsm->Mov(RXSCRATCH, UINT64_C(0xFFFFFFFFFFFFFFFF));
			armAsm->Lsl(RXSCRATCH, RXSCRATCH, a64::x10);
		}
		armAsm->Bic(a64::x0, a64::x0, RXSCRATCH);
		armAsm->Orr(a64::x0, a64::x0, a64::x1);
		vtlbFastmemWrite(9, 0, 64);
		return;
	}

	// Softmem (a PC that has faulted: the address is not RAM).
	// EE-SRA 3 Arm B: only the full effective address must survive the read's
	// slow-path C call (it re-addresses the write-back), so it is the sole
	// callee-saved temp. s = addr & 7 drives the degenerate-alignment branch and
	// is recomputed from the address for shift8 after the read; Rt is re-derived
	// per path via _eeMoveGPRtoR (the read writes only x0, never Rt's memory), so
	// nothing but the address rides the call. One callee-saved temp per handler
	// leaves room for the tier-1 pin re-home.
	const int addrCS = _allocArm64GPR(ARM64TYPE_TEMP, 0, MODE_CALLEESAVED);
	armAsm->Mov(armWRegister(addrCS), a64::w9);                       // full addr

	a64::Label special, merged;
	armAsm->And(RWSCRATCH, armWRegister(addrCS), 7);                  // s (transient, for the branch)
	armAsm->Cmp(RWSCRATCH, is_sdl ? 7 : 0);
	armAsm->B(&special, a64::eq);

	// General path: read aligned word, merge with Rt.
	armAsm->And(a64::w9, armWRegister(addrCS), ~7u);                  // aligned
	vtlbSoftmemRead(9, 64, false);                                    // mem -> x0

	_eeMoveGPRtoR(a64::x1, _Rt_);                                     // Rt (post-read), handles Rt==0
	armAsm->And(RWSCRATCH, armWRegister(addrCS), 7);                  // s, recomputed post-read
	armAsm->Lsl(RWSCRATCH, RWSCRATCH, 3);                            // x8 = shift8 = s * 8

	if (is_sdl)
	{
		// Rt >> (56 - shift8)
		armAsm->Mov(RSCRATCHADDR, 56);
		armAsm->Sub(RSCRATCHADDR, RSCRATCHADDR, RXSCRATCH);          // 56 - shift8
		armAsm->Lsr(a64::x1, a64::x1, RSCRATCHADDR);
		// mem & (~0 << (shift8 + 8))
		armAsm->Add(RXSCRATCH, RXSCRATCH, 8);                        // shift8 + 8
		armAsm->Mov(RSCRATCHADDR, UINT64_C(0xFFFFFFFFFFFFFFFF));
		armAsm->Lsl(RSCRATCHADDR, RSCRATCHADDR, RXSCRATCH);
		armAsm->And(a64::x0, a64::x0, RSCRATCHADDR);
	}
	else
	{
		// Rt << shift8
		armAsm->Lsl(a64::x1, a64::x1, RXSCRATCH);
		// mem & (~0 >> (64 - shift8))
		armAsm->Mov(RSCRATCHADDR, 64);
		armAsm->Sub(RSCRATCHADDR, RSCRATCHADDR, RXSCRATCH);          // 64 - shift8
		armAsm->Mov(RXSCRATCH, UINT64_C(0xFFFFFFFFFFFFFFFF));
		armAsm->Lsr(RXSCRATCH, RXSCRATCH, RSCRATCHADDR);
		armAsm->And(a64::x0, a64::x0, RXSCRATCH);
	}

	armAsm->Orr(a64::x0, a64::x0, a64::x1);                          // merged -> x0
	armAsm->B(&merged);

	armAsm->Bind(&special);
	_eeMoveGPRtoR(a64::x0, _Rt_);                                     // store Rt whole (no read)

	armAsm->Bind(&merged);
	armAsm->And(a64::w9, armWRegister(addrCS), ~7u);                  // aligned (write)
	vtlbSoftmemWrite(9, 0, 64);

	_freeArm64GPR(addrCS);
}

void recLWL() { recUnalignedWord(true);  }
void recLWR() { recUnalignedWord(false); }
void recLDL() { recUnalignedLoadDouble(true);  }
void recLDR() { recUnalignedLoadDouble(false); }
void recSWL() { recUnalignedStoreWord(true);  }
void recSWR() { recUnalignedStoreWord(false); }
void recSDL() { recUnalignedStoreDouble(true);  }
void recSDR() { recUnalignedStoreDouble(false); }
// =====================================================================================================
//  LQC2 / SQC2 — 128-bit COP2 (VU0) register load/store
//  LQC2: VU0.VF[ft] = mem128((rs + imm) & ~0xF)
//  SQC2: mem128((rs + imm) & ~0xF) = VU0.VF[ft]
//  Same as LQ/SQ but target is VU0.VF[ft] instead of cpuRegs.GPR[rt].
//  Requires vu0Sync() before access.
// =====================================================================================================

void recLQC2()
{
	// Sync VU0 before COP2 register access. Gated on EEINST analysis —
	// no emit at all when the analysis says no sync is needed (the common
	// case for quad-load-heavy code like vertex streaming). Mirrors x86
	// recLQC2 (mVUSyncVU0 / mVUFinishVU0 gating on EEINST_COP2_SYNC_VU0 /
	// EEINST_COP2_FINISH_VU0). The helper handles iFlushCall, RECCYCLE
	// save/reload, runtime VPU_STAT check, and cycle accounting.
	cop2EmitConditionalSync(false, _vu0FinishMicro);

	const bool useFastmem = CHECK_FASTMEM && !vtlb_IsFaultingPC(pc);

	// S4-3 fastmem path: no iFlushCall — the allocator (caller-saved scalar
	// GPRs, FPRC, NEON quads/FPRs) rides through, matching the GE-14 recLQ
	// shape. Address from live registers via recComputeAddr; only
	// constants must be memory-visible for the fault-path C handler. The
	// quad lands in RQSCRATCH (q30) — per-op scratch, never allocator-
	// tracked, so no q0 detach is needed — and is stored to VU0.VF[ft]
	// directly (ft's memory is current here: LQC2 is VF-cache-classifier-
	// false, so recompileNextInstruction flushed the COP2 VF compile cache
	// before this emitter ran, and VF regs are never EE-allocator-tracked).
	if (useFastmem)
	{
		recComputeAddrMasked(~0xFu);
		_flushConstRegs(false);
		vtlbFastmemRead128(9, RQSCRATCH.GetCode());
		if (_Rt_)
			armAsm->Str(RQSCRATCH, armVU0Mem(&VU0.VF[_Rt_]));
		return;
	}

	// The VF cache was written back before this opcode. The remaining
	// allocator state only needs canonicalization on the MMIO arm.
	if (arm64gprs[0].inuse)
		_freeArm64GPR(0);
	_flushConstRegs(false);
	const bool known_address = GPR_IS_CONST1(_Rs_);
	const u32 virtual_address = known_address ? (g_cpuConstRegs[_Rs_].UL[0] + _Imm_) & ~0xFu : 0;
	if (!vtlbSoftmemCanUseImmediate(known_address, virtual_address, 16))
		recComputeAddrMasked(~0xFu);
	vtlbSoftmemRead128(RQSCRATCH.GetCode(), known_address, virtual_address);
	if (_Rt_)
		armAsm->Str(RQSCRATCH, armVU0Mem(&VU0.VF[_Rt_]));
}

void recSQC2()
{
	// EEINST-gated VU0 sync — see recLQC2 above.
	cop2EmitConditionalSync(false, _vu0FinishMicro);

	const bool useFastmem = CHECK_FASTMEM && !vtlb_IsFaultingPC(pc);

	// S4-3 fastmem path: no iFlushCall — see recLQC2. The source quad is
	// read from VU0.VF[ft] memory into RQSCRATCH (q30, never allocator-
	// tracked): ft's memory is current because the VF compile cache was
	// flushed (dirty writebacks emitted) before this emitter ran.
	if (useFastmem)
	{
		recComputeAddrMasked(~0xFu);
		_flushConstRegs(false);
		armAsm->Ldr(RQSCRATCH, armVU0Mem(&VU0.VF[_Rt_]));
		vtlbFastmemWrite128(9, RQSCRATCH.GetCode());
		return;
	}

	// q30 is reserved, so the source never overwrites a live mapped quad.
	if (arm64gprs[0].inuse)
		_freeArm64GPR(0);
	_flushConstRegs(false);
	const bool known_address = GPR_IS_CONST1(_Rs_);
	const u32 virtual_address = known_address ? (g_cpuConstRegs[_Rs_].UL[0] + _Imm_) & ~0xFu : 0;
	if (!vtlbSoftmemCanUseImmediate(known_address, virtual_address, 16))
		recComputeAddrMasked(~0xFu);
	armAsm->Ldr(RQSCRATCH, armVU0Mem(&VU0.VF[_Rt_]));
	vtlbSoftmemWrite128(RQSCRATCH.GetCode(), known_address, virtual_address);
}

#ifdef PS2_EE_SOFTMEM_COP2_BATCH
bool recTryCop2SoftmemBatch(const u32* ops, unsigned count)
{
	if (!vtlbSoftmemUsesTaggedPages() || GPR_IS_CONST1(_Rs_))
		return false;
	for (unsigned i = 0; i < count; ++i)
	{
		if (g_pCurInstInfo[i + 1].info & (EEINST_COP2_SYNC_VU0 | EEINST_COP2_FINISH_VU0))
			return false;
	}

	// The normal quad memory opcode has exactly this allocator seam.
	cop2VfCacheFlush();
	if (arm64gprs[0].inuse)
		_freeArm64GPR(0);
	_flushConstRegs(false);
	recComputeAddrMasked(~0xFu);

	a64::Label slow, done;
	const bool write = (ops[0] >> 26) == 0x3e;
	const unsigned rt = (ops[0] >> 16) & 31;
	// First effective address is already aligned, and the following
	// displacements are each +16. One unsigned compare excludes page
	// crossings and guest address wrap. A tagged PTE excludes MMIO.
	armAsm->And(a64::w8, a64::w9, VTLB_PAGE_SIZE - 1);
	armAsm->Cmp(a64::w8, VTLB_PAGE_SIZE - count * 16);
	armAsm->B(&slow, a64::hi);
	vtlbSoftmemLoadPageEntry(armAsm);
	armAsm->Tbnz(a64::x8, EeSoftmemIndirectBit, &slow);
	armAsm->Add(a64::x8, a64::x8, a64::Operand(a64::w9, a64::UXTW));

	for (unsigned i = 0; i + 1 < count; i += 2)
	{
		const auto ram = a64::MemOperand(a64::x8, i * 16);
		const auto vf = armVU0Mem(&VU0.VF[rt + i]);
		if (write)
		{
			armAsm->Ldp(RQSCRATCH, RQSCRATCH2, vf);
			armAsm->Stp(RQSCRATCH, RQSCRATCH2, ram);
		}
		else
		{
			armAsm->Ldp(RQSCRATCH, RQSCRATCH2, ram);
			armAsm->Stp(RQSCRATCH, RQSCRATCH2, vf);
		}
	}
	if (count & 1)
	{
		const auto ram = a64::MemOperand(a64::x8, (count - 1) * 16);
		const auto vf = armVU0Mem(&VU0.VF[rt + count - 1]);
		armAsm->Ldr(RQSCRATCH, write ? vf : ram);
		armAsm->Str(RQSCRATCH, write ? ram : vf);
	}
	armAsm->B(&done);
	armAsm->Bind(&slow);
	// One fallback per original guest instruction. Each one independently
	// resolves its PTE and runs its normal handler/cycle recovery path.
	for (unsigned i = 0; i < count; ++i)
	{
		cpuRegs.code = ops[i];
		pc += 4;
		++g_pCurInstInfo;
		(write ? recSQC2 : recLQC2)();
		_clearNeededArm64GPRregs();
		_clearNeededNEONregs();
	}
	armAsm->Bind(&done);
	return true;
}
#endif

} // namespace OpcodeImpl
} // namespace Dynarec
} // namespace R5900
