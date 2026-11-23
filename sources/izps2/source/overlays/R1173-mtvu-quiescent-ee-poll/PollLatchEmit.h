// SPDX-License-Identifier: GPL-3.0+
// Included in owning191 after the tracked-span helper, not a separate TU.
#pragma once

#ifndef PS2_NATIVE_CROSSBLOCK_POLL_LATCH
#define PS2_NATIVE_CROSSBLOCK_POLL_LATCH 0
#endif
#ifndef PS2_NATIVE_POLL_WRITER_PROFILE
#define PS2_NATIVE_POLL_WRITER_PROFILE 0
#endif

#if PS2_NATIVE_CROSSBLOCK_POLL_LATCH
#include "PollLatchPlan.h"
#include "PollMainRamSegment.h"
#ifdef IZPS2_EE_POLL_WATCHED_CODE
#include "EePollCodeEpoch.h"
#endif
#if defined(PS2_NATIVE_POLL_LATCH_ORACLE) && PS2_NATIVE_POLL_LATCH_ORACLE
#include <cstdio>
#endif

namespace
{
// These four caller-saved allocator slots were freed by the existing tail's
// iFlushCall. x8/x9/x10/x17 are reserved scratch, never dedicated EE pins.
static const a64::Register s_pollScratch[] = {a64::x0, a64::x1, a64::x2, a64::x3};
// x14 is caller-saved, freed by iFlushCall, and is not an EE pin or an
// assembler scratch register. No calls or mapping writers run in this guard.
static const a64::Register s_pollVmap = a64::x14;
#if defined(IZPS2_SWITCH2_NATIVE_POLL_PAGES) && defined(IZPS2_SWITCH2_NATIVE_POLL_ADDRESS_CHAIN)
static const a64::Register s_pollMainRam = a64::x15;
#endif

#ifdef IZPS2_SWITCH2_NATIVE_POLL_CACHE
// FLUSH_EVERYTHING frees these caller-saved allocator slots before the guard.
// Each validated MainRAM value is live only until this guard returns or
// jumps to DispatcherEvent. No code in between calls out or changes MainRAM.
static const a64::Register s_pollValues[] = {a64::x4, a64::x5, a64::x6, a64::x7};
static_assert(std::size(s_pollValues) == PollLatch::MaxReads);
static constexpr bool s_pollCachesReads = true;
#else
static constexpr bool s_pollCachesReads = false;
#endif

static void emitPollRead(const PollLatch::Read& read, const a64::Register& dst, a64::Label* fail,
	[[maybe_unused]] bool reusePage = false, [[maybe_unused]] const PollLatch::Read* previous = nullptr,
	bool reuseMapping = false)
{
	// Refuse effective-address wrap, noncanonical aliases, out-of-RAM and
	// unaligned accesses BEFORE touching guest memory. A refusal is baseline.
#ifdef IZPS2_SWITCH2_NATIVE_POLL_ADDRESS_CHAIN
	// x3 belongs to the read phase until the predicates begin. On a successful
	// predecessor it holds a checked 32-bit effective address, so the signed
	// difference of offsets reconstructs the same 64-bit sum without a GPR reload.
	const auto effective = a64::x3;
	const bool chained = previous && previous->base == read.base;
	if (!chained)
	{
		if (read.base)
			armLoadEERegPtr(effective.W(), &cpuRegs.GPR.r[read.base].UL[0]);
		else
			armAsm->Mov(effective.W(), 0);
	}
	const s32 displacement = static_cast<s32>(read.immediate) - (chained ? previous->immediate : 0);
	if (displacement > 0)
		armAsm->Add(effective, effective, displacement);
	else if (displacement < 0)
		armAsm->Sub(effective, effective, -displacement);
#else
	const auto effective = a64::x8;
	if (read.base)
		armLoadEERegPtr(effective.W(), &cpuRegs.GPR.r[read.base].UL[0]);
	else armAsm->Mov(effective.W(), 0);
	armAsm->Mov(a64::x9, static_cast<s64>(read.immediate));
	armAsm->Add(effective, effective, a64::x9);
#endif
#ifdef IZPS2_SWITCH2_NATIVE_POLL_PAGES
	a64::Label load;
#endif
#if defined(IZPS2_SWITCH2_NATIVE_POLL_PAGES) && defined(IZPS2_SWITCH2_NATIVE_POLL_ADDRESS_CHAIN)
	// A chained address is full-width. The verified predecessor's virtual
	// page comparison rejects overflow and underflow without a separate test.
	// Word alignment survives an aligned delta. Byte loads need no alignment.
	const bool tryPageFirst = reusePage && chained &&
		(read.bytes == 1 || (previous->bytes == 4 && displacement % 4 == 0));
	if (tryPageFirst)
	{
		armAsm->Lsr(a64::x10, effective, vtlb_private::VTLB_PAGE_BITS);
		armAsm->Cmp(a64::x10, a64::x2);
		armAsm->B(&load, a64::eq);
	}
#else
	constexpr bool tryPageFirst = false;
#endif
#if defined(IZPS2_SWITCH2_NATIVE_POLL_PAGES) && defined(IZPS2_SWITCH2_NATIVE_POLL_ADDRESS_CHAIN)
	const bool rawPageCheck = std::has_single_bit(Ps2MemSize::ExposedRam) &&
		Ps2MemSize::ExposedRam >= 4 && Ps2MemSize::ExposedRam <= 0x20000000u;
#else
	constexpr bool rawPageCheck = false;
#endif
	if (rawPageCheck)
	{
		EmitPollMainRamSegment(*armAsm, effective, a64::x9, Ps2MemSize::ExposedRam, read.bytes, fail);
	}
	else
	{
		armAsm->Lsr(a64::x9, effective, 32);
		armAsm->Cbnz(a64::x9, fail);
		if (read.bytes == 4 && !tryPageFirst)
		{
			armAsm->Tst(effective.W(), 3);
			armAsm->B(fail, a64::ne);
		}
	}
#ifdef IZPS2_SWITCH2_NATIVE_POLL_PAGES
	// The read phase owns x1/x2 until expression emission. Page identity
	// includes the virtual segment, and wrap/alignment were checked above.
	static_assert(Ps2MemSize::MainRam % vtlb_private::VTLB_PAGE_SIZE == 0 &&
		Ps2MemSize::TotalRam % vtlb_private::VTLB_PAGE_SIZE == 0);
	const auto page = (reusePage || tryPageFirst) ? a64::w10 : a64::w2;
	if (!tryPageFirst)
		armAsm->Lsr(page, effective.W(), vtlb_private::VTLB_PAGE_BITS);
	if (reusePage && !tryPageFirst)
	{
		armAsm->Cmp(a64::w10, a64::w2);
		armAsm->B(&load, a64::eq);
	}
#endif
	if (!rawPageCheck)
	{
		// A naturally aligned access wholly inside the low RAM segment needs
		// neither an alias check nor a separate physical-address bounds test.
		a64::Label inRam;
		const bool lowSegmentTest = std::has_single_bit(Ps2MemSize::ExposedRam) &&
			Ps2MemSize::ExposedRam >= read.bytes && Ps2MemSize::ExposedRam <= 0x20000000u;
		if (lowSegmentTest)
		{
			armAsm->Mov(a64::w9, effective.W());
			armAsm->Tst(a64::w9, ~(Ps2MemSize::ExposedRam - 1));
			armAsm->B(&inRam, a64::eq);
		}
		a64::Label canonical;
		armAsm->Lsr(a64::w9, effective.W(), 29);
		armAsm->Cbz(a64::w9, &canonical);
		armAsm->Cmp(a64::w9, 4);
		armAsm->B(&canonical, a64::eq);
		armAsm->Cmp(a64::w9, 5);
		armAsm->B(fail, a64::ne);
		armAsm->Bind(&canonical);
		armAsm->And(a64::w9, effective.W(), 0x1fffffffu);
		armAsm->Cmp(a64::w9, Ps2MemSize::ExposedRam - read.bytes);
		armAsm->B(fail, a64::hi);
		armAsm->Bind(&inRam);
	}
	// The actual current vmap, not an assumed canonical alias: this exact
	// equality excludes handlers, scratch/VU memory and remapped TLB pages.
	static_assert(sizeof(vtlb_private::VTLBVirtual) == sizeof(uptr));
	const auto mapping = reuseMapping ? s_pollVmap : a64::x17;
	if (!reuseMapping)
	{
		armLoadPtr(mapping, &vtlb_private::vtlbdata.vmap);
		armAsm->Cbz(mapping, fail);
	}
#ifdef IZPS2_SWITCH2_NATIVE_POLL_PAGES
	armAsm->Ldr(a64::x1, a64::MemOperand(mapping, page.X(), a64::LSL, 3));
	if (page.GetCode() != a64::w2.GetCode())
		armAsm->Mov(a64::w2, page);
#if defined(IZPS2_SWITCH2_NATIVE_POLL_ADDRESS_CHAIN)
	if (rawPageCheck)
	{
		// A direct vmap entry stores host page minus virtual page. Once
		// the segment is qualified, compare that bias without adding EA.
		const auto mainRam = reuseMapping ? s_pollMainRam : a64::x10;
		if (!reuseMapping)
			armMoveAddressToReg(mainRam, &eeMem->Main[0]);
		EmitPollMainRamMapCheck(*armAsm, a64::x1, mainRam, a64::x9, fail);
	}
	else
#endif
	{
		armAsm->Add(a64::x17, a64::x1, effective);
		armMoveAddressToReg(a64::x10, &eeMem->Main[0]);
		armAsm->Add(a64::x10, a64::x10, a64::x9);
		armAsm->Cmp(a64::x17, a64::x10);
		armAsm->B(fail, a64::ne);
	}
#else
	armAsm->Lsr(a64::w10, effective.W(), vtlb_private::VTLB_PAGE_BITS);
	armAsm->Ldr(a64::x17, a64::MemOperand(mapping, a64::x10, a64::LSL, 3));
	armAsm->Add(a64::x17, a64::x17, effective);
	armMoveAddressToReg(a64::x10, &eeMem->Main[0]);
	armAsm->Add(a64::x10, a64::x10, a64::x9);
	armAsm->Cmp(a64::x17, a64::x10);
	armAsm->B(fail, a64::ne);
#endif
#ifdef IZPS2_SWITCH2_NATIVE_POLL_PAGES
	armAsm->Bind(&load);
	// A previously verified direct page covers any aligned word and any byte
	// in that page. No calls or writes intervene between guard reads.
	const a64::MemOperand address(a64::x1, effective.W(), a64::UXTW);
#else
	const a64::MemOperand address(a64::x10);
#endif
	if (read.bytes == 4) armAsm->Ldrsw(dst, address);
	else armAsm->Ldrb(dst.W(), address);
}

static void emitPollExpression(const PollLatch::Plan& p, u32 id, u32 scratch, a64::Label* fail)
{
	const PollLatch::Node& n = p.nodes[id];
	const a64::Register& dst = s_pollScratch[scratch];
	switch (n.op)
	{
		case PollLatch::Op::Input:
			armLoadEERegPtr(dst, &cpuRegs.GPR.r[n.a].UD[0]); return;
		case PollLatch::Op::Constant: armAsm->Mov(dst, n.value); return;
		case PollLatch::Op::Load32:
		case PollLatch::Op::Load8:
#ifdef IZPS2_SWITCH2_NATIVE_POLL_CACHE
			armAsm->Mov(dst, s_pollValues[n.a]);
#else
			emitPollRead(p.reads[n.a], dst, fail);
#endif
			return;
		default: break;
	}
	emitPollExpression(p, n.a, scratch, fail);
	emitPollExpression(p, n.b, scratch + 1, fail);
	const a64::Register& rhs = s_pollScratch[scratch + 1];
	switch (n.op)
	{
		case PollLatch::Op::Add64: armAsm->Add(dst, dst, rhs); break;
		case PollLatch::Op::Add32:
			armAsm->Add(dst.W(), dst.W(), rhs.W()); armAsm->Sxtw(dst, dst.W()); break;
		case PollLatch::Op::And: armAsm->And(dst, dst, rhs); break;
		case PollLatch::Op::Or: armAsm->Orr(dst, dst, rhs); break;
		case PollLatch::Op::Xor: armAsm->Eor(dst, dst, rhs); break;
		default: pxAssert(false); break;
	}
}

#ifdef IZPS2_SWITCH2_NATIVE_POLL_COMPARE
// Comparisons can consume validated values in place. Recursive emission still
// uses x0–x3, leaving the cached reads in x4–x7 untouched.
static a64::Register pollCompareOperand(const PollLatch::Plan& plan, u32 id, u32 scratch, a64::Label* fail)
{
	const auto& node = plan.nodes[id];
	if (node.op == PollLatch::Op::Load32 || node.op == PollLatch::Op::Load8)
		return s_pollValues[node.a];
	if (node.op == PollLatch::Op::Input)
	{
		if (const auto* pin = armEEPinForGPR(node.a))
			return *pin;
	}
	emitPollExpression(plan, id, scratch, fail);
	return s_pollScratch[scratch];
}

static void emitPollTest(const PollLatch::Plan& plan, const PollLatch::Test& test,
	a64::Label* fail, a64::Label* next)
{
	u32 lhs = test.a;
	u32 rhs = test.b;
	if (plan.nodes[lhs].op == PollLatch::Op::Constant)
		std::swap(lhs, rhs);
	const auto left = pollCompareOperand(plan, lhs, 0, fail);
	const auto& right = plan.nodes[rhs];
	if (right.op == PollLatch::Op::Constant && right.value == 0)
	{
		if (test.equal)
			armAsm->Cbnz(left, next);
		else
			armAsm->Cbz(left, next);
		return;
	}
	if (right.op == PollLatch::Op::Constant && right.value <= 4095)
		armAsm->Cmp(left, right.value);
	else
		armAsm->Cmp(left, pollCompareOperand(plan, rhs, 1, fail));
	armAsm->B(next, test.equal ? a64::ne : a64::eq);
}
#endif

static bool pollExpressionRegistersFit(const PollLatch::Plan& p)
{
	const std::vector<PollLatch::EmissionBound> bounds = PollLatch::EmissionBounds(p);
	u32 emittedNodes = 0;
	for (const PollLatch::Trace& t : p.traces)
	{
		for (const PollLatch::Test& test : t.tests)
		{
			if (bounds[test.a].registers > 4 || bounds[test.b].registers > 3 ||
				bounds[test.a].treeNodes > 64 || bounds[test.b].treeNodes > 64) return false;
			emittedNodes += bounds[test.a].treeNodes + bounds[test.b].treeNodes;
		}
		for (u32 r = 1; r != 32; ++r)
		{
			if (!(t.writes & (u32{1} << r))) continue;
			if (bounds[t.result[r]].registers > 4 || bounds[t.result[r]].treeNodes > 64) return false;
			emittedNodes += bounds[t.result[r]].treeNodes;
		}
		if (emittedNodes > 1024) return false;
	}
	return true;
}

static bool pollSpanHasSemanticEntry(u32 first, u32 bytes)
{
	const u32 elf = VMManager::Internal::GetCurrentELFEntryPoint();
	for (u32 off = 0; off < bytes; off += 4)
	{
		const u32 physical = HWADDR(first + off);
		if (physical == EELOAD_START || (g_eeloadMain && physical == HWADDR(g_eeloadMain)) ||
			(g_eeloadExec && physical == HWADDR(g_eeloadExec)) || (elf && physical == HWADDR(elf))) return true;
	}
	return false;
}

static void emitPollInstructionSnapshot(const u8* live, const u8* expected, u32 bytes, a64::Label* fallback)
{
	armMoveAddressToReg(RSCRATCHADDR, live);
	armMoveAddressToReg(RXSCRATCH, expected);
#ifdef IZPS2_SWITCH2_NATIVE_POLL_SIMD
	if (bytes >= 64)
	{
		for (u32 off = 0; off < (bytes & ~63u); off += 64)
			emitEeSnapshotDifference64(off == 0);

		const a64::VRegister accum(29, 128);
		armAsm->Umaxv(accum.S(), accum.V4S());
		armAsm->Fmov(a64::w9, accum.S());
		armAsm->Cbnz(a64::w9, fallback);
		bytes &= 63u;
	}
	if (bytes)
		emitEeSnapshotDifferenceTail(bytes, fallback);
#else
	for (u32 off = 0; off < bytes; off += 4)
	{
		armAsm->Ldr(a64::w9, a64::MemOperand(RSCRATCHADDR, off));
		armAsm->Ldr(a64::w10, a64::MemOperand(RXSCRATCH, off));
		armAsm->Cmp(a64::w9, a64::w10);
		armAsm->B(fallback, a64::ne);
	}
#endif
}

static void emitGuardedPollLatch(u32 imm)
{
#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
	if (!NativePoll::requested)
		return;
#endif
	// Profile1 retains the passed local oracle. Profile2 additionally needs
	// the private actual native-entry lease below. Unknown builds emit no
	// fastpath; source-only production eligibility remains unqualified.
#if (PS2_NATIVE_POLL_WRITER_PROFILE == 1 && defined(PS2_NATIVE_POLL_LATCH_ORACLE) && PS2_NATIVE_POLL_LATCH_ORACLE) || PS2_NATIVE_POLL_WRITER_PROFILE == 2
	if (!EmuConfig.Speedhacks.WaitLoop || IopThread::GetMode() != IopThreadMode::Off ||
		EmuConfig.Trace.Enabled || s_loopSemanticEntryHook || EmuConfig.Gamefixes.GoemonTlbHack ||
		!CBreakPoints::GetBreakpoints(BREAKPOINT_EE, true).empty() || CBreakPoints::GetNumMemchecks() ||
		!s_branchLoopable || imm != s_branchTo || !s_pCurBlockEx ||
		HWADDR(imm) >= s_pCurBlockEx->startpc || s_nBlockFF ||
		pc != s_nEndBlock || s_nEndBlock < 8 || s_nEndBlock <= imm ||
		s_nEndBlock - imm > PollLatch::MaxWords * 4) return;
	const u32 bytes = s_nEndBlock - imm, physical = HWADDR(imm);
	if (physical >= Ps2MemSize::ExposedRam || bytes > Ps2MemSize::ExposedRam - physical) return;
	const u8* live = reinterpret_cast<const u8*>(PSM(physical));
	if ((physical >> 12) + 1 < ((physical + bytes - 1) >> 12) ||
		!manualLoopSmcSpanTracked(imm, physical, bytes, live) || pollSpanHasSemanticEntry(imm, bytes)) return;
	std::vector<u32> words(bytes / 4);
	std::memcpy(words.data(), live, bytes);
	PollLatch::Compiler compiler;
	PollLatch::Plan plan;
	if (!compiler.Build(imm, s_nEndBlock - 8, words, plan) || !pollExpressionRegistersFit(plan)) return;
	const u32 bound = PollLatch::GuardBytes(plan, s_pollCachesReads)
#if PS2_NATIVE_POLL_WRITER_PROFILE == 2
		+ 32 // actual private lease load/branch including full address materialization
#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
		+ (NativePoll::EmitMtvuQuietCheck() ? 32 : 0)
#endif
#endif
#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
		+ 32 * static_cast<u32>(plan.traces.size()) // EE-owner skip counter on each accepted trace
#endif
#ifdef IZPS2_EE_POLL_WATCHED_CODE
		+ 48 // load epoch, materialize expected value, compare and branch
#endif
		;
	const uptr hotBase = reinterpret_cast<uptr>(SysMemory::GetEERec());
	const uptr hotEnd = reinterpret_cast<uptr>(recPtrEnd);
	if (!PollLatch::Capacity(hotBase, reinterpret_cast<uptr>(armGetCurrentCodePointer()), hotEnd, bound)) return;
	// Discharge any PREEXISTING pool before the bounded guard, then recheck
	// the real cursor. Do not put guard bytes into the ordinary64KiB slop.
	armAsm->EnsureEmitFor(bound);
	if (!PollLatch::Capacity(hotBase, reinterpret_cast<uptr>(armGetCurrentCodePointer()), hotEnd, bound)) return;
	const u8* expected = armConstantPool->GetBlob(reinterpret_cast<const u8*>(words.data()), bytes);
	if (!expected) return;
	a64::BlockPoolsScope noGuardPools(armAsm);
	const uptr guardBegin = reinterpret_cast<uptr>(armGetCurrentCodePointer());

	a64::Label fallback;
#if PS2_NATIVE_POLL_WRITER_PROFILE == 2
	// Reused compiled blocks carry no authority across CPU-entry/state/disc
	// changes. Only the actual current native entry can hold this byte.
	armMoveAddressToReg(a64::x17, &NativePoll::lease);
	armAsm->Ldrb(a64::w8, a64::MemOperand(a64::x17));
	armAsm->Cbz(a64::w8, &fallback);
#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
	if (NativePoll::EmitMtvuQuietCheck())
		NativePoll::EmitMtvuQuietGate(&fallback);
#endif
#endif
	// Mode is mutable at settings/reset seams. Fail even if code survived an
	// unexpected mode transition; never infer Off from a worker count.
	armMoveAddressToReg(a64::x17, &IopThread::s_mode);
	static_assert(static_cast<u32>(IopThreadMode::Off) == 0);
	armAsm->Ldrb(a64::w8, a64::MemOperand(a64::x17));
	armAsm->Cbnz(a64::w8, &fallback);
#ifdef PCSX2_RECOMPILER_TESTS
	a64::Label park_ok;
	armMoveAddressToReg(a64::x17, &g_eeHarnessActive);
	armAsm->Ldrb(a64::w8, a64::MemOperand(a64::x17));
	armAsm->Cbz(a64::w8, &park_ok);
	armLoadPtr(a64::w8, &g_eeHarnessParkPc);
	armAsm->Sub(a64::w8, a64::w8, imm);
	armAsm->Cmp(a64::w8, bytes);
	armAsm->B(&fallback, a64::lo);
	armAsm->Bind(&park_ok);
#endif
	// Validate even discarded loads (e.g. LW $zero) before any skip. Extra
	// rejected-path reads here are checked MainRAM only, never handlers.
#ifdef IZPS2_SWITCH2_NATIVE_POLL_ADDRESS_CHAIN
	pxAssert(!arm64gprs[a64::x3.GetCode()].inuse);
#endif
#ifdef IZPS2_SWITCH2_NATIVE_POLL_CACHE
	u32 firstReads = (u32{1} << plan.reads.size()) - 1;
	u32 deferredReads = 0;
	bool smallerTraceIsSecond = false;
	if (plan.traces.size() == 2)
	{
		const u32 left = plan.traces[0].readMask;
		const u32 right = plan.traces[1].readMask;
		const u32 shared = left & right;
		// A strict subset lets the shorter trace go first. Each load is emitted
		// at most once, so GuardBytes still bounds the finished guard.
		if (left != right && (shared == left || shared == right))
		{
			firstReads = shared;
			deferredReads = (left | right) & ~shared;
			smallerTraceIsSecond = shared == right;
		}
	}
	const bool reuseMapping = !plan.reads.empty();
	if (reuseMapping)
	{
		pxAssert(!arm64gprs[s_pollVmap.GetCode()].inuse);
		armLoadPtr(s_pollVmap, &vtlb_private::vtlbdata.vmap);
		armAsm->Cbz(s_pollVmap, &fallback);
#if defined(IZPS2_SWITCH2_NATIVE_POLL_PAGES) && defined(IZPS2_SWITCH2_NATIVE_POLL_ADDRESS_CHAIN)
		if (std::has_single_bit(Ps2MemSize::ExposedRam) && Ps2MemSize::ExposedRam >= 4 &&
			Ps2MemSize::ExposedRam <= 0x20000000u)
		{
			pxAssert(!arm64gprs[s_pollMainRam.GetCode()].inuse);
			armMoveAddressToReg(s_pollMainRam, &eeMem->Main[0]);
		}
#endif
	}
	const auto emitReads = [&](u32 mask) {
		[[maybe_unused]] const PollLatch::Read* previous = nullptr;
		for (u32 i = 0; i < plan.reads.size(); ++i)
		{
			if (!(mask & (u32{1} << i)))
				continue;
			pxAssert(!arm64gprs[s_pollValues[i].GetCode()].inuse);
			const auto& read = plan.reads[i];
#ifdef IZPS2_SWITCH2_NATIVE_POLL_PAGES
			bool reusePage = false;
			if (previous)
			{
				const s32 separation = static_cast<s32>(read.immediate) - previous->immediate;
				reusePage = read.base == previous->base &&
					separation > -static_cast<s32>(vtlb_private::VTLB_PAGE_SIZE) &&
					separation < static_cast<s32>(vtlb_private::VTLB_PAGE_SIZE);
			}
			emitPollRead(read, s_pollValues[i], &fallback, reusePage, previous, reuseMapping);
			previous = &read;
#else
			emitPollRead(read, s_pollValues[i], &fallback, false, nullptr, reuseMapping);
#endif
		}
	};
	emitReads(firstReads);
#if defined(PS2_NATIVE_POLL_LATCH_ORACLE) && PS2_NATIVE_POLL_LATCH_ORACLE
	std::fprintf(stderr, "POLL_READ_PLAN first=%u initial=%x deferred=%x\n", imm, firstReads, deferredReads);
#endif
#else
	constexpr bool reuseMapping = false;
	for (const auto& read : plan.reads)
		emitPollRead(read, a64::x0, &fallback);
#endif
	bool sharedSelector = false;
	if (plan.traces.size() == 2 && !plan.traces[0].tests.empty() && !plan.traces[1].tests.empty())
	{
		const auto& left = plan.traces[0].tests.front();
		const auto& right = plan.traces[1].tests.front();
		sharedSelector = left.equal != right.equal &&
			((left.a == right.a && left.b == right.b) || (left.a == right.b && left.b == right.a));
	}
	const auto emitTest = [&](const PollLatch::Test& test, a64::Label* reject) {
#ifdef IZPS2_SWITCH2_NATIVE_POLL_COMPARE
		emitPollTest(plan, test, &fallback, reject);
#else
		emitPollExpression(plan, test.a, 0, &fallback);
		emitPollExpression(plan, test.b, 1, &fallback);
		armAsm->Cmp(a64::x0, a64::x1);
		armAsm->B(reject, test.equal ? a64::ne : a64::eq);
#endif
	};
#if defined(PS2_NATIVE_POLL_LATCH_ORACLE) && PS2_NATIVE_POLL_LATCH_ORACLE
	std::fprintf(stderr, "POLL_BRANCH_PLAN first=%u shared_selector=%u\n", imm, unsigned(sharedSelector));
#endif
	a64::Label stable;
	for (size_t index = 0; index < plan.traces.size(); ++index)
	{
#ifdef IZPS2_SWITCH2_NATIVE_POLL_CACHE
		// Predicate evaluation has clobbered the address registers. A deferred
		// read starts a fresh address chain while cached values remain live.
		if (index == 1 && deferredReads)
			emitReads(deferredReads);
		const PollLatch::Trace& trace = plan.traces[smallerTraceIsSecond ? 1 - index : index];
#else
		const PollLatch::Trace& trace = plan.traces[index];
#endif
		const bool last = index + 1 == plan.traces.size();
		a64::Label next;
		a64::Label* const reject = (last || sharedSelector) ? &fallback : &next;
		// The opposite selector makes the second trace unreachable after a
		// later failure of the first trace. On the other arm it is already true.
		if (sharedSelector && !last)
			emitTest(trace.tests.front(), &next);
		for (size_t test = sharedSelector ? 1 : 0; test < trace.tests.size(); ++test)
			emitTest(trace.tests[test], reject);
#ifdef IZPS2_SWITCH2_NATIVE_POLL_COMPARE
		// Cached reads and register-only expression emission never set
		// NZCV. Keep a mismatch sticky and branch once after the group.
		bool firstComparison = true;
#endif
		for (u32 r = 1; r != 32; ++r)
		{
			if (!(trace.writes & (u32{1} << r))) continue;
#ifdef IZPS2_SWITCH2_NATIVE_POLL_COMPARE
			const auto value = pollCompareOperand(plan, trace.result[r], 0, &fallback);
			const auto* pin = armEEPinForGPR(r);
			if (!pin)
				armLoadEERegPtr(a64::x1, &cpuRegs.GPR.r[r].UD[0]);
			const auto other = pin ? *pin : a64::Register(a64::x1);
			if (firstComparison)
				armAsm->Cmp(value, other);
			else
				armAsm->Ccmp(value, other, a64::NoFlag, a64::eq);
			firstComparison = false;
#else
			emitPollExpression(plan, trace.result[r], 0, &fallback);
			armLoadEERegPtr(a64::x1, &cpuRegs.GPR.r[r].UD[0]);
			armAsm->Cmp(a64::x0, a64::x1);
			armAsm->B(reject, a64::ne);
#endif
		}
#ifdef IZPS2_SWITCH2_NATIVE_POLL_COMPARE
		if (!firstComparison)
			armAsm->B(reject, a64::ne);
#endif
		if (!last)
		{
			armAsm->B(&stable);
			armAsm->Bind(&next);
		}
	}
	armAsm->Bind(&stable);

	// A rejected trace only made checked, side-effect-free MainRAM reads.
	// Validate the frozen instructions for a stable trace before advancing.
	// There is no call or mapping update between these reads.
#ifdef IZPS2_EE_POLL_WATCHED_CODE
	// Data reads and fixed-point predicates still run on every attempt.
	// A matching epoch also certifies both instruction pages.
	a64::Label snapshotMatches;
	if (!armAddressRecorder)
	{
		const u32 first = imm >> vtlb_private::VTLB_PAGE_BITS;
		const u32 last = (imm + bytes - 1) >> vtlb_private::VTLB_PAGE_BITS;
		if (const auto* epoch = eeWatchPollCode(live, bytes, first, last))
		{
			armLoadPtr(a64::x8, epoch);
			armAsm->Cmp(a64::x8, *epoch);
			armAsm->B(&snapshotMatches, a64::eq);
		}
	}
#endif
	const auto codeMap = reuseMapping ? s_pollVmap : a64::x17;
	if (!reuseMapping)
	{
		armLoadPtr(codeMap, &vtlb_private::vtlbdata.vmap);
		armAsm->Cbz(codeMap, &fallback);
	}
	uptr expectedMap = 0;
	for (u32 off = 0; off < bytes;)
	{
		const u32 va = imm + off;
		const u32 page = va >> vtlb_private::VTLB_PAGE_BITS;
		if (page < 4096)
			armAsm->Ldr(a64::x8, a64::MemOperand(codeMap, page * 8));
		else
		{
			armAsm->Mov(a64::x8, page);
			armAsm->Ldr(a64::x8, a64::MemOperand(codeMap, a64::x8, a64::LSL, 3));
		}
		const uptr raw = vtlb_private::vtlbdata.vmap[page].raw();
		if (off == 0 || raw != expectedMap)
			armAsm->Mov(a64::x9, raw);
		expectedMap = raw;
		if (off == 0)
			armAsm->Cmp(a64::x8, a64::x9);
		else
			armAsm->Ccmp(a64::x8, a64::x9, a64::NoFlag, a64::eq);
		off += std::min<u32>(bytes - off, 4096 - (va & 4095));
	}
	armAsm->B(&fallback, a64::ne);
	emitPollInstructionSnapshot(live, expected, bytes, &fallback);
#ifdef IZPS2_EE_POLL_WATCHED_CODE
	armAsm->Bind(&snapshotMatches);
#endif

	// The original block has already charged its cycles and tested the
	// deadline. Advance to that same deadline and run the normal events.
#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
	armMoveAddressToReg(a64::x17, &NativePoll::skipStats);
	armAsm->Ldp(a64::x8, a64::x9, a64::MemOperand(a64::x17));
	armAsm->Add(a64::x8, a64::x8, 1);
	armAsm->Sub(a64::x9, a64::x9, RECCYCLE);
	armAsm->Stp(a64::x8, a64::x9, a64::MemOperand(a64::x17));
#endif
	armAsm->Mov(RECCYCLE, 0);
	armEmitJmp(DispatcherEvent);
	armAsm->Bind(&fallback);
#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
	++NativePoll::guards;
#endif
	const uptr guardEnd = reinterpret_cast<uptr>(armGetCurrentCodePointer());
	pxAssertRel(guardEnd >= guardBegin && guardEnd - guardBegin <= bound && guardEnd <= hotEnd,
		"poll-latch guard must fit its conservative HOT bound without consuming baseline tail slop");
#if defined(PS2_NATIVE_POLL_LATCH_ORACLE) && PS2_NATIVE_POLL_LATCH_ORACLE
	// Compile-time fixture witness only; not a generated guest callback/log.
	std::fprintf(stderr, "POLL_GUARD_BOUND first=%u begin=%llu end=%llu hotEnd=%llu bound=%u actual=%llu\n", imm,
		static_cast<unsigned long long>(guardBegin), static_cast<unsigned long long>(guardEnd),
		static_cast<unsigned long long>(hotEnd), bound, static_cast<unsigned long long>(guardEnd - guardBegin));
#endif
#else
	(void)imm;
#endif
}
} // namespace
#endif
