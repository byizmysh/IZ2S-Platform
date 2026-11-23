// SPDX-FileCopyrightText: 2026 yaps2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// ARM64 EE (R5900) Dynamic Recompiler Core
// Dispatcher, block management, all instructions as interpreter fallbacks.

#include <algorithm>
#include <atomic>
#include <bit>
#include <cfloat>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "arm64/iR5900-arm64.h"
#include "EeWriteShadow.h"
#include "arm64/iR5900Analysis.h"
#include "arm64/AsmHelpers.h"
#if defined(IZPS2_SWITCH2_EE_WARM_ALIGN) && !defined(PS2_HOST_CODE_BRIDGE)
u8* armEndOrderedEeWarmBlock(bool publish);
#endif
#include "EeSoftmemColdContext.h"
#include "EeSoftmemPages.h"
#ifdef IZPS2_SWITCH2_EE_WARM
// RUN25's VM runner and this producer must share the same profile limits.
// The inherited -iquote directory has another native profile format limit.
#include <native_ee_warm_profile.hpp>
#elif defined(PS2_HOST_CODE_BRIDGE)
#include "native_ee_warm_profile.hpp"
#endif
#ifdef IZPS2_SWITCH2_EE_WARM_RANGES
bool iz_ps2_begin_ee_warm_ranges();
void iz_ps2_end_ee_warm_ranges();
#endif
#ifdef PS2_HOST_CODE_BRIDGE
#include "arm64/ColdIslandPagePatchBatch.h"
#include "ee_publication_transaction.h"
extern "C" __attribute__((visibility("hidden"))) int ps2_mtvu_code_host_caller_idle();
extern "C" __attribute__((visibility("hidden"))) int ps2_mtvu_caller_is_ee();
__attribute__((visibility("hidden"))) bool ps2ArmCodeWriteWindowActive();
#if PS2_EE_SOFTMEM_COLD_OOL
#include "ps2_mtvu_lifecycle_private.h"
#endif
#endif
#line 21
#include "common/HostSys.h"
#ifdef IZPS2_RUN24_EE_TIMING
#include "common/Switch2/CodeCache.h"
#include "native-system.h"

static thread_local EeCompileAudit s_eeCompileAudit{};

EeCompileAudit recGetEeCompileAudit()
{
	return s_eeCompileAudit;
}
#endif
#ifdef __APPLE__
#include "common/Darwin/DarwinMisc.h"
#endif
#include "Host.h"
#include "R3000A.h"
#include "R5900.h"
#include "IopThread/IopThreadMode.h"
#include "Arm64EeIndexedBaseBlocks.h"
#include "EeIncomingPageBatchPrivate.inc"
#include "R5900OpcodeTables.h"
#include "Common.h"
#include "VMManager.h"
#include "Patch.h"
#include "Config.h"
#include "vtlb.h"
#include "Dmac.h"
#include "EECycleRate.h"
#include "GS.h"
#ifdef PCSX2_RECOMPILER_TESTS
#include "ee_divtrace.h" // diagnostic divergence-trace hooks (test builds only)
#endif

#include "common/Assertions.h"
#include "common/AlignedMalloc.h"
#include "common/Console.h"
#include "common/FastJmp.h"
#include "common/HeapArray.h"
#include "common/Perf.h"

#include "DebugTools/Breakpoints.h"

#include "EeGprLoadPageGroup.h"
#include "native_ee_compile_timing.h"
#include "native_ee_smc_snapshots.h"
#include "native_ee_smc_storage.h"

extern "C" {
[[gnu::used, gnu::visibility("hidden")]] NativeEeCompileTiming ps2_native_ee_compile_timing;
}

namespace a64 = vixl::aarch64;

// R104 source-only opt-in: the block-entry SMC compare may use NEON only
// before guest allocations are loaded. Resident-loop backedges stay scalar.
#ifndef PS2_EE_MANUAL_SMC_SIMD_ENTRY
#define PS2_EE_MANUAL_SMC_SIMD_ENTRY 0
#endif
#if PS2_EE_MANUAL_SMC_SIMD_ENTRY != 0 && PS2_EE_MANUAL_SMC_SIMD_ENTRY != 1
#error PS2_EE_MANUAL_SMC_SIMD_ENTRY must be 0 or 1
#endif

#ifndef PS2_EE_RESTORE_CURRENT_ENTRY
#define PS2_EE_RESTORE_CURRENT_ENTRY 0
#endif
#if PS2_EE_RESTORE_CURRENT_ENTRY != 0 && PS2_EE_RESTORE_CURRENT_ENTRY != 1
#error PS2_EE_RESTORE_CURRENT_ENTRY must be zero or one
#endif

// Keep the test ABI for local differential execution, but production need not
// allocate/update unconsumed block-introspection containers. No emitter state.
#ifndef PS2_EE_TEST_BLOCK_METADATA
#define PS2_EE_TEST_BLOCK_METADATA 1
#endif
#if PS2_EE_TEST_BLOCK_METADATA != 0 && PS2_EE_TEST_BLOCK_METADATA != 1
#error PS2_EE_TEST_BLOCK_METADATA must be zero or one
#endif

// =====================================================================================================
//  Global State
// =====================================================================================================

u32 maxrecmem = 0;
// Longest guest extent, in bytes, of any block compiled since the last reset.
// Bounds how far back the stale-overlap walk in recRecompile must scan.
static u32 s_maxBlockBytes = 0;
// Conservative upper ends for completed blocks starting in each 256-byte RAM
// bucket. Deletion leaves stale HIGH bounds (extra scans only); only a full
// block-cache reset clears the index. No dispatch or cycle policy changes.
#ifndef PS2_EE_OVERLAP_BUCKET_INDEX
#define PS2_EE_OVERLAP_BUCKET_INDEX 1
#endif
static constexpr u32 kEeOverlapBucketShift = 8;
static u32 s_overlapUpperEnd[Ps2MemSize::TotalRam >> kEeOverlapBucketShift] = {};
static u64 s_overlapMetadataVisits = 0;
static u64 s_overlapBucketsSkipped = 0;
static u64 s_emptyClearZero = 0;
static u64 s_emptyClearBelowFirst = 0;
#ifndef PS2_EE_CLEAR_BUCKET_INDEX
#define PS2_EE_CLEAR_BUCKET_INDEX 1
#endif
// One bit for each physical RAM page reached by a completed block in
// this generation. Clears can leave conservative set bits behind.
static constexpr u32 kEeCodePageShift = 12;
static constexpr u32 kEeCodePageBits = 64;
static u64 s_eeCodePages[Ps2MemSize::TotalRam >> (kEeCodePageShift + 6)] = {};

static bool eeClearMayHitCode(u32 addr, u32 end)
{
	if (end <= addr || end > Ps2MemSize::TotalRam)
		return true;
	const u32 first = addr >> kEeCodePageShift;
	const u32 last = (end - 1) >> kEeCodePageShift;
	const u32 first_word = first / kEeCodePageBits;
	const u32 last_word = last / kEeCodePageBits;
	const u64 first_mask = ~u64{0} << (first % kEeCodePageBits);
	const u64 last_mask = ~u64{0} >> (kEeCodePageBits - 1 - last % kEeCodePageBits);
	if (first_word == last_word)
		return (s_eeCodePages[first_word] & first_mask & last_mask) != 0;
	if ((s_eeCodePages[first_word] & first_mask) || (s_eeCodePages[last_word] & last_mask))
		return true;
	for (u32 word = first_word + 1; word < last_word; ++word)
	{
		if (s_eeCodePages[word])
			return true;
	}
	return false;
}

static u64 s_clearMetadataVisits = 0;
static u64 s_clearBucketsSkipped = 0;
#ifdef PCSX2_RECOMPILER_TESTS
__attribute__((visibility("hidden"))) void recEeClearScanStats(u64* visited, u64* skipped)
{
    *visited = s_clearMetadataVisits;
    *skipped = s_clearBucketsSkipped;
}
#endif
#ifdef PCSX2_RECOMPILER_TESTS
void recEeOverlapScanStats(u64* visited, u64* skipped)
{
    *visited = s_overlapMetadataVisits;
    *skipped = s_overlapBucketsSkipped;
}
#endif

u32 pc;
int g_branch;
u32 target;
u32 s_nBlockCycles;
bool s_nBlockInterlocked;

bool g_recompilingDelaySlot = false;
bool g_cpuFlushedPC = false;
bool g_cpuFlushedCode = false;
// Mirrors x86 iR5900.cpp:64. Set only by the (currently #if 0'd) FLUSH_CAUSE
// path in iFlushCall and reset after every recompiled instruction, so in
// practice it is always false and the two BD-bit clears it guards never emit at
// runtime — exactly matching x86. Kept so the delay-slot handling is a
// line-for-line mirror of x86 and re-syncs cleanly if upstream re-enables it.
bool g_maySignalException = false;

// LDL/LDR pair fusion state (see iR5900-arm64.h). g_eeUnalignedFused is set by the
// leading half of a fused unaligned 64-bit load and consumed by the trailing half;
// g_eeUnalignedFuseCount is a diagnostic/test tally.
bool g_eeUnalignedFused = false;
u32 g_eeUnalignedFuseCount = 0;

// Constant propagation — defined here, declared extern in iR5900-arm64.h

#ifdef PS2_EE_DISPATCH_ALIGNED_LUT
// Keep the low address bits zero even when the code arena is out of ADRP reach.
alignas(_64kb)
#endif
static uptr recLUT[0x10000];
static u32 hwLUT[0x10000];

static __fi u32 HWADDR(u32 mem) { return hwLUT[mem >> 16] + mem; }

static BASEBLOCK* recRAM = nullptr;
static BASEBLOCK* recROM = nullptr;
static BASEBLOCK* recROM1 = nullptr;
static BASEBLOCK* recROM2 = nullptr;
#if PS2_EE_INCOMING_PAGE_BATCH && defined(PS2_HOST_CODE_BRIDGE)
static Ps2EeIncomingBlocks recBlocks;
#else
static Arm64EeIndexedBaseBlocks recBlocks;
#endif

// The view belongs to one already active assembler. Offset stays live
// because VIXL may emit pools inside an emission check scope.
struct EeEmitterView
{
	a64::MacroAssembler* masm;
	u8* rx_base;

	u8* Cursor() const { return rx_base + masm->GetCursorOffset(); }
};

static void emitEeLinkedBranch(u32 address, bool call = false)
{
#if PS2_EE_INCOMING_PAGE_BATCH && defined(PS2_HOST_CODE_BRIDGE)
    recBlocks.EmitLink(HWADDR(address), call);
#else
    a64::SingleEmissionCheckScope guard(armAsm);
    u8* site = armGetCurrentCodePointer();
    if (call)
        armAsm->bl(int64_t{0});
    else
        armAsm->b(int64_t{0});
#ifdef IZPS2_SWITCH2_EE_INCOMING_BATCH
    // A direct B/BL skips BTI. The dispatcher keeps the canonical entry,
    // and stale blocks redirect at the same first instruction after BTI.
    recBlocks.Link(HWADDR(address), site, call, sizeof(u32));
#else
    recBlocks.Link(HWADDR(address), site, call);
#endif
#endif
}

static u8* recPtr = nullptr;
static u8* recPtrEnd = nullptr;

// SL-10: cold side-exit arena. Superblock taken arms outline into a separate
// region carved from the top of the EE cache (below the constant pool) instead
// of inline after each block's tail — inline placement puts cold bytes between
// consecutive hot blocks in the compile-order stream, which the S2 device A/B
// measured as +5.4% EErec icache-miss density. The arena stays inside the EE
// rec region, so fastmem fault range checks and perf bucketing are unaffected.
// Bump-allocated; recycled only by the full cache reset, like block memory.
static u8* s_coldBase = nullptr;
static u8* s_coldPtr = nullptr;
static u8* s_coldPtrEnd = nullptr;

#ifdef PS2_HOST_CODE_BRIDGE
static bool s_eeWarmPublicationOpen = false;

static u8* recNextColdPageIfShortTail(u8* cursor, const u8* limit)
{
	if (s_eeWarmPublicationOpen)
		return cursor;
	const size_t tail = (0 - reinterpret_cast<uptr>(cursor)) & (_4kb - 1);
	// Prefer fresh pages only in the early part of the cold arena.
	const size_t tail_limit = cursor < s_coldBase + 384 * _1kb ? _4kb - 1 : _4kb / 2;
	if (tail >= 16 && tail <= tail_limit && cursor < limit &&
		cursor < s_coldBase + 512 * _1kb && tail < static_cast<size_t>(limit - cursor))
		return cursor + tail;
	return cursor;
}
#endif

static EEINST* s_pInstCache = nullptr;
static u32 s_nInstCacheSize = 0;

static BASEBLOCK* s_pCurBlock = nullptr;
static BASEBLOCKEX* s_pCurBlockEx = nullptr;

static u32 s_nEndBlock = 0;
static u32 s_branchTo;
// SL-1: terminal branch class permits a resident back-edge (conditional
// branches and plain J — not JAL/JR, which own call-ret/indirect tails).
static bool s_branchLoopable;
static bool s_nBlockFF;

// Exposed for the LDL/LDR fusion in recVTLB-arm64.cpp (same-block partner check).
u32 recCurrentBlockEndPC() { return s_nEndBlock; }

#ifdef PS2_EE_DISPATCH_ALIGNED_RAM
static DynamicHeapArray<BASEBLOCK, _64kb> recLutReserve_RAM;
#else
static DynamicHeapArray<BASEBLOCK, 4096> recLutReserve_RAM;
#endif
static DynamicHeapArray<BASEBLOCK, 4096> recLutUnmapped;
static DynamicHeapArray<u8> recRAMCopy;

#ifdef PS2_EE_SNAPSHOT_RESET_WITNESS
extern "C" {
u64 ps2_ee_snapshot_reset_bytes = 0;

bool ps2_ee_snapshot_is_zero()
{
	return std::all_of(recRAMCopy.data(), recRAMCopy.data() + recRAMCopy.size(),
		[](u8 value) { return value == 0; });
}
}
#define EE_SNAPSHOT_RESET_BYTES(count) (ps2_ee_snapshot_reset_bytes += (count))
#else
#define EE_SNAPSHOT_RESET_BYTES(count) ((void)0)
#endif

static size_t recLutEntries = 0;
static constexpr size_t kEeLutSlotsPerPage = 4096 / sizeof(BASEBLOCK);
static std::vector<u64> s_eeLutDirtyPages;
static const void* s_eeLutDefault = nullptr;

#ifdef PS2_EE_RESET_LUT_WITNESS
extern "C" {
u64 ps2_ee_reset_lut_slots = 0;
}
#define EE_LUT_RESET_SLOTS(count) (ps2_ee_reset_lut_slots += (count))
#else
#define EE_LUT_RESET_SLOTS(count) ((void)0)
#endif


static ArmConstantPool s_eeConstantPool;
static NativeEeSmcSnapshots s_eeSmcSnapshots;
static NativeEeSmcStorage s_eeSmcStorage;
extern "C" {
[[gnu::used, gnu::visibility("hidden")]] u64 ps2_ee_smc_snapshots[4];
[[gnu::used, gnu::visibility("hidden")]] u64 ps2_ee_smc_data[3];
}

static const u8* recImmutableSmcSnapshot(const u8* live, size_t bytes)
{
	if (!armConstantPool)
		return nullptr;
	// Keep the cache scoped to the EE pool and the current generation.
	if (armConstantPool != &s_eeConstantPool || bytes < sizeof(u32))
		return armConstantPool->GetBlob(live, bytes);
	++ps2_ee_smc_snapshots[0];
	auto& entry = s_eeSmcSnapshots.Slot(live, bytes);
	if (NativeEeSmcSnapshots::Matches(entry, live, bytes))
	{
		++ps2_ee_smc_snapshots[1];
		ps2_ee_smc_snapshots[2] += bytes;
		return entry.bytes;
	}
	const u8* snapshot = s_eeSmcStorage.Append(live, bytes);
	if (snapshot)
	{
		++ps2_ee_smc_data[0];
		ps2_ee_smc_data[1] += bytes;
	}
	else
	{
		++ps2_ee_smc_data[2];
		snapshot = armConstantPool->GetBlob(live, bytes);
	}
	if (snapshot)
	{
		entry = {snapshot, bytes};
		ps2_ee_smc_snapshots[3] += bytes;
		return snapshot;
	}
	return nullptr;
}

// Execution state
static fastjmp_buf m_SetJmp_StateCheck;
static bool eeCpuExecuting = false;
static bool eeRecNeedsReset = false;
// Set by recSafeExitExecution() — which the Android pause/stop JNI calls from the UI thread via
// Cpu->ExitExecution() — and consumed on the CPU thread in recEventTest(). Atomic because a plain
// bool here is a cross-thread data race: under ARM64's weak memory model the request can be
// reordered or hoisted, leaving the EE spinning through a pause the user already asked for.
static std::atomic<bool> eeRecExitRequested{false};

#ifdef PCSX2_RECOMPILER_TESTS
// Harness-entry state. Set by recEeExecuteBlock before entering the JIT,
// observed by recEventTest at every iBranchTest event-due fall-out.
// Production execution (recExecute) leaves g_eeHarnessActive false; the
// predicate short-circuits on it. Compiled out entirely in release builds
// (ENABLE_RECOMPILER_TEST_HOOKS=OFF) so the live VM path carries no
// test-only symbols or branches.
static bool g_eeHarnessActive = false;
static u32 g_eeHarnessParkPc = 0;
static s32 g_eeHarnessCycleBudget = 0;
static u64 g_eeHarnessCycleStart = 0;
static constexpr s32 kExecuteBlockSafetyCap = 1 << 20;
// Wait-loop detection verdict for the most recently analysed block —
// non-static: read by the AX-07 detector tests via extern.
bool g_eeRecLastBlockFF = false;
#endif

// Self-modifying code detection. Sized for the 128 MB map (Ps2MemSize::TotalRam), as on x86; a 32 MB
// machine only ever touches the first quarter.
static u16 manual_page[Ps2MemSize::TotalRam / 4096] = {};
static u8 manual_counter[Ps2MemSize::TotalRam / 4096] = {};

// Forward declarations
static void recRecompile(const u32 startpc);
static void recResetRaw();
static void recExitExecution();
static void recSafeExitExecution();
#ifdef PCSX2_RECOMPILER_TESTS
static bool harnessShouldExit();
#endif
static void recError(u32 error);
static void dyna_block_discard(u32 start, u32 sz);
static void dyna_page_reset(u32 start, u32 sz);
static void iopClearRecLUT(BASEBLOCK* base, int count);

// recBackpropBSC declared in arm64/iR5900Analysis.h

#define GETBLOCK(x) PC_GETBLOCK_(x, recLUT)

// =====================================================================================================
//  Dynamically Compiled Dispatchers - R5900 ARM64
// =====================================================================================================

static const void* DispatcherEvent = nullptr;
static const void* DispatcherReg = nullptr;
#if PS2_EE_DISPATCH_KNOWN_PC
static const void* DispatcherRegWithPc = nullptr;
#endif
// SL-11: shared tail for cold side exits (pc in RWSCRATCH, cycles in RWARG2):
// Str pc / Adds cycles / event-exit / Ret. BL'd from each exit so the per-exit
// tail shrinks to Mov+Mov+BL+linked-B.
static const void* SuperblockExitStub = nullptr;
#if PS2_EE_INCOMING_PAGE_BATCH && defined(PS2_HOST_CODE_BRIDGE)
static const void* SuperblockDispatchExitStub = nullptr;
static const void* SuperblockRamExitStub = nullptr;
#endif
static const void* JITCompile = nullptr;
static const void* EnterRecompiledCode = nullptr;
static const void* DispatchBlockDiscard = nullptr;
static const void* DispatchPageReset = nullptr;
static const void* UnmappedRecLUTPage = nullptr;

// B.cond and CBZ reach ±1MB. The dispatchers are generated at the base of a
// code cache tens of megabytes deep, so past its first megabyte the event
// check at every block exit and the delay-slot exception bracket both fell
// back to an inverted condition branching over a B: an instruction more, and
// a taken branch on the path the condition does not take. A veneer pair in
// front of a block restores the short form for the megabyte that follows it,
// and starts out as the dispatchers themselves. Both emit sites re-measure
// the distance, so a block wider than the reach left over, and the cold arena
// above the hot one, still get the long form.
static const u8* s_condVeneerEvent = nullptr;
static const u8* s_condVeneerReg = nullptr;

#ifdef PCSX2_RECOMPILER_TESTS
// A test cache never grows past the first megabyte, so a test reaches the
// veneers by forcing them: every block then emits a pair and every event
// check and bracket hops through one whatever the distance. A forced run
// that counts no hops tested the direct form twice.
static bool s_condVeneerForced = false;
static u32 s_condVeneerHops = 0;
void recEeForceCondVeneers(bool on)
{
	s_condVeneerForced = on;
}
u32 recEeCondVeneerHops()
{
	return s_condVeneerHops;
}
// The dispatchers, so a test can follow a block's conditional exits into the
// veneer and read what it branches to. Nothing the harness observes separates
// the two: DispatcherEvent falls through into DispatcherReg, and the parking
// lot leaves through an unconditional jump to DispatcherEvent whatever a
// block's own event check did.
void recEeDispatcherAddrs(const void** dispatcher_event, const void** dispatcher_reg)
{
	*dispatcher_event = DispatcherEvent;
	*dispatcher_reg = DispatcherReg;
}
#define COUNT_VENEER_HOP() (void)(s_condVeneerHops++)
#else
static constexpr bool s_condVeneerForced = false;
#define COUNT_VENEER_HOP() ((void)0)
#endif

#if FPU_GUARD_MASK_STUB
// Shared FPU add/sub guard-bit masking stub. Emitted once per dispatcher
// generation, re-set on cache reset. See iFPU-arm64.cpp.
const void* g_fpuGuardMaskStub = nullptr;
#endif

#ifdef PCSX2_RECOMPILER_TESTS
// Runs inside recEventTest, after the shared event test, as host code
// reached from there would: interrupt delivery, or a savestate save from the
// message pump at vsync. The harness runs with no VM, where the shared event
// test returns early, so this is how a test puts work there.
void (*g_eeHarnessEventTestHook)() = nullptr;
#endif

#ifdef PS2_EE_SOFTMEM_TAGGED_PAGES
bool g_eeSoftmemUseCanonicalMap = false;
#endif

#ifdef PS2_EE_DEFER_HOT_PUBLICATION
// RUN25 R806 reproduced a cold-boot failure with deferred hot-block
// publication. With the immediate path selected, the same runtime booted GTA.
static bool s_deferHotPublication = false;
#endif

#ifdef IZPS2_SWITCH2_STARTUP_TRACE
// Configured by the VM owner. Recompilation and scheduler callbacks run on
// that same thread. Limit the SD logging to the beginning of the session.
static unsigned s_ee_startup_compiles;
static unsigned s_ee_startup_events;
static u32 s_ee_trace_window_start;
static u32 s_ee_trace_window_end;
static unsigned s_ee_trace_window_words;
static bool s_ee_trace_reference_lwc1;
static unsigned s_ee_trace_reference_logs;

static void eeTraceReferenceLwc1()
{
    const u32 code = cpuRegs.code;
    const u32 ea = cpuRegs.GPR.r[(code >> 21) & 31].UL[0] + static_cast<s16>(code);
    const bool log = s_ee_trace_reference_logs != 0;
    if (log) {
        --s_ee_trace_reference_logs;
        const auto pte = vtlb_private::vtlbdata.vmap[ea >> vtlb_private::VTLB_PAGE_BITS];
        const bool handler = pte.isHandler(ea);
        const uptr ram = reinterpret_cast<uptr>(eeMem->Main);
        const uptr offset = handler ? Ps2MemSize::ExposedRam : pte.assumePtr(ea) - ram;
        Console.WriteLn("IZPS2_EE_REFERENCE phase=read_begin pc=%08x va=%08x handler=%d main_ram=%d",
            s_ee_trace_window_start, ea, handler ? 1 : 0, offset <= Ps2MemSize::ExposedRam - sizeof(u32) ? 1 : 0);
    }
    R5900::GetInstruction(code).interpret();
    if (log)
        Console.WriteLn("IZPS2_EE_REFERENCE phase=read_complete pc=%08x", s_ee_trace_window_start);
}

void izConfigureEeStartupTrace(bool enabled)
{
#ifdef PS2_EE_SOFTMEM_TAGGED_PAGES
    // The VM owner chooses the map before CPU initialization. All EE memory
    // emitters keep the same choice for the lifetime of this VM's code cache.
    g_eeSoftmemUseCanonicalMap = false;
    if (FILE* file = std::fopen("sdmc:/iz2s/modules/izps2/ee-canonical-vmap.flag", "r")) {
        std::fclose(file);
        g_eeSoftmemUseCanonicalMap = true;
        Console.WriteLn("IZPS2_EE_SOFTMEM map=canonical");
    }
#endif
    unsigned compiles = 2048;
    if (enabled) {
        if (FILE* file = std::fopen("sdmc:/iz2s/modules/izps2/ee-startup-trace.flag", "r")) {
            unsigned requested = 0;
            if (std::fscanf(file, "%u", &requested) == 1)
                compiles = std::clamp(requested, 128u, 8192u);
            std::fclose(file);
        }
    }
    s_ee_trace_window_words = 0;
    if (enabled) {
        if (FILE* file = std::fopen("sdmc:/iz2s/modules/izps2/ee-compile-window.flag", "r")) {
            unsigned first = 0, end = 0, words = 0;
            if (std::fscanf(file, "%x %x %u", &first, &end, &words) == 3 &&
                first < end && (first & 3) == 0 && (end & 3) == 0 &&
                end <= Ps2MemSize::ExposedRam && end - first <= _4kb && words && words <= 32) {
                s_ee_trace_window_start = first;
                s_ee_trace_window_end = end;
                s_ee_trace_window_words = words;
                Console.WriteLn("IZPS2_EE_TRACE_WINDOW first=%08x end=%08x words=%u", first, end, words);
            }
            std::fclose(file);
        }
    }
    s_ee_trace_reference_lwc1 = false;
    s_ee_trace_reference_logs = 0;
    if (enabled && s_ee_trace_window_words == 1) {
        if (FILE* file = std::fopen("sdmc:/iz2s/modules/izps2/ee-lwc1-reference.flag", "r")) {
            std::fclose(file);
            s_ee_trace_reference_lwc1 = true;
            s_ee_trace_reference_logs = 2;
        }
    }
    s_ee_startup_compiles = enabled ? compiles : 0;
    s_ee_startup_events = enabled ? 64 : 0;
}
#endif

// One instance per IOP thread mode. The dispatcher stub calls the instance of the mode in effect when
// it was generated (_DynGen_DispatcherEvent), so the Off instance carries no test of the mode.
template <IopThreadMode M>
static void recEventTest()
{
#ifdef IZPS2_SWITCH2_STARTUP_TRACE
	const unsigned trace = s_ee_startup_events;
	if (trace)
	{
		--s_ee_startup_events;
		Console.WriteLn("IZPS2_EE_STARTUP phase=event_begin remaining=%u ee_pc=%08x iop_pc=%08x ee_cycle=%llu",
			trace - 1, cpuRegs.pc, psxRegs.pc, static_cast<unsigned long long>(cpuRegs.cycle));
	}
#endif
	_cpuEventTest_Shared<M>();
#ifdef IZPS2_SWITCH2_STARTUP_TRACE
	if (trace)
		Console.WriteLn("IZPS2_EE_STARTUP phase=event_complete remaining=%u ee_pc=%08x iop_pc=%08x ee_cycle=%llu",
			trace - 1, cpuRegs.pc, psxRegs.pc, static_cast<unsigned long long>(cpuRegs.cycle));
#endif

	// A relaxed load, and the acquire as a fence on the taken path. An acquire load (LDAR) may not
	// complete before an earlier release store (STLR) has, and the Threaded event test has just made
	// one, of the IOP's watermark, to a line the IOP thread is reading: an LDAR here would wait for
	// that line on every event test. Same guarantee: a relaxed load that reads the release store of
	// `true` in recSafeExitExecution, followed by an acquire fence, synchronizes with that store, so
	// what the requesting thread wrote before it is visible to the exit below. Reading `false`
	// gives nothing to depend on: `false` is stored only by this thread.
	if (eeRecExitRequested.load(std::memory_order_relaxed))
	{
		std::atomic_thread_fence(std::memory_order_acquire);
		eeRecExitRequested.store(false, std::memory_order_relaxed);
		recExitExecution();
	}

#ifdef PCSX2_RECOMPILER_TESTS
	if (g_eeHarnessActive)
	{
		if (g_eeHarnessEventTestHook)
			g_eeHarnessEventTestHook();
		if (harnessShouldExit())
			recExitExecution();
	}
#endif

	if (eeRecNeedsReset)
	{
		eeRecNeedsReset = false;
		recResetRaw();
	}
}

static const void* RecEventTestFor(IopThreadMode mode)
{
	switch (mode)
	{
		case IopThreadMode::Lockstep:
			return reinterpret_cast<const void*>(&recEventTest<IopThreadMode::Lockstep>);
		case IopThreadMode::Threaded:
			return reinterpret_cast<const void*>(&recEventTest<IopThreadMode::Threaded>);
		case IopThreadMode::Off:
		default:
			return reinterpret_cast<const void*>(&recEventTest<IopThreadMode::Off>);
	}
}

#ifdef PCSX2_RECOMPILER_TESTS
// Harness-exit predicate. Returns true when running under recEeExecuteBlock
// AND either the parking PC has been reached or the cycle budget has been
// exhausted. Test-only — release builds drop the call site entirely.
static bool harnessShouldExit()
{
	if (!g_eeHarnessActive)
		return false;
	if (cpuRegs.pc == g_eeHarnessParkPc)
		return true;
	const u64 elapsed = cpuRegs.cycle - g_eeHarnessCycleStart;
	return elapsed >= static_cast<u64>(g_eeHarnessCycleBudget);
}
#endif

static bool emitEeColdRamTail(u32 address, bool call = false)
{
#if PS2_EE_INCOMING_PAGE_BATCH && defined(PS2_HOST_CODE_BRIDGE)
	// This exit already wrote the guest PC. A cold RAM destination can use
	// its authoritative dispatch slot directly, leaving the caller immutable.
	// Alias invalidation and recompilation update the same slot as DispatcherReg.
	const u32 offset = address & 0x1fffffffu;
	const u32 alias = address & 0xe0000000u;
	if ((alias == 0 || alias == 0x80000000u || alias == 0xa0000000u) &&
		!(address & 3) && offset < Ps2MemSize::ExposedRam && GETBLOCK(address) == &recRAM[offset / 4])
	{
		const u32 target_pc = HWADDR(address);
		const BASEBLOCKEX* target = recBlocks.Get(target_pc);
		if (!target || target->startpc != target_pc)
		{
			armMoveAddressToReg(RSCRATCHADDR, &recRAM[offset / 4]);
			armAsm->Ldr(RSCRATCHADDR, a64::MemOperand(RSCRATCHADDR));
			if (call)
			{
				// BLR must be immediately before the caller binds its return landing.
				a64::SingleEmissionCheckScope guard(armAsm);
				armAsm->blr(RSCRATCHADDR);
			}
			else
				armAsm->Br(RSCRATCHADDR);
			return true;
		}
	}
#endif
	return false;
}

#ifdef PS2_EE_DISPATCH_ALIGNED_LUT
static void emitEeDispatchTableAddress(const void* table)
{
	// A page-aligned LUT needs only the page instruction when it is in reach.
	// For the far case VIXL's MOV can omit the zero low half-word.
	armAsm->EnsureEmitPoolsFor(a64::kInstructionSize);
	const uptr pointer = reinterpret_cast<uptr>(table);
	const uptr code_page = reinterpret_cast<uptr>(armGetCurrentCodePointer()) & ~uptr{0xfff};
	const auto displacement = (static_cast<s64>(pointer) - static_cast<s64>(code_page)) >> 12;
	if (!(pointer & 0xfff) && vixl::IsInt21(displacement))
	{
		a64::SingleEmissionCheckScope guard(armAsm);
		armAsm->adrp(RSCRATCHADDR, displacement);
	}
	else
	{
		armAsm->Mov(RSCRATCHADDR, pointer);
	}
}
#endif

#ifdef PS2_EE_DISPATCH_AB
// Set on the VM owner before CPU initialization. Only the selected code is
// emitted, so the generated dispatcher never reads this switch.
static bool s_directRamDispatch = false;

void recSelectDirectRamDispatch(bool enabled)
{
	s_directRamDispatch = enabled;
}
#endif

// ARM64 EE dispatcher — same two-level LUT as IOP but using cpuRegs.pc
static const void* _DynGen_DispatcherReg()
{
	u8* retval = armGetCurrentCodePointer();
	armAsm->bti(a64::EmitBTI_jc);

	armAsm->Ldr(a64::w0, armCpuRegMem(&cpuRegs.pc));

#if PS2_EE_DISPATCH_KNOWN_PC
	// Direct B/BL from a register tail can enter here with the saved PC in w0.
	DispatcherRegWithPc = armGetCurrentCodePointer();
#endif

#ifdef PS2_EE_DISPATCH_DIRECT_RAM
#ifdef PS2_EE_DISPATCH_AB
	if (s_directRamDispatch)
#endif
	{
		// recBuildAddressMap maps each low RAM page to the corresponding recRAM
		// page. Use the same entries so recompile and invalidation stay visible.
		static_assert(std::has_single_bit(Ps2MemSize::MainRam));
		a64::Label general;
		armAsm->Lsr(a64::w1, a64::w0, std::countr_zero(Ps2MemSize::MainRam));
		armAsm->Cbnz(a64::w1, &general);
		const auto materialize_start = armAsm->GetCursorOffset();
#ifdef PS2_EE_DISPATCH_ALIGNED_RAM
		emitEeDispatchTableAddress(recRAM);
#else
		armMoveAddressToReg(RSCRATCHADDR, recRAM);
#endif
		const auto materialize_bytes = armAsm->GetCursorOffset() - materialize_start;
		armAsm->Ubfiz(a64::x1, a64::x0, 1, 32);
		armAsm->Ldr(RSCRATCHADDR, a64::MemOperand(RSCRATCHADDR, a64::x1));
		armAsm->Br(RSCRATCHADDR);
		// The RAM table allocation can move on reset. Keep later dispatcher
		// entries, including live JITCompile return points, at stable offsets.
		constexpr ptrdiff_t address_budget = 4 * sizeof(u32);
		pxAssertRel(materialize_bytes <= address_budget, "EE dispatcher address exceeds its reserved words");
		for (auto padding = materialize_bytes; padding < address_budget; padding += sizeof(u32))
			armAsm->Nop();
		armAsm->Bind(&general);
	}
#endif

	// Two-level LUT lookup:
	// base = recLUT[pc >> 16]
	// block = *(BASEBLOCK*)(base + pc * sizeof(BASEBLOCK)/4)
	// sizeof(BASEBLOCK) = 8, so /4 = *2, hence: base + pc*2
	// Note: use FULL pc as index (not pc & 0xFFFF) because recLUT_SetPage
	// adjusts the base address to account for the upper bits.
	armAsm->Lsr(a64::w1, a64::w0, 16);
#ifdef PS2_EE_DISPATCH_ALIGNED_LUT
	emitEeDispatchTableAddress(recLUT);
#else
	armMoveAddressToReg(RSCRATCHADDR, recLUT);
#endif
	armAsm->Ldr(RSCRATCHADDR, a64::MemOperand(RSCRATCHADDR, a64::x1, a64::LSL, 3));

	// Index with the full, zero-extended PC: base + pc * 2.
#if PS2_EE_DISPATCH_KNOWN_PC
	// Compute the byte offset independently of the first lookup's result.
	// The indexed load can then consume that result directly.
	armAsm->Ubfiz(a64::x1, a64::x0, 1, 32);
	armAsm->Ldr(RSCRATCHADDR, a64::MemOperand(RSCRATCHADDR, a64::x1));
#else
	armAsm->Add(RSCRATCHADDR, RSCRATCHADDR, a64::Operand(a64::x0, a64::LSL, 1));
	armAsm->Ldr(RSCRATCHADDR, a64::MemOperand(RSCRATCHADDR));
#endif

	armAsm->Br(RSCRATCHADDR);

	return retval;
}

#if PS2_EE_DISPATCH_KNOWN_PC
static const void* emitEeDispatchTarget(const a64::Register& pc)
{
	// The inner entry has no BTI. Use it only within immediate B/BL reach,
	// leaving enough room for a register move and any pending literal pool.
	const s64 displacement = GetPCDisplacement(armGetCurrentCodePointer(), DispatcherRegWithPc);
	constexpr s64 reach = (s64{1} << 25) - (_1mb / 4);
	if (displacement <= -reach || displacement >= reach)
		return DispatcherReg;

	if (!pc.Is(a64::w0))
		armAsm->Mov(a64::w0, pc);
	return DispatcherRegWithPc;
}
#endif

static const void* _DynGen_JITCompile()
{
	u8* retval = armGetCurrentCodePointer();
	armAsm->bti(a64::EmitBTI_jc);

	// Flush pinned cycle delta (as the absolute counter) before the C call,
	// then re-derive after — recRecompile itself doesn't modify
	// cpuRegs.cycle, but other paths (e.g. block discard) might, and the
	// convention is "every C-call boundary syncs RECCYCLE both ways".
	armFlushCycleDelta();

	armFlushEEGPRPins(); // lazy-dirty seam: compile-time hooks read guest GPRs
	armAsm->Ldr(RWARG1, armCpuRegMem(&cpuRegs.pc));
	armEmitCall((void*)recRecompile);

	armReloadCycleDelta();
	// Compile-time hooks (EntryPointCompilingOnCPUThread → game starting /
	// ELF load) can write guest GPRs — refresh the pin mirrors.
	armReloadEEGPRPins();

	armEmitJmp(DispatcherReg);

	return retval;
}

static const void* _DynGen_DispatcherEvent()
{
	u8* retval = armGetCurrentCodePointer();
	armAsm->bti(a64::EmitBTI_jc);
	// Flush pinned cycle delta for recEventTest (it reads cpuRegs.cycle for
	// counter / interrupt scheduling), then re-derive — the event test both
	// modifies cpuRegs.cycle (e.g. fast-forwarding to nextEventCycle) and
	// reschedules nextEventCycle itself.
	armFlushCycleDelta();
	// Store every pin: a savestate save from the host message pump at vsync
	// reads GPR memory, and recExitExecution leaves with only memory.
	armFlushEEGPRPins();
	armEmitCall(RecEventTestFor(IopThread::GetMode()));
	armReloadCycleDelta();
	// A normal return means nothing wrote guest GPR memory: interrupt and
	// exception delivery write only CP0 and pc, and whatever does rewrite GPRs
	// (savestate load, VM reset, a recompiler reset from a settings change)
	// also requests an exit, so recEventTest leaves through recExitExecution
	// and EnterRecompiledCode reloads every pin on the way back in. The
	// callee-saved pins are still in their registers; reload only the ones
	// the call destroyed. The stub's bytes stay a compile-time constant,
	// which an in-place reset from recEventTest relies on: it returns into
	// the regenerated copy at the same offset.
	armReloadEEClobberedPins();
	return retval; // falls through to DispatcherReg
}

// Entry and inlined COP2 must agree on the table represented by x19.
void armEmitEeMemoryBase()
{
	// Load the active EE address-translation base into x19
	if (CHECK_FASTMEM)
	{
		armMoveAddressToReg(RSCRATCHADDR, &vtlb_private::vtlbdata.fastmem_base);
		armAsm->Ldr(RFASTMEMBASE, a64::MemOperand(RSCRATCHADDR));
	}
	else
	{
		// x19 is the VTLB table base throughout softmem EE execution.
#ifdef PS2_EE_SOFTMEM_TAGGED_PAGES
		if (!g_eeSoftmemUseCanonicalMap)
		{
			armMoveAddressToReg(RFASTMEMBASE, eeSoftmemJitBase());
			return;
		}
#endif
		// Read the current global pointer on every entry/bridge return.
		armMoveAddressToReg(RSCRATCHADDR, &vtlb_private::vtlbdata.vmap);
		armAsm->Ldr(RFASTMEMBASE, a64::MemOperand(RSCRATCHADDR));
	}
}

static const void* _DynGen_EnterRecompiledCode()
{
	u8* retval = armGetCurrentCodePointer();
	armAsm->bti(a64::EmitBTI_jc);

	// We never return through this function — we exit via fastjmp_jmp (longjmp).
	// fastjmp_set/fastjmp_jmp save and restore callee-saved registers, so we
	// don't need armBeginStackFrame. Just align the stack (AArch64 requires
	// 16-byte alignment). Match x86 pattern: only adjust SP, don't save regs.
	armAsm->Sub(a64::sp, a64::sp, 16);

	// Park PS2 FPU clamp constants in callee-saved scalar NEON registers.
	// s8 = +FLT_MAX, s9 = -FLT_MAX. AAPCS64 preserves the lower 64 bits of
	// d8-d15 across C calls, so these survive every armEmitCall path inside
	// JIT blocks without compile-time tracking. fpuClampResult and iCOP2
	// scalar VDIV/VSQRT/VRSQRT clamps read them directly. v8/v9 are removed
	// from the NEON allocator pool (see NEON_RESERVED_FPU_{MAX,MIN} in
	// iCore-arm64.cpp), so nothing in JIT codegen can clobber them.
	armAsm->Ldr(a64::s8, FLT_MAX);
	armAsm->Ldr(a64::s9, -FLT_MAX);

	// Same convention, three registers along: d11 = 2^kEeFprScaleExp, which
	// turns a slot into the value it denotes. The contract is on
	// NEON_RESERVED_EEFPU_UNSCALE; iFPUd-arm64.cpp is the consumer.
	//
	// Emitted unconditionally rather than under CHECK_FPU_FULL: two
	// instructions once per JIT entry are not worth a dispatcher that goes
	// stale if the clamp mode changes without a recompiler reset.
	armAsm->Mov(RXSCRATCH, kEeFprUnscaleBits);
	armAsm->Fmov(a64::VRegister(NEON_RESERVED_EEFPU_UNSCALE, 64), RXSCRATCH);

	armEmitEeMemoryBase();

	// Load &cpuRegs into RSTATE. Callee-saved, never modified by C, so this
	// load happens once per JIT entry. Subsequent cpuRegs.X accesses become
	// `Ldr/Str ..., [RSTATE, #offsetof(...)]` instead of materializing the
	// full address each time.
	armMoveAddressToReg(RSTATE, &cpuRegs);

	// Load pinned cycle delta into RECCYCLE (cycle - nextEventCycle; see
	// iR5900-arm64.h). The convention is that RECCYCLE holds the delta for
	// the entire duration of JIT execution, with flush+reload around C
	// calls that touch cycle/nextEventCycle.
	armReloadCycleDelta();

	// Load the write-through pin mirrors for every kEEPinTable entry
	// (see the REEPIN_* doc in iR5900-arm64.h).
	armReloadEEGPRPins();

	// Load &VU0 into RVU0. Same idea as RSTATE: VU0 is a static reference
	// (constant address), so iCOP2 codegen can reach every VURegs field via
	// [RVU0, #imm12]. Survives both armEmitCall and mVU dispatcher runs.
	armMoveAddressToReg(RVU0, &VU0);

	// Jump into dispatcher
	armEmitJmp(DispatcherReg);

	// Exit point — restore callee-saved and return
	// We get here via fastjmp_jmp, not via normal return
	return retval;
}

static const void* _DynGen_DispatchBlockDiscard()
{
	u8* retval = armGetCurrentCodePointer();
	armAsm->bti(a64::EmitBTI_jc);
	armFlushEEClobberedPins(); // lazy-dirty seam: pairs with the reload below
	armEmitCall((void*)dyna_block_discard);
	// DispatcherReg's cache-hit path reloads nothing — restore the
	// caller-saved pins the C call clobbered before re-entering blocks.
	armReloadEEClobberedPins();
	armEmitJmp(DispatcherReg);
	return retval;
}

static const void* _DynGen_DispatchPageReset()
{
	u8* retval = armGetCurrentCodePointer();
	armAsm->bti(a64::EmitBTI_jc);
	armFlushEEClobberedPins(); // lazy-dirty seam: pairs with the reload below
	armEmitCall((void*)dyna_page_reset);
	// See _DynGen_DispatchBlockDiscard.
	armReloadEEClobberedPins();
	armEmitJmp(DispatcherReg);
	return retval;
}

static const void* _DynGen_UnmappedRecLUTPage()
{
	u8* retval = armGetCurrentCodePointer();
	armAsm->bti(a64::EmitBTI_jc);
	armAsm->Mov(RWARG1, 0);
	armEmitCall((void*)(void(*)(u32))recError);
	return retval;
}

#if FPU_GUARD_MASK_STUB
// Shared PS2 FPU guard-bit masking stub. Branchless, GPR-only; does the masking
// but not the fadd/fsub, so one stub serves both add and sub (the caller does
// the op):
//   in:  w0 = operand A bits, w1 = operand B bits
//   out: w0 = masked A bits,  w1 = masked B bits
//   clobbers w0/w1/w8/w9/w10/w16/w17, x30; no NEON, no memory. Leaf (ret x30).
//
// ⚠️ Scratches must stay OFF the int-allocator pools: a resident FCR31
// (ARM64TYPE_FPRC, GE-12) lives in {x2-x7, x14, x15} across ops and is NOT
// evicted around this call (the caller emits a raw bl, no iFlushCall). The
// original w2-w6 scratch choice corrupted it — the SotC guard-bit bug; see
// fpuEmitGuardedAddSub's contract note and the
// EeRecFpu.CompareSurvivesInterposedGuardedAdd* tests. w8-w10 are the
// non-allocatable value scratches, w16/w17 the vixl/addr scratches (dead
// across a bl — veneers may clobber them anyway).
static const void* _DynGen_FpuGuardMaskStub()
{
	u8* retval = armGetCurrentCodePointer();
	armAsm->bti(a64::EmitBTI_jc);
	armAsm->Ubfx(a64::w8, a64::w0, 23, 8);              // expA
	armAsm->Ubfx(a64::w9, a64::w1, 23, 8);              // expB
	armAsm->Sub(a64::w8, a64::w8, a64::w9);             // diff = expA - expB
	armAsm->Sub(a64::w9, a64::w8, 1);                   // diff - 1
	armAsm->Mvn(a64::w10, a64::wzr);                    // 0xffffffff
	armAsm->Lsl(a64::w9, a64::w10, a64::w9);            // mask B: clear low (diff-1) bits
	armAsm->Lsl(a64::w16, a64::w10, 31);                // 0x80000000 (sign-only)
	armAsm->Cmp(a64::w8, 25);
	armAsm->Csel(a64::w9, a64::w16, a64::w9, a64::ge);  // diff >= 25 -> keep only sign
	armAsm->And(a64::w9, a64::w1, a64::w9);             // masked-B candidate
	armAsm->Mvn(a64::w17, a64::w8);                     // ~diff = -diff - 1
	armAsm->Lsl(a64::w17, a64::w10, a64::w17);          // mask A: clear low (-diff-1) bits
	armAsm->Cmn(a64::w8, 25);
	armAsm->Csel(a64::w17, a64::w16, a64::w17, a64::le); // diff <= -25 -> keep only sign
	armAsm->And(a64::w17, a64::w0, a64::w17);           // masked-A candidate
	armAsm->Cmp(a64::w8, 0);
	armAsm->Csel(a64::w1, a64::w9, a64::w1, a64::gt);   // diff > 0 -> B is smaller, mask B
	armAsm->Csel(a64::w0, a64::w17, a64::w0, a64::lt);  // diff < 0 -> A is smaller, mask A
	armAsm->Ret();
	return retval;
}
#endif // FPU_GUARD_MASK_STUB

static void _DynGen_Dispatchers()
{
	const u8* start = armGetCurrentCodePointer();

	DispatcherEvent = _DynGen_DispatcherEvent();
	DispatcherReg = _DynGen_DispatcherReg();
#if FPU_GUARD_MASK_STUB
	g_fpuGuardMaskStub = _DynGen_FpuGuardMaskStub();
#endif
	cop2DynGenSyncStubs();
	cop2DynGenModelStubs();
	fpuDynGenModelStubs();

	JITCompile = _DynGen_JITCompile();
	EnterRecompiledCode = _DynGen_EnterRecompiledCode();
	DispatchBlockDiscard = _DynGen_DispatchBlockDiscard();
	DispatchPageReset = _DynGen_DispatchPageReset();
	UnmappedRecLUTPage = _DynGen_UnmappedRecLUTPage();

	// Block linker: stale / not-yet-compiled link sites divert to
	// DispatcherReg — re-dispatch from the pc the site's tail already
	// stored, never a direct re-compile (see the stale-dispatch policy in
	// BaseblockEx-arm64.h). JITCompile is the direct-instantiation
	// fallback and stays registered for the unit tests' sake.
	recBlocks.SetJITCompile(JITCompile);
	recBlocks.SetDispatcher(DispatcherReg);

	Perf::any.Register(start, static_cast<u32>(armGetCurrentCodePointer() - start), "EE Dispatcher");
}

// =====================================================================================================
//  Error handling
// =====================================================================================================

static void recError(u32 error)
{
	switch (error)
	{
		case 0:
			Host::ReportErrorAsync("R5900 Exception",
				fmt::format("Unrecognized opcode (PC: 0x{:08x})", cpuRegs.pc));
			break;

		case 1:
			Host::ReportErrorAsync("R5900 Exception",
				fmt::format("Jump to unaligned address (PC: 0x{:08x})", cpuRegs.pc));
			break;
	}

	VMManager::SetPaused(true);
	Cpu->ExitExecution();
}

// =====================================================================================================
//  Code generation helpers
// =====================================================================================================

void iFlushCall(int flushtype)
{
	// EP-2b: the COP2 VF residency cache lives in caller-saved q16-q20 and
	// never survives a C-call seam — write back dirty slots and invalidate
	// before anything else. Every block tail also funnels through here; the
	// SetBranch* fork tails restore the compile-time state afterwards
	// (Cop2VfCacheScope) so each fork emits its own writebacks.
	cop2VfCacheFlush();

	// SL-13: the callee may clobber the caller-saved q25/q26 clamp-constant
	// broadcasts — pure compile-time invalidation (constants are clean by
	// definition; the next clamp site re-materializes with 2 Dups).
	// Unconditional on flushtype: ANY C call can clobber them.
	cop2ClampConstsInvalidate();

	// Free caller-saved registers
	for (int i = 0; i < NUM_ARM_GPR_REGS; i++)
	{
		if (!arm64gprs[i].inuse)
			continue;

		if (!armIsCalleeSavedRegister(i) ||
			((flushtype & FLUSH_FREE_NONTEMP_X86) && arm64gprs[i].type != ARM64TYPE_TEMP) ||
			((flushtype & FLUSH_FREE_TEMP_X86) && arm64gprs[i].type == ARM64TYPE_TEMP))
		{
			_freeArm64GPR(i);
		}
	}

	// GE-15: FPR-class slots (NEONTYPE_FPREG/FPACC) in the callee-saved
	// q10-q15 range survive plain C-helper seams — AAPCS64 preserves the
	// LOWER 64 bits of v8-v15, and this class only ever reads/writes lane 0,
	// at S width, or at D width where eeClampMode 3 and up put a relocated
	// double in the slot. Both fit the preserved half, and _writebackNEONreg stores
	// the same width it filled, so post-call garbage above it is never
	// observed. Writeback if
	// dirty but KEEP mapped — 4248's writeback-dirty-but-keep passes, whose
	// type-mask 0x482 likewise excludes the 128-bit classes: GPRREG quads /
	// VFREG upper lanes are caller-saved and must still flush
	// (feedback_arm64_callee_saved_neon_const_pattern).
	//
	// Retention is vetoed by FLUSH_FREE_XMM: FLUSH_EVERYTHING/INTERPRETER
	// carry it (interp fallbacks like recRSQRT_S RMW fpr[]/ACC memory in C,
	// so a kept slot would go stale), and the COP2 macro wrappers pass it
	// explicitly (the inlined mVU bodies need the NEON file to themselves).
	// Everything outside q10-q15 always frees (x86 iFlushCall model,
	// pcsx2/x86/ix86-32/iR5900.cpp:1196-1207 — all XMM caller-saved there).
	for (int i = 0; i < NUM_ARM_NEON_REGS; i++)
	{
		if (!arm64neon[i].inuse)
			continue;

		const bool retainable = !(flushtype & FLUSH_FREE_XMM) &&
			static_cast<u32>(i) >= NEON_CALLEE_SAVED_START &&
			static_cast<u32>(i) < NEON_CALLEE_SAVED_END &&
			(arm64neon[i].type == NEONTYPE_FPREG || arm64neon[i].type == NEONTYPE_FPACC);
		if (retainable)
			_flushNEONreg(i);
		else
			_freeNEONreg(i);
	}

	if (flushtype & FLUSH_ALL_X86)
		_flushArm64GPRregs();

	// the callee can't write guest GPRs, so const values are written back
	// for memory coherence but the marks survive. Post-call code keeps
	// folding const addresses (x86 parity: FLUSH_FULLVTLB doesn't even
	// write the values). Without the modifier (interpreter seams),
	// delete: the callee may change cpuRegs.GPR.
	if (flushtype & FLUSH_CONSTANT_REGS)
		_flushConstRegs(!(flushtype & FLUSH_CONST_KEEP));

	if ((flushtype & FLUSH_PC) && !g_cpuFlushedPC)
	{
		armAsm->Mov(RWSCRATCH, pc);
		armAsm->Str(RWSCRATCH, armCpuRegMem(&cpuRegs.pc));
		g_cpuFlushedPC = true;
	}

	if ((flushtype & FLUSH_CODE) && !g_cpuFlushedCode)
	{
		armAsm->Mov(RWSCRATCH, cpuRegs.code);
		armAsm->Str(RWSCRATCH, armCpuRegMem(&cpuRegs.code));
		g_cpuFlushedCode = true;
	}

	// Lazy-dirty pin seam (EE-SRA 2 WS-B; no-op in write-through mode).
	// Flush ALL pins only before GPR-READING callees — the exact
	// FLUSH_INTERPRETER mode (recCall/recBranchCall interpreter fallbacks).
	// FLUSH_KEEP_PINS opts out for callees with a known read set and no GPR
	// writes (recBranchCallNoGprWrite, Interp::MTC0), whose call sites store
	// only the pins they need. FLUSH_EVERYTHING at block tails is NOT a C seam: dirty
	// pins deliberately ride across the dispatcher in their registers (the
	// entire prize), and the JIT-exit paths (JITCompile/DispatcherEvent
	// stubs) flush explicitly. Lighter modes flush nothing here — their
	// call sites carry the caller-saved flush paired with the reload.
	// Ordered after _flushConstRegs: a const-tracked pinned reg
	// materializes through the pin first. First lazy census with the naive
	// FLUSH_ALL_X86 gate: +224k emitted Strs (+20%) — placement is the
	// whole game here.
	if ((flushtype & FLUSH_INTERPRETER) == FLUSH_INTERPRETER && !(flushtype & FLUSH_KEEP_PINS))
		armFlushEEGPRPins();

#if 0
	// Disabled in x86 too (iR5900.cpp:1235-1242). Left here #if 0'd so the
	// FLUSH_CAUSE / g_maySignalException mechanism stays a faithful mirror of
	// x86 and re-enables in lockstep if upstream ever does.
	if ((flushtype == FLUSH_CAUSE) && !g_maySignalException)
	{
		if (g_recompilingDelaySlot)
		{
			armAsm->Ldr(RWSCRATCH, armCpuRegMem(&cpuRegs.CP0.n.Cause));
			armAsm->Orr(RWSCRATCH, RWSCRATCH, 1u << 31); // BD
			armAsm->Str(RWSCRATCH, armCpuRegMem(&cpuRegs.CP0.n.Cause));
		}
		g_maySignalException = true;
	}
#endif
}

// Emit cpuRegs.CP0.n.Cause &= ~(1 << 31) — clears the branch-delay (BD) bit.
// Mirrors x86's inline `xAND(ptr32[&cpuRegs.CP0.n.Cause], ~(1 << 31))`.
static void armClearCauseBD()
{
	armAsm->Ldr(RWSCRATCH, armCpuRegMem(&cpuRegs.CP0.n.Cause));
	armAsm->And(RWSCRATCH, RWSCRATCH, ~(1u << 31));
	armAsm->Str(RWSCRATCH, armCpuRegMem(&cpuRegs.CP0.n.Cause));
}

// Interp-called ops assume the interpreter's post-fetch convention:
// cpuRegs.pc = op + 4 at handler entry (trap()/SYSCALL/BREAK subtract 4 to
// find the faulting op). Non-delay-slot compiles advance `pc` before the
// body, so FLUSH_PC already matches; delay slots advance only afterwards —
// compensate here or a raising delay-slot op computes EPC one slot low
// under the cpuRegs.branch bracket. (AX-05)
static u32 interpCallFlushPcBias()
{
	return g_recompilingDelaySlot ? 4 : 0;
}

// Bridges for mVU macro-mode emit bodies (can't see kEEPinTable): emit the
// caller-saved pin flush-before / reload-after around a C call emitted inline
// into an EE block. The flush is a lazy-dirty-mode no-op in write-through.
void armEmitEEClobberedPinFlushForCOP2()
{
	armFlushEEClobberedPins();
}

void armEmitEEClobberedPinReloadForCOP2()
{
	armReloadEEClobberedPins();
}

void recCall(void (*func)())
{
	const u32 saved_pc = pc;
	pc += interpCallFlushPcBias();
	iFlushCall(FLUSH_INTERPRETER);
	pc = saved_pc;

	// Flush the delta → cpuRegs.cycle so the interpreter sees the live cycle
	// value (some opcodes — COP0 Count, TLB miss, branch helpers — read it).
	armFlushCycleDelta();

	armEmitCall((void*)func);

	// Re-derive the delta in case the interpreter modified cpuRegs.cycle or
	// rescheduled nextEventCycle.
	armReloadCycleDelta();

	// The interpreter writes guest GPRs in memory — refresh the pin mirrors.
	armReloadEEGPRPins();
}

void recBranchCall(void (*func)())
{
	const u32 saved_pc = pc;
	pc += interpCallFlushPcBias(); // see recCall (AX-05)
	iFlushCall(FLUSH_INTERPRETER);
	pc = saved_pc;

	// Apply accumulated block cycles to the delta, then flush to memory
	// before the C call — the interpreter's intEventTest reads
	// cpuRegs.cycle. Re-derive after, so the g_branch=2 exit code that
	// follows can keep using RECCYCLE.
	u32 cycles = scaleblockcycles_clear(EeChargeForm::AddImm12);
	if (cycles > 0)
		armAsm->Add(RECCYCLE, RECCYCLE, cycles);
	recEeNoteChargeSite(EeChargeSite::BranchCall);

	armFlushCycleDelta();

	armEmitCall((void*)func);
	g_branch = 2;

	armReloadCycleDelta();
	armReloadEEGPRPins();
}

// recBranchCall for a callee that reads at most the guest GPRs in `gpr_reads`
// (bit n = GPR n) from memory and writes none: ERET, EI, and SYSCALL when the
// call number is a constant the interpreter has no case for. The full seam
// stores all nine pins and reloads all nine; this one stores the pins the
// callee reads plus the caller-saved pins, and reloads only the caller-saved
// ones. Callee-saved pins stay dirty in their registers across the call and
// across the dispatcher, under the same rule as any block tail: every later
// C reader of GPR memory (the event-test and compile stubs, interpreter
// fallbacks) stores the pins itself first.
//
// The callee must not write a guest GPR. A write would land in memory behind
// a callee-saved pin that is not reloaded, and the pin would overwrite it at
// the next flush.
void recBranchCallNoGprWrite(void (*func)(), u32 gpr_reads)
{
	const u32 saved_pc = pc;
	pc += interpCallFlushPcBias(); // see recCall
	iFlushCall(FLUSH_INTERPRETER | FLUSH_KEEP_PINS);
	pc = saved_pc;

	u32 cycles = scaleblockcycles_clear(EeChargeForm::AddImm12);
	if (cycles > 0)
		armAsm->Add(RECCYCLE, RECCYCLE, cycles);
	recEeNoteChargeSite(EeChargeSite::BranchCallNoGprWrite);

	armFlushCycleDelta();
	armFlushEEPinsForCall(gpr_reads);

	armEmitCall((void*)func);
	g_branch = 2;

	armReloadCycleDelta();
	armReloadEEClobberedPins();
}

// s_nBlockCycles is 3-bit fixed point. Divide by 8 when done!
// Scaling blocks under 40 cycles seems to produce countless problems, so let's try to avoid them.
// Matches x86 scaleblockcycles_calculation() in ix86-32/iR5900.cpp
#define DEFAULT_SCALED_BLOCKS() (s_nBlockCycles >> 3)

// The scaler for an explicit selector. The production entry point below passes the
// live effective one; the encoding-form classifier passes each selector the governor
// could move to, to see what this block would be charged there. Keep this the only
// copy of the formula besides the interpreter's: a slip in either is a silent timing
// change (ee_rec_cycle_rate_tests).
static u32 scaleblockcycles_calculation_for(s8 cyclerate)
{
	const bool lowcycles = (s_nBlockCycles <= 40);
	u32 scale_cycles = 0;

	if (cyclerate == 0 || lowcycles || cyclerate < -99 || cyclerate > 3)
		scale_cycles = DEFAULT_SCALED_BLOCKS();

	else if (cyclerate > 1)
		scale_cycles = s_nBlockCycles >> (2 + cyclerate);

	else if (cyclerate == 1)
		scale_cycles = DEFAULT_SCALED_BLOCKS() / 1.3f;

	else if (cyclerate == -1)
		scale_cycles = (s_nBlockCycles <= 80 || s_nBlockCycles > 168 ? 5 : 7) * s_nBlockCycles / 32;

	else
		scale_cycles = ((5 + (-2 * (cyclerate + 1))) * s_nBlockCycles) >> 5;

	return (scale_cycles < 1) ? 1 : scale_cycles;
}

static u32 scaleblockcycles_calculation()
{
	return scaleblockcycles_calculation_for(EECycleRate::GetEffective());
}

static u32 scaleblockcycles_clear_impl()
{
	const u32 scaled = scaleblockcycles_calculation();

	const s8 cyclerate = EECycleRate::GetEffective();
	const bool lowcycles = (s_nBlockCycles <= 40);

	if (!lowcycles && cyclerate > 1)
		s_nBlockCycles &= (0x1 << (cyclerate + 2)) - 1;
	else
		s_nBlockCycles &= 0x7;

	return scaled;
}

// Charge sites emitted since the last reset. Counted in scaleblockcycles_clear()
// rather than at each emitter: every emitter calls it once per site, and the scaler
// floors its charge at 1 (pinned by ee_rec_cycle_rate_tests), so a consumed charge
// always has an instruction emitted for it.
static u32 s_eeChargeSites = 0;

// Sites whose charge would not stay in one instruction across the governor's
// window, and blocks holding at least one of them.
static u32 s_eeFormChangeSites = 0;
static u32 s_eeFormChangeBlocks = 0;
static bool s_eeBlockHasFormChange = false;

// Does `charge` still fit the single-instruction form this site was emitted
// in? A charge that stops fitting cannot be patched in place: vixl would emit a
// longer sequence for it, and there is no room on the page.
//
// The MOVZ test is deliberately the narrow one. vixl also reaches one
// instruction through MOVN or an ORR-immediate for some values, so for that site
// the count below is an upper bound. Failing at all takes a charge over 65535,
// which no realistic block reaches.
static bool eeChargeFitsForm(u32 charge, EeChargeForm form)
{
	switch (form)
	{
		case EeChargeForm::AddImm12:
			// vixl Assembler::IsImmAddSub. Anything else makes AddSubMacro
			// acquire a scratch register and emit a materialise-then-add of
			// two to five instructions.
			return (charge <= 0xfffu) || (((charge >> 12) <= 0xfffu) && ((charge & 0xfffu) == 0));

		case EeChargeForm::MovzImm16:
			// vixl Assembler::IsImmMovz for a 32-bit destination: at least one
			// of the two halfwords clear.
			return ((charge & 0xffff0000u) == 0) || ((charge & 0x0000ffffu) == 0);
	}
	return false;
}

// Whether this site's charge stops being a single instruction at any selector in
// the governor's window, [GetFloor(), GetConfigured()] (at most three values).
// Returns true when it does: such a site cannot be repaired by rewriting its
// immediate, so the transition pass invalidates and recompiles its block instead.
static bool eeClassifyChargeForm(EeChargeForm form)
{
	const s8 floor = EECycleRate::GetFloor();
	const s8 top = EECycleRate::GetConfigured();

	for (int sel = floor; sel <= top; sel++)
	{
		if (eeChargeFitsForm(scaleblockcycles_calculation_for(static_cast<s8>(sel)), form))
			continue;

		s_eeFormChangeSites++;
		if (!s_eeBlockHasFormChange)
		{
			s_eeBlockHasFormChange = true;
			s_eeFormChangeBlocks++;
		}
		return true;
	}
	return false;
}

// The charge-site side table: one record per emitted charge, so a cycle-rate
// transition can rewrite the immediates in place instead of resetting the
// recompiler.
//
// Records live in their own vectors rather than on BASEBLOCKEX, which is memcpy'd
// on grow, memmove'd on insert and erased without destructors; a pointer field
// there would need ownership handling on three paths that have none. A flat
// vector also suits the pass, which walks in code-address order.
//
// Nothing prunes them. They grow by one record per site actually emitted, so they
// are bounded by arena occupancy. Patching a record whose block has since been
// recompiled writes into orphaned code, which nothing executes.

// This site's charge leaves its single-instruction encoding form somewhere in
// the governor's window, so its block cannot be repaired in place.
static constexpr u8 kEeChargeUnpatchable = 1u << 0;

// This site is the block's entry point, and is patchable while the block is live.
// The only thing that overwrites an entry word is Arm64BaseBlocks::Remove/Invalidate,
// which stamps a `B` redirect stub over it, so an entry record whose word no longer
// decodes as its form belongs to a removed block and the pass skips it instead of
// refusing.
static constexpr u8 kEeChargeAtBlockEntry = 1u << 1;

struct EeChargeRecord
{
	u32 offset;        // from the base of the arena this record's vector covers
	u32 raw;           // s_nBlockCycles as this site saw it, before the mask
	u32 owner_startpc; // the block that emitted it, so a flagged one is findable
	u8  form;
	u8  flags;
};

// Two vectors because the two arenas have different bases and are flushed as
// separate ranges. Appended in emit order, which is increasing code address
// within each arena.
static std::vector<EeChargeRecord> s_eeChargeRecords;
static std::vector<EeChargeRecord> s_eeColdChargeRecords;

// scaleblockcycles_clear() knows the raw and the form but runs before the
// emitter emits, so it cannot know the address. It parks what it has here and
// recEeNoteChargeSite() picks it up one instruction later.
static bool s_eeChargePending = false;
static u32 s_eeChargePendingRaw = 0;
static u32 s_eeChargePendingCharge = 0;
static EeChargeForm s_eeChargePendingForm = EeChargeForm::AddImm12;
static bool s_eeChargePendingUnpatchable = false;

// Does the word at a charge site actually carry the form the emitter declared?
// Checked at record time against what vixl really emitted, and again by the
// patch pass before it writes.
//
//   ADD/ADDS (immediate), 64-bit:  sf=1 op=0 S=? 10001 sh imm12 Rn Rd
//     0x91000000 (ADD) or 0xB1000000 (ADDS), bit 23 clear, Rn = Rd = RECCYCLE.
//   MOVZ (32-bit):                 sf=0 opc=10 100101 hw imm16 Rd
//     0x52800000 with hw = 0 and Rd = RWARG2.
//
// A charge that outgrew its immediate makes vixl acquire a scratch register
// and emit a materialise-then-add, whose last instruction is the shifted-
// REGISTER form of ADD (0x8B...). This test tells the two apart by reading the
// page, not by trusting bookkeeping.
static bool eeChargeSiteHasForm(u32 word, EeChargeForm form)
{
	switch (form)
	{
		case EeChargeForm::AddImm12:
		{
			const u32 fixed = word & 0xFF800000u;
			if (fixed != 0x91000000u && fixed != 0xB1000000u)
				return false;
			return ((word & 0x1Fu) == RECCYCLE.GetCode()) &&
			       (((word >> 5) & 0x1Fu) == RECCYCLE.GetCode());
		}

		case EeChargeForm::MovzImm16:
			return ((word & 0xFF800000u) == 0x52800000u) && ((word & 0x00600000u) == 0) &&
			       ((word & 0x1Fu) == RWARG2.GetCode());
	}
	return false;
}

// Rewrite only the immediate field, leaving every other bit as vixl emitted it
// — the S bit that separates ADD from ADDS, the operand size, the registers.
// Only meaningful when eeChargeFitsForm(charge, form) says the charge still
// fits; the caller checks that first.
static u32 eeEncodeChargeWord(u32 word, EeChargeForm form, u32 charge)
{
	switch (form)
	{
		case EeChargeForm::AddImm12:
			// sh at bit 22, imm12 at 21..10. sh = 1 is the shifted-by-12 form
			// vixl picks for a charge that is a multiple of 4096.
			word &= ~0x007FFC00u;
			return (charge <= 0xfffu) ? (word | (charge << 10))
			                          : (word | 0x00400000u | ((charge >> 12) << 10));

		case EeChargeForm::MovzImm16:
			// hw at 22..21 (stays 0), imm16 at 20..5.
			word &= ~0x007FFFE0u;
			return word | (charge << 5);
	}
	return word;
}

u32 scaleblockcycles_clear(EeChargeForm form)
{
	s_eeChargeSites++;
	pxAssertMsg(!s_eeChargePending,
		"an EE cycle charge was computed and never recorded at a site");

	// Before the impl: it masks s_nBlockCycles down to the retained remainder,
	// and both the classifier and the side table need the raw this site saw.
	s_eeChargePendingRaw = s_nBlockCycles;
	s_eeChargePendingForm = form;
	s_eeChargePendingUnpatchable = eeClassifyChargeForm(form);
	s_eeChargePendingCharge = scaleblockcycles_clear_impl();
	s_eeChargePending = true;

	// The scaler floors at 1, so every caller below emits its charge
	// instruction — the `if (cycles != 0) ... else Cmp` arms are dead. The
	// record protocol depends on that: a caller that emitted nothing would
	// leave the charge unconsumed and trip the assertion above at the next
	// site.
	pxAssert(s_eeChargePendingCharge >= 1);
	return s_eeChargePendingCharge;
}

#ifdef PCSX2_RECOMPILER_TESTS
// Per-emitter tally, for the coverage tests. Cleared with the rest of the
// side table.
static u32 s_eeChargeSiteByEmitter[static_cast<int>(EeChargeSite::Count)] = {};

u32 recEeGetChargeSiteCountFor(EeChargeSite which)
{
	return s_eeChargeSiteByEmitter[static_cast<int>(which)];
}
#endif

static void recEeNoteChargeSiteAt(EeChargeSite which, const u8* code_cursor)
{
#ifdef PCSX2_RECOMPILER_TESTS
	s_eeChargeSiteByEmitter[static_cast<int>(which)]++;
#else
	(void)which;
#endif

	pxAssertMsg(s_eeChargePending, "an EE charge site was recorded with no charge behind it");
	s_eeChargePending = false;

	// The instruction just emitted. Taken from the cursor rather than from a
	// pointer captured before the emit, because vixl may flush a literal pool
	// on its way in — the last word written is the charge either way.
	const u8* const site = code_cursor - 4;

	// A charge can be a block's own entry point, even though the manual-protection
	// check and pin reloads normally come first. Two ways in: a block whose first
	// guest instruction is a COP2 op needing a VU0 sync emits its charge Add before
	// anything else, on a page still write-protected, so there is no
	// manual-protection prologue
	// (EeVu0Cop2GuardMask.MultiplyAccumulateFormsMatchTheInterpreter compiles
	// exactly that); and constant propagation, since `addiu t0, zero, 1` emits no
	// code, so anything behind one lands on the entry.
	//
	// It is recorded, not refused: while the block is live the entry word is the
	// ADD/ADDS this record describes, and patching only moves the immediate. The
	// flag lets the pass recognise a removed block (the word is now a redirect
	// stub) without mistaking it for the recompiler contradicting itself.
	const bool at_block_entry =
		(s_pCurBlockEx && reinterpret_cast<uptr>(site) == s_pCurBlockEx->fnptr);

	const EeChargeForm form = s_eeChargePendingForm;
	const u32 charge = s_eeChargePendingCharge;

	u32 word = 0;
	std::memcpy(&word, armGetWritableCodePtr(const_cast<u8*>(site)), sizeof(word));

	const bool single = eeChargeSiteHasForm(word, form) &&
	                    (eeEncodeChargeWord(word, form, charge) == word);
	pxAssertMsg(!eeChargeFitsForm(charge, form) || single,
		"an EE cycle-charge site did not emit the single instruction its form names");

	u8 flags = 0;
	if (s_eeChargePendingUnpatchable || !single)
		flags |= kEeChargeUnpatchable;
	if (at_block_entry)
		flags |= kEeChargeAtBlockEntry;

	// Which arena. Everything at or above s_coldBase is the cold side-exit
	// arena (the constant pool above it holds no charge sites).
	const bool cold = (s_coldBase != nullptr && site >= s_coldBase);
	const u8* const base = cold ? s_coldBase : SysMemory::GetEERec();
	std::vector<EeChargeRecord>& table = cold ? s_eeColdChargeRecords : s_eeChargeRecords;

	pxAssert(site >= base);
	table.push_back(EeChargeRecord{
		static_cast<u32>(site - base),
		s_eeChargePendingRaw,
		s_pCurBlockEx ? s_pCurBlockEx->startpc : 0,
		static_cast<u8>(form),
		flags});
}

void recEeNoteChargeSite(EeChargeSite which)
{
	recEeNoteChargeSiteAt(which, armGetCurrentCodePointer());
}

#ifdef PCSX2_RECOMPILER_TESTS
u32 recEeGetChargeRecordCount()
{
	return static_cast<u32>(s_eeChargeRecords.size() + s_eeColdChargeRecords.size());
}

bool recEeArenaExtent(bool cold, uptr* base, u32* used)
{
	if (!s_coldBase || !recPtr)
		return false;
	u8* const arena_base = cold ? s_coldBase : SysMemory::GetEERec();
	u8* const arena_end = cold ? s_coldPtr : recPtr;
	*base = reinterpret_cast<uptr>(arena_base);
	*used = static_cast<u32>(arena_end - arena_base);
	return true;
}

u32 recEeEncodeChargeWordForTest(u32 word, EeChargeForm form, u32 charge)
{
	return eeEncodeChargeWord(word, form, charge);
}

bool recEeChargeWordHasFormForTest(u32 word, EeChargeForm form)
{
	return eeChargeSiteHasForm(word, form);
}
#endif

// Charge sites emitted since the last reset, and blocks currently registered.
u32 recEeGetChargeSiteCount()
{
	return s_eeChargeSites;
}

u32 recEeGetFormChangeSiteCount()
{
	return s_eeFormChangeSites;
}

u32 recEeGetFormChangeBlockCount()
{
	return s_eeFormChangeBlocks;
}

u32 recEeGetLiveBlockCount()
{
	return recBlocks.size();
}

#ifdef PCSX2_RECOMPILER_TESTS
// Test-only: drive the production block-cycle scaler with a synthetic raw
// count so a characterization table can pin the seven selector formulas
// without transcribing them into the test. Returns the charge; hands back the
// remainder the scaler retains for the next block. Restores s_nBlockCycles.
u32 recEeScaleBlockCyclesForTest(u32 raw_block_cycles, u32* out_remainder)
{
	const u32 saved = s_nBlockCycles;
	s_nBlockCycles = raw_block_cycles;
	// The impl, not the counting wrapper: a synthetic raw driven through the
	// scaler emits nothing, so it is not a charge site.
	const u32 charge = scaleblockcycles_clear_impl();
	if (out_remainder)
		*out_remainder = s_nBlockCycles;
	s_nBlockCycles = saved;
	return charge;
}
#endif

void _eeFlushAllDirty()
{
	_flushConstRegs(false);
	_flushArm64GPRregs();
	_flushNEONregs();
}

void _eeOnWriteReg(int reg, int signext)
{
	GPR_DEL_CONST(reg);
}

void _deleteEEreg(int reg, int flush)
{
	if (!reg)
		return;
	if (flush && GPR_IS_CONST1(reg))
		_flushConstReg(reg);

	GPR_DEL_CONST(reg);
	_deleteGPRtoArm64GPR(reg, flush ? DELETE_REG_FREE : DELETE_REG_FREE_NO_WRITEBACK);
	// NEON side: ALWAYS writeback before free. EE GPRs are 128-bit and scalar
	// MIPS ops only overwrite UD[0]; the slot's UD[1] holds the live upper-64
	// from a prior MMI write (MMI routes through eeRecompileCodeXMM, so Rd stays
	// live in the slot with MODE_WRITE). Dropping without writeback silently
	// zeros UD[1] in memory and breaks the interpreter's "preserve UD[1]"
	// contract for LUI/MFLO/MOVZ/ADDIU/...
	_deleteGPRtoNEONreg(reg, DELETE_REG_FREE);
}

void _deleteEEreg128(int reg)
{
	if (!reg)
		return;
	if (GPR_IS_CONST1(reg))
		_flushConstReg(reg);

	GPR_DEL_CONST(reg);
	_deleteGPRtoArm64GPR(reg, DELETE_REG_FREE_NO_WRITEBACK);
	_deleteGPRtoNEONreg(reg, DELETE_REG_FREE);
}

void _flushEEreg(int reg, bool clear)
{
	if (!reg)
		return;

	if (GPR_IS_DIRTY_CONST(reg))
		_flushConstReg(reg);
	if (clear)
		GPR_DEL_CONST(reg);

	// Per-register flush honoring reg/clear, mirroring x86 _flushEEreg.
	// clear=false → writeback but keep the allocation; clear=true → also free.
	// (The previous arm64 impl flushed ALL registers and ignored reg/clear —
	// a behavior-equivalent superset given the lone caller, but a lying API.)
	_deleteGPRtoNEONreg(reg, clear ? DELETE_REG_FLUSH_AND_FREE : DELETE_REG_FLUSH);
	_deleteGPRtoArm64GPR(reg, clear ? DELETE_REG_FLUSH_AND_FREE : DELETE_REG_FLUSH);
}

void _eeMoveGPRtoR(const a64::Register& to, int fromgpr, bool allow_preload)
{
	if (fromgpr == 0)
	{
		// r0 is always zero
		if (to.Is64Bits())
			armAsm->Mov(to, a64::xzr);
		else
			armAsm->Mov(to, a64::wzr);
		return;
	}

	if (GPR_IS_CONST1(fromgpr))
	{
		// Value known at compile time — emit immediate load
		if (to.Is64Bits())
			armAsm->Mov(to, g_cpuConstRegs[fromgpr].SD[0]);
		else
			armAsm->Mov(to, g_cpuConstRegs[fromgpr].UL[0]);
		return;
	}

	// Check if the register is currently allocated in an ARM64 GPR with
	// MODE_READ — meaning the host register holds the current guest value.
	// MODE_WRITE-only means it's a destination allocation; the current value
	// was never loaded, so the host register contains stale data.
	for (int i = 0; i < NUM_ARM_GPR_REGS; i++)
	{
		if (arm64gprs[i].inuse && arm64gprs[i].type == ARM64TYPE_GPR &&
			arm64gprs[i].reg == fromgpr && (arm64gprs[i].mode & MODE_READ))
		{
			if (to.Is64Bits())
				armAsm->Mov(to, armXRegister(i));
			else
				armAsm->Mov(to, armWRegister(i));
			return;
		}
	}

	// Check if allocated in a NEON register. A MODE_WRITE-only slot is also
	// authoritative — the MMI op that allocated it has written the live value
	// to qreg even though the slot was never MODE_READ-loaded. Reading from
	// memory in that case would return the pre-MMI stale value. eeRecompileCodeXMM
	// passes MODE_WRITE alone (no MODE_READ unless XMMINFO_READD is set) for Rd,
	// so every MMI destination lands here.
	for (int i = 0; i < NUM_ARM_NEON_REGS; i++)
	{
		if (arm64neon[i].inuse && arm64neon[i].type == NEONTYPE_GPRREG &&
			arm64neon[i].reg == fromgpr && (arm64neon[i].mode & (MODE_READ | MODE_WRITE)))
		{
			if (to.Is64Bits())
				armAsm->Fmov(to, armDRegister(i));     // FMOV Xd, Dn
			else
				armAsm->Fmov(to, armSRegister(i));      // FMOV Wd, Sn (lower 32 bits)
			return;
		}
	}

	// Not allocated anywhere — load from cpuRegs memory
	armLoadEERegPtr(to, &cpuRegs.GPR.r[fromgpr].UD[0]);
}

// Operand-substitution variant of _eeMoveGPRtoR (EE-SRA 2 WS-C): returns a
// register that CURRENTLY holds the guest value — the pin, or an allocator
// slot already in MODE_READ — emitting nothing at all in those cases; only
// the fallback (const / NEON-resident / memory) materializes into `scratch`.
// Contract:
//   - dirty-safe by construction: a resident allocator/NEON slot IS the newest
//     value (a dirty slot is exactly what we want to read), and pin / GPR-slot /
//     NEON residency are mutually exclusive (invariants I1/I2), so the
//     pin>slot>fallback order never returns a stale home. (The GE-M2 write
//     helpers below mark their dest slots MODE_READ after depositing, which is
//     what keeps a within-block writer→reader chain coherent here.)
//   - consume the returned register in the immediately following
//     instruction(s), before anything can mutate pins or allocator slots.
//   - the returned register may be `scratch` or may not be — never write it.
//   - $zero materializes 0 into `scratch` rather than returning xzr: reg 31
//     in an Rn operand position encodes SP for arithmetic ops (Cmp!), and
//     callers shouldn't have to care.
vixl::aarch64::Register _eeGetGPRSourceReg(const a64::Register& scratch, int fromgpr)
{
	if (fromgpr != 0 && !GPR_IS_CONST1(fromgpr))
	{
		// A live 128-bit NEON home (an in-flight MMI / LQ result) is authoritative
		// over BOTH the pin mirror and any GPR slot: an MMI write updates only the
		// quad, and the pin + cpuRegs memory stay stale until a seam runs
		// armStoreEEGPRQuad to refresh them. Take the no-emit pin/slot fast path
		// only when no quad is live; otherwise fall through to _eeMoveGPRtoR, which
		// reads lane 0 straight out of the quad. This is the coherence the RC0
		// residency flip depends on — pre-flip the templates flushed the source
		// (refreshing the pin) so the fast path was always current; post-flip a
		// producer's quad can still be live when a consumer reads the reg here.
		if (!_hasNEONreg(NEONTYPE_GPRREG, fromgpr))
		{
			if (const a64::Register* pin = armEEPinForGPR(fromgpr))
				return scratch.Is64Bits() ? *pin : pin->W();

			for (int i = 0; i < NUM_ARM_GPR_REGS; i++)
			{
				if (arm64gprs[i].inuse && arm64gprs[i].type == ARM64TYPE_GPR &&
					arm64gprs[i].reg == fromgpr && (arm64gprs[i].mode & MODE_READ))
				{
					return scratch.Is64Bits() ? armXRegister(i) : armWRegister(i);
				}
			}
		}
	}

	_eeMoveGPRtoR(scratch, fromgpr);
	return scratch;
}

// WRITE-side operand substitution (GE-M2 central write API). Resolve a guest
// GPR's 64-bit write home so that a hand-written op deposits its result into a
// pinned mirror or an allocator-resident host register when one exists, instead
// of always storing to cpuRegs memory — the dest counterpart of
// _eeGetGPRSourceReg. Mirrors PCSX2's x86 allocator-template dest handling
// (pcsx2/x86/ix86-32/iR5900Templates.cpp + _allocX86reg, pcsx2/x86/iCore.cpp).
//
// Returns the X-sized write home (like armEEDestForGPR — the caller writes .W()
// and sign-/zero-extends into it as the op requires). Before resolving, a scalar
// write invalidates any live 128-bit dual-residence copy (writeback first so the
// slot's UD[1] upper half survives in memory — the _deleteEEreg UD[1] rationale)
// and clears the compile-time-const flag.
//
// alloc_if_used == false (the default, used by the Phase-1 coherence sweep) never
// creates a new slot: it resolves to a pin, an already-resident slot, or the
// caller's scratch — bit-for-bit today's pin/memory behavior when nothing is
// resident. alloc_if_used == true lets the op claim a fresh MODE_WRITE slot for a
// still-live dest (the residency flip, Phases 3/4).
//
// Emit-ordering contract (identical to armEEDestForGPR): ONLY the final
// result-producing instruction may target the returned register, reading its
// guest sources in that same instruction; follow it with _eeStoreGPRDestReg
// before anything can observe the reg.
vixl::aarch64::Register _eeGetGPRDestReg(int gpr, const a64::Register& scratch, bool alloc_if_used)
{
	// Scalar write kills any 128-bit copy (writeback preserves UD[1]) and any
	// known-constant value for this reg.
	_deleteGPRtoNEONreg(gpr, DELETE_REG_FREE);
	GPR_DEL_CONST(gpr);

	if (const a64::Register* pin = armEEPinForGPR(gpr))
		return *pin;

	int r = _checkArm64GPR(ARM64TYPE_GPR, gpr, MODE_WRITE);
	if (r < 0 && alloc_if_used && EEINST_USEDTEST(gpr))
		r = _allocArm64GPR(ARM64TYPE_GPR, gpr, MODE_WRITE);
	if (r >= 0)
		return armXRegister(r);

	return scratch.X();
}

// Deposit `src` (the dest home itself, a source register, or xzr) into gpr's
// resolved home. An allocator-resident dest takes a DEFERRED store: mark the
// slot MODE_READ so _eeGetGPRSourceReg / branch / COP2 consumers pick the value
// up before the next seam writes it back, and Mov only when src isn't already
// the slot's host reg. A pinned or memory-backed dest takes the existing
// lazy-dirty / canonical helper (a src that IS the pin emits nothing).
void _eeStoreGPRDestReg(int gpr, const a64::Register& src)
{
	const int r = _checkArm64GPR(ARM64TYPE_GPR, gpr, MODE_READ | MODE_WRITE);
	if (r >= 0)
	{
		if (src.GetCode() != static_cast<unsigned>(r))
			armAsm->Mov(armXRegister(r), src.X());
		return;
	}
	armStoreEERegPtr(src, &cpuRegs.GPR.r[gpr].UD[0]);
}

#ifdef PCSX2_DEVBUILD
// GE-M2 coherence tripwire body (see armAssertRawGPRPtrCoherent's header
// declaration for the full rationale). Reject a raw guest-GPR memory access
// only when the reg's allocator/NEON copy is DIRTY (memory stale) and the reg
// is neither $zero nor pinned.
void armAssertRawGPRPtrCoherent(const void* field)
{
	const uptr base = reinterpret_cast<uptr>(&cpuRegs.GPR.r[0]);
	const uptr f = reinterpret_cast<uptr>(field);
	if (f < base || f >= base + sizeof(cpuRegs.GPR.r))
		return; // not a guest-GPR pointer

	const uptr rel = f - base;
	if ((rel % sizeof(cpuRegs.GPR.r[0])) >= 8)
		return; // upper 64 bits are never scalar-resident

	const int n = static_cast<int>(rel / sizeof(cpuRegs.GPR.r[0]));
	if (n == 0 || armEEPinForGPR(n) != nullptr)
		return; // $zero and pinned regs: the raw path is authoritative

	pxAssertMsg(!_hasArm64GPR(ARM64TYPE_GPR, n, MODE_WRITE) &&
					!_hasNEONreg(NEONTYPE_GPRREG, n, MODE_WRITE),
		"GE-M2 I3: raw guest-GPR memory access while its allocator/NEON copy is dirty");
}
#endif

// =====================================================================================================
//  Branch handling
// =====================================================================================================

// Call-ret shadow-stack ring (P2-2; FEX-Emu design, adapted). Frames are
// {u64 guest return PC, u64 host landing}; the landing sits right after the
// call site's BL, so a matching guest JR-$ra can RET to it and the hardware
// return-address stack — pushed by that BL — predicts the transfer.
//
// Ring instead of FEX's guard-page stack: the 64 KiB ring wraps a 16-bit
// offset, stored separately or in the low bits of an aligned frame pointer.
// Over/underflow simply cycles it — no bounds checks, no
// SIGSEGV recentering (our PageFaultHandler interface exposes no ucontext),
// and net push/pop imbalance (interpreter-path calls, exceptions, thread
// switches) degrades to compare-misses that re-sync within one call depth.
//
// Consuming a frame is correct whenever its guestRA matches the live target:
// the landing's B is linked to that same PC, and Remove() keeps dead block
// entries resolving via redirect stubs, so stale-but-matching frames stay
// VALID across every recClear. Only recResetRaw (code cache rewind) dangles
// host pointers — it fills with an aligned zero PC and a dispatcher landing.
// Matching a zero sentinel is equivalent to a miss: it RETs to the dispatcher.
namespace
{
	constexpr u32 kEECallRetRingBytes = 0x10000; // 4096 x 16-byte frames
	constexpr u64 kEECallRetSentinelRA = 0;
	static_assert(kEECallRetRingBytes == (1u << 16),
		"the emitted push and pop wrap the offset in its own 16 bits");

#ifdef PS2_EE_CALLRET_CURSOR
	// An NRO mapping need not preserve over-alignment. Align the live ring
	// from its backing storage at runtime.
#ifdef PS2_EE_CALLRET_POST_INDEX
	constexpr u32 kEECallRetRingAlignment = 2 * kEECallRetRingBytes;
#else
	constexpr u32 kEECallRetRingAlignment = kEECallRetRingBytes;
#endif
	alignas(16) u8 s_eeCallRetStorage[kEECallRetRingBytes + kEECallRetRingAlignment - 16];

	u64* eeCallRetRing()
	{
		const uptr address = reinterpret_cast<uptr>(s_eeCallRetStorage + kEECallRetRingAlignment - 16);
		return reinterpret_cast<u64*>(address & ~(static_cast<uptr>(kEECallRetRingAlignment) - 1));
	}

	// x9 is the current frame. Keep the ring's high address bits and wrap
	// only its low 16 bits, in either direction, without a second state load.
	void emitCallRetAdvanceCursor(bool push)
	{
		if (push)
			armAsm->Sub(a64::w10, a64::w9, 16);
		else
			armAsm->Add(a64::w10, a64::w9, 16);
		armAsm->Bfxil(a64::x9, a64::x10, 0, 16);
		armAsm->Str(a64::x9, armCpuRegMem(&_cpuRegistersPack.eeCallRetBase));
	}
#else
	alignas(16) u8 s_eeCallRetStorage[kEECallRetRingBytes];

	u64* eeCallRetRing()
	{
		return reinterpret_cast<u64*>(s_eeCallRetStorage);
	}
#endif

	void eeCallRetResetRing()
	{
		u64* p = eeCallRetRing();
		for (size_t i = 0; i < kEECallRetRingBytes / 8; i += 2)
		{
			p[i] = kEECallRetSentinelRA;
			p[i + 1] = reinterpret_cast<uptr>(DispatcherReg);
		}
		_cpuRegistersPack.eeCallRetBase = reinterpret_cast<u64>(p);
		_cpuRegistersPack.eeCallRetOff = 0;
	}

	// (Push/pop emission is gated off at the recJAL/recJALR/recJR call sites
	// under GoemonTlbHack: SetBranchReg compares V2P-translated targets
	// there, which can never match the virtual frame RAs. Gamefix flips
	// reset the rec, so emit-time gating is sound.)

	// {guestRA, landing} frame push. Emitted between the tail flush and the
	// event check: the frame must exist on EVERY downstream path (an event
	// detour still reaches the callee, whose eventual return must find a
	// balanced ring). Clobbers x8/x9/x10/x17; all dead post-flush, and the
	// tier-2 pins (x11-x13) are untouched.
	void emitCallRetPush(u32 return_pc, a64::Label* landing)
	{
		pxAssert((return_pc & 3) == 0);
		armAsm->Mov(RXSCRATCH, return_pc);
		armAsm->Adr(RSCRATCHADDR, landing);
		armAsm->Ldr(a64::x9, armCpuRegMem(&_cpuRegistersPack.eeCallRetBase));
#ifdef PS2_EE_CALLRET_CURSOR
		emitCallRetAdvanceCursor(true);
#else
		armAsm->Ldrh(a64::w10, armCpuRegMem(&_cpuRegistersPack.eeCallRetOff));
		armAsm->Sub(a64::w10, a64::w10, 16);
		armAsm->Strh(a64::w10, armCpuRegMem(&_cpuRegistersPack.eeCallRetOff));
		armAsm->Add(a64::x9, a64::x9, a64::Operand(a64::w10, a64::UXTH));
#endif
		armAsm->Stp(RXSCRATCH, RSCRATCHADDR, a64::MemOperand(a64::x9));
	}
} // namespace

// Reach a veneer keeps in hand for the block emitted after it: B.cond's ±1MB
// less twice the 64KB of overhang armSetAsmPtr grants a compile.
static constexpr s64 kCondVeneerReach = (1 << 20) - 2 * _64kb;

static bool condBranchReaches(const EeEmitterView& emitter, const void* target)
{
	const s64 d = static_cast<s64>(reinterpret_cast<intptr_t>(target) -
								   reinterpret_cast<intptr_t>(emitter.Cursor()));
	return a64::Instruction::IsValidImmPCOffset(a64::CondBranchType, d >> 2);
}

static s64 condBranchOffset(const EeEmitterView& emitter, const void* target)
{
	return static_cast<s64>(reinterpret_cast<intptr_t>(target) -
							reinterpret_cast<intptr_t>(emitter.Cursor())) >> 2;
}

// A dispatcher exit taken on a condition, hopping through `veneer` when the
// dispatcher is out of imm19 reach and it is not.
static void emitDispatcherCondBranch(const EeEmitterView& emitter, a64::Condition cond, const void* target, const u8* veneer)
{
	if ((s_condVeneerForced || !condBranchReaches(emitter, target)) && veneer && condBranchReaches(emitter, veneer))
	{
		target = veneer;
		COUNT_VENEER_HOP();
	}
	if (condBranchReaches(emitter, target))
	{
		a64::SingleEmissionCheckScope guard(emitter.masm);
		emitter.masm->b(condBranchOffset(emitter, target), cond);
		return;
	}
	armEmitCondBranch(cond, target);
}

// The CBZ form. armEmitCbnz tests the other way, so its fallback is the shape
// this replaces rather than one to call.
static void emitDispatcherCbz(const EeEmitterView& emitter, const a64::Register& reg, const void* target, const u8* veneer)
{
	if ((s_condVeneerForced || !condBranchReaches(emitter, target)) && veneer && condBranchReaches(emitter, veneer))
	{
		target = veneer;
		COUNT_VENEER_HOP();
	}
	if (condBranchReaches(emitter, target))
	{
		a64::SingleEmissionCheckScope guard(emitter.masm);
		emitter.masm->cbz(reg, condBranchOffset(emitter, target));
		return;
	}
	a64::Label taken;
	emitter.masm->Cbnz(reg, &taken);
	armEmitJmp(target);
	emitter.masm->Bind(&taken);
}

// Emitted in front of a block, where nothing can fall into it: the entry
// pointer the block manager links against is taken after this returns.
static void emitCondVeneers()
{
	const u8* here = armGetCurrentCodePointer();
	if (!s_condVeneerForced && s_condVeneerEvent && (here - s_condVeneerEvent) < kCondVeneerReach)
		return;

	s_condVeneerEvent = here;
	armEmitJmp(DispatcherEvent);
	s_condVeneerReg = armGetCurrentCodePointer();
	armEmitJmp(DispatcherReg);

	// armStartBlock aligned the entry to 16 bytes and the veneers displace it;
	// pad the alignment back rather than leave block entries straddling.
	while ((reinterpret_cast<uintptr_t>(armGetCurrentCodePointer()) & 15) != 0)
	{
		a64::SingleEmissionCheckScope guard(armAsm);
		armAsm->nop();
	}
}

// Block-tail cycle update + event check under the delta representation:
// fold the pending block cycles into RECCYCLE with a flag-setting add, then
// branch to DispatcherEvent when the delta is non-negative (an event is
// due). Replaces the old per-exit `ldr nextEventCycle; cmp; b.ge` — no
// memory access on the hot linked path. The adds can't overflow s64: the
// delta stays within the event-scheduling horizon (≤ a frame) and block
// cycles are 16-bit scaled.
static void emitCycleUpdateAndEventCheck()
{
	const u32 cycles = scaleblockcycles_clear(EeChargeForm::AddImm12);
	const EeEmitterView emitter{armAsm, armAsmPtr};
	if (cycles != 0)
		emitter.masm->Adds(RECCYCLE, RECCYCLE, cycles);
	else
		emitter.masm->Cmp(RECCYCLE, 0);
	recEeNoteChargeSiteAt(EeChargeSite::BlockTail, emitter.Cursor());
	emitDispatcherCondBranch(emitter, a64::ge, DispatcherEvent, s_condVeneerEvent);
}

static void EmitBranchReg(EEBranchRegMode mode, u32 call_return_pc, int wbreg, const a64::Register* pin)
{
	const Cop2VfCacheScope vfCacheScope; // fork tail: preserve compile-time cache state
	g_branch = 1;

	// Where the target is. recJR/recJALR park it in an ARM64TYPE_PCWRITEBACK
	// slot, whose allocator home is cpuRegs.pcWriteback — so a delay slot that
	// took the register back has already spilled the value there, and one that
	// left it alone means the register is still the newest copy. Dropping
	// MODE_WRITE tells the flush below to release the slot without emitting
	// that spill.
	const bool parked = wbreg >= 0 && arm64gprs[wbreg].inuse &&
		arm64gprs[wbreg].type == ARM64TYPE_PCWRITEBACK;
	if (parked)
		arm64gprs[wbreg].mode &= ~MODE_WRITE;

	// Flush all GPR/NEON/constant allocations FIRST, while host registers
	// still hold correct guest values. iFlushCall writes back delay slot
	// results (like addiu sp) before the branch target is loaded into w0.
	iFlushCall(FLUSH_EVERYTHING);

	// The tail reads the target wherever it already is — the parked register or
	// w0 — rather than funnelling it through w0 first. Nothing below writes an
	// allocatable register before it's read: the ring and event-check code use
	// x8/x9/x10/x17, all carved out of the allocator pool.
	if (parked)
	{
		// iFlushCall allocates no registers, so the released slot still holds
		// the target.
		pxAssert(!arm64gprs[wbreg].inuse);
	}
	else if (!pin)
	{
		armLoadEERegPtr(a64::w0, &cpuRegs.pcWriteback);
	}
	// The tail flush preserves pins. Callers only pass a pin with no
	// pending const or competing quad after the swapped delay slot.
	const a64::Register target_w = pin ? pin->W() : (parked ? armWRegister(wbreg) : a64::w0);

	// GoemonTlbHack: recJR/recJALR store the raw virtual register target; the
	// JIT dispatches in physical space, so translate it before use. Mirrors
	// recJ/recJAL (compile-time vtlb_V2P via SetBranchImm) and x86
	// recJR/recJALR (vtlb_DynV2P). The V2P lives in SetBranchReg, whose only
	// EE callers are recJR/recJALR, so no other target gets double-translated.
	// The iFlushCall(FLUSH_EVERYTHING) above has already spilled guest state;
	// vtlb_V2P preserves callee-saved x25 (RECCYCLE) per AAPCS64, so the
	// C-call needs no extra save. w0 (== RWARG1) already holds the virtual
	// target and receives the translated paddr. (Call/Return modes are
	// emit-gated off under this hack — see eeCallRetActive.)
	if (EmuConfig.Gamefixes.GoemonTlbHack)
	{
		pxAssert(mode == EEBranchRegMode::Jump);
		armFlushEEClobberedPins(); // lazy-dirty seam: pairs with the reload below
		armAsm->Mov(a64::w0, target_w); // RWARG1: the virtual target in, the paddr out
		armEmitCall((void*)vtlb_V2P);
		// vtlb_V2P writes no guest GPRs but clobbers the caller-saved pins;
		// the DispatcherReg jump below can cache-hit straight into a block.
		armReloadEEClobberedPins();
	}
	const a64::Register& branch_w = EmuConfig.Gamefixes.GoemonTlbHack ? a64::w0 : target_w;

	// Store to cpuRegs.pc
	armAsm->Str(branch_w, armCpuRegMem(&cpuRegs.pc));

	a64::Label unaligned;
#if defined(PS2_EE_CALLRET_CURSOR) && defined(PS2_EE_CALLRET_POST_INDEX)
	if (mode != EEBranchRegMode::Return)
#endif
	{
		armAsm->Tst(branch_w, 3);
		armAsm->B(&unaligned, a64::ne);
	}

	if (mode == EEBranchRegMode::Return)
	{
		// Guest JR-$ra: pop a frame — ALWAYS, before the event check, so an
		// event detour (which still reaches the return target through the
		// dispatcher) leaves the ring balanced. Frame regs survive the event
		// check below: it is flags-only (Adds/Cmp + b.ge).
		armAsm->Ldr(a64::x9, armCpuRegMem(&_cpuRegistersPack.eeCallRetBase));
#ifdef PS2_EE_CALLRET_CURSOR
#ifdef PS2_EE_CALLRET_POST_INDEX
		// Bit 16 is clear throughout the live ring. The post-index sets it
		// only when popping its last frame, so clearing it wraps the cursor.
		armAsm->Ldp(RXSCRATCH, RSCRATCHADDR, a64::MemOperand(a64::x9, 16, a64::PostIndex));
		armAsm->Bic(a64::x9, a64::x9, kEECallRetRingBytes);

		// Every frame has an aligned guest PC, including reset sentinels.
		// A matching PC needs no separate test. A miss checks alignment
		// before committing either the cursor or the guest-cycle charge.
		a64::Label miss, commit_pop;
		if (pin)
			armAsm->Cmp(RWSCRATCH, branch_w);
		else
			armAsm->Cmp(RXSCRATCH, branch_w.X());
		armAsm->B(&miss, a64::ne);
		armAsm->Bind(&commit_pop);
		armAsm->Str(a64::x9, armCpuRegMem(&_cpuRegistersPack.eeCallRetBase));
#else
		armAsm->Ldp(RXSCRATCH, RSCRATCHADDR, a64::MemOperand(a64::x9));
		emitCallRetAdvanceCursor(false);
#endif
#else
		// The Ldrh zero-extends, so this reads the frame at the offset as it
		// stands; only the incremented value below has to be narrowed again.
		armAsm->Ldrh(a64::w10, armCpuRegMem(&_cpuRegistersPack.eeCallRetOff));
		armAsm->Add(a64::x9, a64::x9, a64::x10);
		armAsm->Ldp(RXSCRATCH, RSCRATCHADDR, a64::MemOperand(a64::x9));
		armAsm->Add(a64::w10, a64::w10, 16);
		armAsm->Strh(a64::w10, armCpuRegMem(&_cpuRegistersPack.eeCallRetOff));
#endif

		emitCycleUpdateAndEventCheck();

#if defined(PS2_EE_CALLRET_CURSOR) && defined(PS2_EE_CALLRET_POST_INDEX)
		// Aligned misses use the same RET, preserving the host RAS pop.
		armAsm->Ret(RSCRATCHADDR);
		armAsm->Bind(&miss);
		armAsm->Tst(branch_w, 3);
		armAsm->B(&unaligned, a64::ne);
		armMoveAddressToReg(RSCRATCHADDR, DispatcherReg);
		armAsm->B(&commit_pop);
#else
		// Hit: RET to the frame's landing — paired with the pushing BL, the
		// hardware RAS predicts it. Miss: STILL exit via RET (into the
		// dispatcher) so the hardware RAS pops in lockstep with the software
		// ring — a plain B here would leave the paired RAS entry unpopped
		// and desync every subsequent return (the FEX detail our first
		// sketch got wrong).
		a64::Label miss;
		if (pin)
			armAsm->Cmp(RWSCRATCH, branch_w);
		else
			armAsm->Cmp(RXSCRATCH, branch_w.X());
		armAsm->B(&miss, a64::ne);
		armAsm->Ret(RSCRATCHADDR);

		armAsm->Bind(&miss);
		armMoveAddressToReg(RSCRATCHADDR, DispatcherReg);
		armAsm->Ret(RSCRATCHADDR);
#endif
	}
	else if (mode == EEBranchRegMode::Call)
	{
		// Guest JALR-rd31: push the frame (before the event check — the
		// callee is reached either way and its return must pop this frame),
		// then transfer with a BL through the shared dispatcher. The BL
		// pushes the hardware RAS entry the callee's RET will consume.
		a64::Label landing;
		emitCallRetPush(call_return_pc, &landing);

		emitCycleUpdateAndEventCheck();

#if PS2_EE_DISPATCH_KNOWN_PC
		armEmitCall(emitEeDispatchTarget(branch_w));
#else
		armEmitCall(DispatcherReg);
#endif

		armAsm->Bind(&landing);
		{
			if (!emitEeColdRamTail(call_return_pc))
				emitEeLinkedBranch(call_return_pc);
		}
	}
	else
	{
		// Update the pinned cycle delta and check events (delta >= 0 →
		// DispatcherEvent converts RECCYCLE itself before calling recEventTest).
		emitCycleUpdateAndEventCheck();

#if PS2_EE_DISPATCH_KNOWN_PC
		armEmitJmp(emitEeDispatchTarget(branch_w));
#else
		armEmitJmp(DispatcherReg);
#endif
	}

	armAsm->Bind(&unaligned);
	armAsm->Mov(RWARG1, 1);
	armEmitCall((void*)recError);
}

void SetBranchReg(EEBranchRegMode mode, u32 call_return_pc, int wbreg)
{
	EmitBranchReg(mode, call_return_pc, wbreg, nullptr);
}

#ifdef PS2_EE_PINNED_BRANCH_TARGET
void SetBranchRegFromPin(EEBranchRegMode mode, u32 call_return_pc, const a64::Register& pin)
{
	pxAssert(!EmuConfig.Gamefixes.GoemonTlbHack);
	EmitBranchReg(mode, call_return_pc, -1, &pin);
}
#endif

// =====================================================================================================
//  SL-1: loop-carried residency for self-loop blocks
// =====================================================================================================
// A block whose terminal branch targets its own startpc (the scanner's
// backward-split rule guarantees loop heads are block starts) compiles with a
// register-resident back-edge: the loop-live guest GPRs are allocated once in
// a preheader (dirty-pessimized, loop-pinned), the loop-top label is bound
// after it, and the taken arm of the terminal branch — instead of the normal
// flush + event check + linked-B tail — emits a reconcile back to the loop-top
// allocator state, the cycle/event check (side-exiting to a cold spill stub),
// and a direct B to the loop-top. Dirty values ride host registers across
// iterations; memory becomes current only on the exit arm, the event side
// exit, or across any mid-body C seam (iFlushCall frees caller-saved entries
// coherently, so seams stay correct — they just localize the win away).
//
// The runtime contract at the back-edge → loop-top jump: every snapshot (S0)
// mapping holds its guest value in its host reg; every other allocatable host
// reg is dead; memory is current for every guest reg NOT mapped by S0. The S0
// dirty bits are pessimized (preheader allocates MODE_READ|MODE_WRITE) so the
// body-emitted evictions always write back — a clean S0 bit over a runtime-
// dirty value would lose writes.
//
// External observers never see mid-loop stale memory: reaching any frame
// boundary, savestate, or C code requires leaving through the exit arm (full
// flush), the spill stub (spills S0), or a mid-body seam (iFlushCall).

static bool s_loopResident = false;
static bool s_loopBackedgeEmitted = false;
static u32 s_loopTopPc = 0;
static _arm64gprregs s_loopEntryState[NUM_ARM_GPR_REGS];
// Only one resident-loop top is emitted at a time. Its label stays
// at a fixed address while the current assembler uses it.
static std::optional<a64::Label> s_loopTopLabel;

// Compile-only metadata for this block's existing immutable manual-SMC blob.
// Never points into recRAMCopy, and never survives a cache reset/shutdown.
struct ManualLoopSmcSnapshot
{
	const u8* expected = nullptr;
	const u8* live = nullptr;
	u32 physical_start = 0;
	u32 byte_length = 0;
	bool forced_manual = false;
};
static ManualLoopSmcSnapshot s_manualLoopSmc;
static bool s_loopManualSmc = false;
static bool s_loopSemanticEntryHook = false;

static bool manualLoopSmcEligible()
{
	return s_manualLoopSmc.forced_manual && s_manualLoopSmc.expected &&
		s_manualLoopSmc.live && s_manualLoopSmc.byte_length && !s_loopSemanticEntryHook;
}

// A page query validates only its own page. Prove the entire frozen span is
// ordinary contiguous tracked Main RAM, including a straddling delay slot.
// Only canonical low RAM/KSEG0/KSEG1 instruction aliases are admitted. Also
// prove their current virtual fetch and dispatcher mappings, not only PSM().
// Normal TLB changes must retain the existing Cpu->Clear/backedge contract.
static bool manualLoopSmcSpanTracked(u32 virtual_start, u32 start, u32 bytes, const u8* live)
{
	const u32 alias = virtual_start & 0xe0000000u;
	if ((alias != 0 && alias != 0x80000000u && alias != 0xa0000000u) ||
		(virtual_start & 0x1fffffffu) != start || !vtlb_private::vtlbdata.vmap ||
		!bytes || (bytes & 3) || start >= Ps2MemSize::ExposedRam ||
		bytes > Ps2MemSize::ExposedRam - start || live != &eeMem->Main[start])
		return false;
	for (u32 offset = 0; offset < bytes;)
	{
		const u32 address = start + offset;
		const u32 chunk = std::min<u32>(bytes - offset, 4096 - (address & 4095));
		const u32 vaddr = virtual_start + offset;
		const u32 vend = vaddr + chunk - 1;
		const auto& vm = vtlb_private::vtlbdata.vmap[vaddr >> 12];
		if (mmap_GetRamPageInfo(address) == ProtMode_NotRequired ||
			(const u8*)PSM(address) != live + offset ||
			(const u8*)PSM(address + chunk - 1) != live + offset + chunk - 1 ||
			HWADDR(vaddr) != address || HWADDR(vend) != address + chunk - 1 ||
			GETBLOCK(vaddr & ~3u) != &recRAM[address / 4] ||
			GETBLOCK(vend & ~3u) != &recRAM[(address + chunk - 1) / 4] ||
			vm.isHandler(vaddr) ||
			vm.assumePtr(vaddr) != reinterpret_cast<uptr>(live + offset) ||
			vm.assumePtr(vend) != reinterpret_cast<uptr>(live + offset + chunk - 1))
			return false;
		offset += chunk;
	}
	return true;
}

// Optional existing-latch fixedpoint proof; default compile policy is off.
#include "PollLatchNativeGate.h"
#if PS2_NATIVE_CROSSBLOCK_POLL_LATCH && PS2_NATIVE_POLL_WRITER_PROFILE == 2
#include "PINE.h"
#include "MTVU.h"
namespace NativePoll {
std::uint8_t lease = 0;
#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
static bool requested;
static u64 guards;
static u64 eligibleEntries;
static u64 ineligibleEntries;
alignas(16) static struct {
	u64 skips;
	u64 cycles;
} skipStats;
static_assert(sizeof(skipStats) == 16);
static const char* firstRejection;

void ConfigureTrial(bool enabled)
{
	Invalidate();
	requested = enabled;
	guards = eligibleEntries = ineligibleEntries = 0;
	skipStats = {};
	firstRejection = nullptr;
}

void LogTrial()
{
	Console.WriteLn("IZPS2_EE_POLL requested=%d eligible_entries=%llu generated_guards=%llu skips=%llu "
		"pipeline_precompile=%d ineligible_entries=%llu first_reject=%s skipped_guest_cycles=%llu "
		"vu_aot=%d vu_program_cache=%d",
		requested ? 1 : 0, static_cast<unsigned long long>(eligibleEntries),
		static_cast<unsigned long long>(guards), static_cast<unsigned long long>(skipStats.skips),
		EmuConfig.GS.PrecompilePipelines ? 1 : 0, static_cast<unsigned long long>(ineligibleEntries),
		firstRejection ? firstRejection : "none", static_cast<unsigned long long>(skipStats.cycles),
		EmuConfig.Cpu.Recompiler.EnableVUAot ? 1 : 0,
		EmuConfig.Cpu.Recompiler.EnableVUProgramCache ? 1 : 0);
}

void GetTrialCounters(u64& skips, u64& cycles)
{
	skips = skipStats.skips;
	cycles = skipStats.cycles;
}
#endif
static const char* GetEntryRejection() noexcept
{
	// A guard runs between EE events without callbacks. CDVD and DEV9
	// lifecycle transitions invalidate the lease.
	if (VMManager::GetState() != VMState::Running)
		return "vm_state";
#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
	if (CHECK_FASTMEM)
		return "fastmem";
	if (EmuConfig.Speedhacks.vuThread || vu1Thread.IsOpen())
		return "mtvu";
#endif
	if (IopThread::GetMode() != IopThreadMode::Off || EmuConfig.Speedhacks.iopThread)
		return "iop";
	if (EmuConfig.EnablePINE || PINEServer::IsInitialized())
		return "pine";
	if (EmuConfig.DEV9.EthEnable || EmuConfig.DEV9.HddEnable || Dev9HasAsyncWriter())
		return "dev9";
	if (EmuConfig.GS.Renderer != GSRendererType::VK || EmuConfig.GS.SWExtraThreads != 0)
		return "renderer";
#ifndef IZPS2_SWITCH2_NATIVE_POLL_KEEP_TFX
	if (EmuConfig.GS.PrecompilePipelines)
		return "pipeline_precompile";
#endif
	if (EmuConfig.Cpu.Recompiler.EnableVUAot)
		return "vu_aot";
#ifndef IZPS2_SWITCH2_NATIVE_POLL_KEEP_VU_CACHE
	if (EmuConfig.Cpu.Recompiler.EnableVUProgramCache)
		return "vu_cache";
#endif
	return ActualDiscIsFlatISO() ? nullptr : "disc";
}

#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
static void StartTrialEntry()
{
	Invalidate();
	if (!requested)
		return;
	if (const char* rejection = GetEntryRejection())
	{
		if (!firstRejection)
		{
			firstRejection = rejection;
			Console.WriteLn("IZPS2_EE_POLL_ENTRY eligible=0 reason=%s", rejection);
		}
		++ineligibleEntries;
		return;
	}
	if (eligibleEntries++ == 0)
		Console.WriteLn("IZPS2_EE_POLL_ENTRY eligible=1 reason=none");
	lease = 1;
}
#endif
}
#endif
// Compare exactly 64 readable bytes. The two pointers advance by 64 bytes.
// q0-q7 are free at external entries or after FLUSH_EVERYTHING. q29 is reserved.
[[maybe_unused]] static void emitEeSnapshotDifference64(bool initialize)
{
	const a64::VRegister lhs0(0, 128), lhs1(1, 128), lhs2(2, 128), lhs3(3, 128);
	const a64::VRegister rhs4(4, 128), rhs5(5, 128), rhs6(6, 128), rhs7(7, 128);
	const a64::VRegister accum(29, 128);
	armAsm->Ld1(lhs0.V16B(), lhs1.V16B(), lhs2.V16B(), lhs3.V16B(),
		a64::MemOperand(RSCRATCHADDR, 64, a64::PostIndex));
	armAsm->Ld1(rhs4.V16B(), rhs5.V16B(), rhs6.V16B(), rhs7.V16B(),
		a64::MemOperand(RXSCRATCH, 64, a64::PostIndex));
	armAsm->Eor(lhs0.V16B(), lhs0.V16B(), rhs4.V16B());
	armAsm->Eor(lhs1.V16B(), lhs1.V16B(), rhs5.V16B());
	armAsm->Eor(lhs2.V16B(), lhs2.V16B(), rhs6.V16B());
	armAsm->Eor(lhs3.V16B(), lhs3.V16B(), rhs7.V16B());
	armAsm->Orr(lhs0.V16B(), lhs0.V16B(), lhs1.V16B());
	armAsm->Orr(lhs2.V16B(), lhs2.V16B(), lhs3.V16B());
	if (initialize)
		armAsm->Orr(accum.V16B(), lhs0.V16B(), lhs2.V16B());
	else
	{
		armAsm->Orr(lhs0.V16B(), lhs0.V16B(), lhs2.V16B());
		armAsm->Orr(accum.V16B(), accum.V16B(), lhs0.V16B());
	}
}

// Compare only the remaining readable bytes. A mismatch remains NE through
// the CCMP chain. The caller supplies two bases and dead x2/x3/x9/x10.
static void emitEeSnapshotDifferenceTail(u32 rem, a64::Label* mismatch)
{
	bool first = true;
	while (rem >= 16)
	{
		armAsm->Ldp(a64::x9, a64::x10, a64::MemOperand(RSCRATCHADDR, 16, a64::PostIndex));
		armAsm->Ldp(a64::x2, a64::x3, a64::MemOperand(RXSCRATCH, 16, a64::PostIndex));
		if (first)
			armAsm->Cmp(a64::x9, a64::x2);
		else
			armAsm->Ccmp(a64::x9, a64::x2, a64::NoFlag, a64::eq);
		armAsm->Ccmp(a64::x10, a64::x3, a64::NoFlag, a64::eq);
		first = false;
		rem -= 16;
	}
	if (rem >= 8)
	{
		armAsm->Ldr(a64::x9, a64::MemOperand(RSCRATCHADDR, 8, a64::PostIndex));
		armAsm->Ldr(a64::x2, a64::MemOperand(RXSCRATCH, 8, a64::PostIndex));
		if (first)
			armAsm->Cmp(a64::x9, a64::x2);
		else
			armAsm->Ccmp(a64::x9, a64::x2, a64::NoFlag, a64::eq);
		first = false;
		rem -= 8;
	}
	if (rem)
	{
		armAsm->Ldr(a64::w9, a64::MemOperand(RSCRATCHADDR));
		armAsm->Ldr(a64::w2, a64::MemOperand(RXSCRATCH));
		if (first)
			armAsm->Cmp(a64::w9, a64::w2);
		else
			armAsm->Ccmp(a64::w9, a64::w2, a64::NoFlag, a64::eq);
	}
	armAsm->B(mismatch, a64::ne);
}

#include "PollLatchEmit.h"
#include "EeSmcVectorLiteral.h"
#include "EeSmcVectorTail.h"
#include "EeSmcEarlyExit.h"

static void emitLoopManualSmcCheck(a64::Label* mismatch)
{
	pxAssert(manualLoopSmcEligible());
	const u32 bytes = s_manualLoopSmc.byte_length;
	if ((bytes == 28 || bytes == 32) && !armAddressRecorder)
	{
		// Reconciliation has flushed guest NEON state. Two 16-byte
		// reads cover the span exactly, overlapping its middle for 28.
		// The other operands are immutable bytes, stored as literals
		// in this block instead of rematerializing a second pointer.
		// The live span stays at the same address for the lifetime of this block.
		armAsm->Ldr(RSCRATCHADDR, reinterpret_cast<uptr>(s_manualLoopSmc.live));
		const a64::VRegister first(0, 128), last(1, 128), frozen(2, 128);
		const auto emit_difference = [&](const a64::VRegister& value, u32 offset) {
			u64 lo, hi;
			const u8* expected = s_manualLoopSmc.expected + offset;
			std::memcpy(&lo, expected, sizeof(lo));
			std::memcpy(&hi, expected + sizeof(lo), sizeof(hi));
			armAsm->Ldr(value, a64::MemOperand(RSCRATCHADDR, offset));
			armAsm->Ldr(frozen, hi, lo);
			armAsm->Eor(value.V16B(), value.V16B(), frozen.V16B());
		};
		emit_difference(first, 0);
		emit_difference(last, bytes - 16);
		armAsm->Orr(first.V16B(), first.V16B(), last.V16B());
		armAsm->Umaxv(first.S(), first.V4S());
		armAsm->Fmov(a64::w9, first.S());
		armAsm->Cbnz(a64::w9, mismatch);
		return;
	}

	armMoveAddressToReg(RSCRATCHADDR, s_manualLoopSmc.live);
	armMoveAddressToReg(RXSCRATCH, s_manualLoopSmc.expected);
	emitEeSnapshotDifferenceTail(s_manualLoopSmc.byte_length, mismatch);
}

#ifdef PCSX2_RECOMPILER_TESTS
[[gnu::used]] static std::unordered_set<u32> s_loopResidentBlocks;
bool recEeBlockIsLoopResident(u32 pc_query)
{
	return s_loopResidentBlocks.find(HWADDR(pc_query)) != s_loopResidentBlocks.end();
}

// SL-03 introspection: the continuation sites a compiled block actually FORMED,
// keyed by block startpc.
//
// Recorded at EMISSION time, not scan time, and the distinction is the whole
// point: the scanner can record a site that the branch handler then declines —
// it does exactly that when the delay slot would itself end the block — and only
// the sites that survive into emission shape the code. A scan-time list reports
// both alike and so cannot tell a formed superblock from a refused one.
[[gnu::used]] static std::unordered_map<u32, std::vector<u32>> s_blockContSites;
[[gnu::used]] static std::vector<u32> s_curBlockContSites;

// How many continuation sites the compiled block at pc_query formed (0 = none,
// or no such block). Fills `out` with their guest BRANCH pcs, up to out_len.
u32 recEeBlockContinuationSites(u32 pc_query, u32* out, u32 out_len)
{
	const auto it = s_blockContSites.find(HWADDR(pc_query));
	if (it == s_blockContSites.end())
		return 0;
	const std::vector<u32>& v = it->second;
	if (out)
	{
		const u32 n = std::min<u32>(out_len, static_cast<u32>(v.size()));
		for (u32 i = 0; i < n; i++)
			out[i] = v[i];
	}
	return static_cast<u32>(v.size());
}
#endif

// Emit the state transform "compile-state S1 → loop-top state S0" at the
// back-edge. Phase A makes memory current for everything S0 doesn't carry: VF
// compile cache, body-created constants (materialized BEFORE phase B so its
// reloads see them), and every allocator entry that is not exactly an S0 pin
// sitting in its S0 slot (misplaced pin guests route through memory — no
// move-cycle handling needed). Phase B reloads S0 pins the body displaced.
static void _eeLoopReconcileBackedge()
{
	cop2VfCacheFlush();
	_flushConstRegs(false);

	for (int i = 0; i < NUM_ARM_GPR_REGS; i++)
	{
		if (!arm64gprs[i].inuse)
			continue;
		const _arm64gprregs& want = s_loopEntryState[i];
		if (want.inuse && arm64gprs[i].type == want.type && arm64gprs[i].reg == want.reg)
			continue; // S0 pin riding through — stays resident and dirty
		_freeArm64GPR(i); // writeback-if-dirty + free
	}

	// S0 carries no NEON entries: write back dirty quads so memory is current.
	// The emitted loop-top code loads on demand; a residual clean value being
	// clobbered by a later alloc is harmless.
	_flushNEONregs();

	for (int i = 0; i < NUM_ARM_GPR_REGS; i++)
	{
		const _arm64gprregs& want = s_loopEntryState[i];
		if (!want.inuse)
			continue;
		if (arm64gprs[i].inuse && arm64gprs[i].type == want.type && arm64gprs[i].reg == want.reg)
			continue;
		pxAssert(!arm64gprs[i].inuse); // phase A vacated every mismatch
		armLoadEERegPtrRaw(armXRegister(i), &cpuRegs.GPR.r[want.reg].UD[0]);
	}
}

#if PS2_NATIVE_CROSSBLOCK_POLL_LATCH && PS2_NATIVE_POLL_WRITER_PROFILE == 2 && defined(IZPS2_SWITCH2_NATIVE_POLL_TRIAL)
#include "EeByteCopyBurst.h"
extern "C" __attribute__((visibility("hidden"))) bool ps2_patch_has_active_dynamic_patches();

struct EeByteLoopHosts
{
    a64::Register source, destination, counter, stop, value;
};

static std::optional<EeByteLoopHosts> getEeByteLoop()
{
    if (!NativePoll::requested || !s_loopManualSmc || s_manualLoopSmc.byte_length != 28 ||
        armAddressRecorder || EmuConfig.Trace.Enabled || EmuConfig.Gamefixes.GoemonTlbHack ||
        !CBreakPoints::GetBreakpoints(BREAKPOINT_EE, true).empty() || CBreakPoints::GetNumMemchecks() ||
        (EmuConfig.EnablePatches && ps2_patch_has_active_dynamic_patches()) ||
        Ps2MemSize::ExposedRam < 4096 || !std::has_single_bit(Ps2MemSize::ExposedRam))
        return std::nullopt;

    std::array<u32,7> words;
    std::memcpy(words.data(), s_manualLoopSmc.expected, sizeof(words));
    auto gprs = DecodeEeByteCopy(words);
    if (!gprs)
        gprs = DecodeEeByteFill(words);
    if (!gprs)
        return std::nullopt;

    const auto host = [](unsigned guest) {
        if (const auto* pin = armEEPinForGPR(guest))
            return *pin;
        for (int i = 0; i < NUM_ARM_GPR_REGS; ++i)
        {
            const auto& state = s_loopEntryState[i];
            if (state.inuse && state.type == ARM64TYPE_GPR && state.reg == static_cast<int>(guest))
                return armXRegister(i);
        }
        return a64::NoReg;
    };
    // A missing source denotes a fill with an invariant value register.
    EeByteLoopHosts regs{gprs->source ? host(gprs->source) : a64::NoReg,
        host(gprs->destination), host(gprs->counter), host(gprs->stop), host(gprs->value)};
    if ((gprs->source && !regs.source.IsValid()) || !regs.destination.IsValid() || !regs.counter.IsValid() ||
        !regs.stop.IsValid() || !regs.value.IsValid())
        return std::nullopt;
    return regs;
}

static void emitEeByteLoop(const EeByteLoopHosts& r)
{
    using namespace vixl::aarch64;
    Label scalar, at_or_after_code, code_safe, budget_ready, next_packet;
    const bool fill = !r.source.IsValid();
    constexpr u32 wideBytes = 32;
    const u32 count = fill ? wideBytes : 8;
    const u32 ram = Ps2MemSize::ExposedRam;

    // This follows the exact 28-byte check, which preserves the charge
    // in x8. No callbacks run between the RAM checks and the last packet.
    armMoveAddressToReg(x17, &NativePoll::lease);
    armAsm->Ldrb(w9, MemOperand(x17));
    armAsm->Cbz(w9, &scalar);
    armMoveAddressToReg(x17, &IopThread::s_mode);
    armAsm->Ldrb(w9, MemOperand(x17));
    armAsm->Cbnz(w9, &scalar);
#ifdef PCSX2_RECOMPILER_TESTS
    armMoveAddressToReg(x17, &g_eeHarnessActive);
    armAsm->Ldrb(w9, MemOperand(x17));
    armAsm->Cbnz(w9, &scalar);
#endif

    armAsm->Sxtw(x9, r.stop.W());
    armAsm->Cmp(x9, r.stop);
    armAsm->B(&scalar, ne);
    armAsm->Sub(w9, r.counter.W(), r.stop.W());
    armAsm->Cmp(w9, count);
    armAsm->B(&scalar, ls);

    // A store is equivalent to eight successive byte stores even if
    // packets overlap, provided a packet cannot see its own writes.
    if (!fill)
    {
        armAsm->Sub(w9, r.destination.W(), r.source.W());
        armAsm->Cmp(w9, count);
        armAsm->B(&scalar, lo);
    }

    // Resolve the needed direct mappings once. The larger in-page offset
    // determines how many bytes can be written before a mapping changes.
    armMoveAddressToReg(x17, &vtlb_private::vtlbdata.vmap);
    armAsm->Ldr(x17, MemOperand(x17));
    armAsm->Cbz(x17, &scalar);
    armMoveAddressToReg(x10, &eeMem->Main[0]);
    for (const auto& address : {r.source, r.destination})
    {
        if (!address.IsValid())
            continue;
        armAsm->Tst(address.W(), ~(ram - 1));
        armAsm->B(&scalar, ne);
        armAsm->Lsr(w9, address.W(), vtlb_private::VTLB_PAGE_BITS);
        armAsm->Ldr(x9, MemOperand(x17, x9, LSL, 3));
        armAsm->Cmp(x9, x10);
        armAsm->B(&scalar, ne);
    }
    // This loop admits only direct RAM inside the current destination
    // page. Consult the write plane before using the direct packet stores.
    if (eeWriteRoutingActive())
    {
        armAsm->Lsr(w9, r.destination.W(), vtlb_private::VTLB_PAGE_BITS);
        armAsm->Ldr(x9, MemOperand(RFASTMEMBASE, x9, LSL, 3));
        armAsm->Tbnz(x9, EeSoftmemIndirectBit, &scalar);
    }
    armAsm->And(w17, r.destination.W(), vtlb_private::VTLB_PAGE_MASK);
    if (!fill)
    {
        armAsm->And(w9, r.source.W(), vtlb_private::VTLB_PAGE_MASK);
        armAsm->Cmp(w17, w9);
        armAsm->Csel(w17, w17, w9, hi);
    }

    armAsm->Neg(w17, w17);
    armAsm->Add(w17, w17, vtlb_private::VTLB_PAGE_SIZE);

    // One byte must remain to take the guest's ordinary loop exit.
    const auto limit_bytes = [&] {
        armAsm->Cmp(w17, w9);
        armAsm->Csel(w17, w17, w9, lo);
    };
    armAsm->Sub(w9, r.counter.W(), r.stop.W());
    armAsm->Sub(w9, w9, 1);
    limit_bytes();

    // If the target precedes the code, end the run at the frozen span.
    // If the target is already in the code, fall back to the scalar path.
    armAsm->Subs(w9, r.destination.W(), s_manualLoopSmc.physical_start);
    armAsm->B(&at_or_after_code, hs);
    armAsm->Neg(w9, w9);
    limit_bytes();
    armAsm->B(&code_safe);
    armAsm->Bind(&at_or_after_code);
    armAsm->Cmp(w9, s_manualLoopSmc.byte_length);
    armAsm->B(&scalar, lo);
    armAsm->Bind(&code_safe);

    // A negative delta is guaranteed by the original scalar backedge.
    // The next event must remain strictly in the future after the run.
    // Use the live patchable charge and divide before rounding to a packet.
    armAsm->Cbz(x8, &budget_ready);
    armAsm->Mvn(x9, RECCYCLE);
    armAsm->Udiv(x9, x9, x8);
    armAsm->Cmp(x17, x9);
    armAsm->Csel(x17, x17, x9, lo);
    armAsm->Bind(&budget_ready);
    armAsm->Ands(w17, w17, ~(count - 1));
    armAsm->B(&scalar, eq);

    armAsm->Madd(RECCYCLE, x8, x17, RECCYCLE);
    if (!fill)
        armAsm->Add(x8, x10, Operand(r.source.W(), UXTW));
    armAsm->Add(x10, x10, Operand(r.destination.W(), UXTW));
    if (!fill)
        armAsm->Add(r.source.W(), r.source.W(), w17);
    armAsm->Add(r.destination.W(), r.destination.W(), w17);
    armAsm->Sub(r.counter.W(), r.counter.W(), w17);
    armAsm->Sxtw(r.counter, r.counter.W());

    // Reconciliation freed q0. SB observes only the low byte of value.
    // Keep that register intact because the guest loop never writes it.
    if (fill)
    {
        armAsm->Dup(q0.V16B(), r.value.W());
        armAsm->Bind(&next_packet);
        armAsm->Stp(q0, q0, MemOperand(x10, wideBytes, PostIndex));
        armAsm->Subs(w17, w17, wideBytes);
        armAsm->B(&next_packet, ne);
        armAsm->Bind(&scalar);
        return;
    }

    // q0/q1 are dead here: loop reconciliation committed and cleared
    // every NEON allocation. Widen only when a packet cannot read bytes
    // it would have overwritten during earlier guest iterations.
    Label narrow, wide, lastWide, packetsDone;
    armAsm->Cmp(w17, wideBytes);
    armAsm->B(&narrow, lo);
    armAsm->Sub(x9, x10, x8);
    armAsm->Cmp(x9, wideBytes);
    armAsm->B(&narrow, lo);
    armAsm->Bind(&wide);
    armAsm->Ldp(q0, q1, MemOperand(x8, wideBytes, PostIndex));
    armAsm->Stp(q0, q1, MemOperand(x10, wideBytes, PostIndex));
    armAsm->Sub(w17, w17, wideBytes);
    armAsm->Cmp(w17, wideBytes);
    armAsm->B(&wide, hs);
    armAsm->Cbz(w17, &lastWide);

    armAsm->Bind(&narrow);
    armAsm->Bind(&next_packet);
    armAsm->Ldr(x9, MemOperand(x8, count, PostIndex));
    armAsm->Str(x9, MemOperand(x10, count, PostIndex));
    armAsm->Subs(w17, w17, count);
    armAsm->B(&next_packet, ne);
    armAsm->Lsr(r.value, x9, 56);
    armAsm->B(&packetsDone);

    armAsm->Bind(&lastWide);
    armAsm->Umov(r.value.W(), q1.V16B(), 15);
    armAsm->Bind(&packetsDone);
    armAsm->Bind(&scalar);
}
#endif

// Back-edge tail — replaces SetBranchImm on the resident self-loop taken arm.
static void SetBranchBackedge()
{
	const Cop2VfCacheScope vfCacheScope; // fork tail: preserve compile-time cache state
	g_branch = 1;

	_eeLoopReconcileBackedge();

	// Cycle update + event check, side-exiting to a local cold stub instead of
	// DispatcherEvent (the stub owes the spill + pc store first).
	a64::Label spill;
#if PS2_NATIVE_CROSSBLOCK_POLL_LATCH && PS2_NATIVE_POLL_WRITER_PROFILE == 2 && defined(IZPS2_SWITCH2_NATIVE_POLL_TRIAL)
	const auto byteLoop = getEeByteLoop();
	if (byteLoop)
	{
		// The same runtime charge serves both the ordinary iteration and
		// the burst. Governor patching cannot make the two drift apart.
		const u32 cycles = scaleblockcycles_clear(EeChargeForm::MovzImm16);
		armAsm->Mov(a64::w8, cycles);
		recEeNoteChargeSite(EeChargeSite::LoopBackedge);
		armAsm->Adds(RECCYCLE, RECCYCLE, a64::x8);
	}
	else
#endif
	{
		const u32 cycles = scaleblockcycles_clear(EeChargeForm::AddImm12);
		if (cycles != 0)
			armAsm->Adds(RECCYCLE, RECCYCLE, cycles);
		else
			armAsm->Cmp(RECCYCLE, 0);
		recEeNoteChargeSite(EeChargeSite::LoopBackedge);
	}
	armAsm->B(&spill, a64::ge);

	a64::Label smc_mismatch;
	if (s_loopManualSmc)
		emitLoopManualSmcCheck(&smc_mismatch);
#if PS2_NATIVE_CROSSBLOCK_POLL_LATCH && PS2_NATIVE_POLL_WRITER_PROFILE == 2 && defined(IZPS2_SWITCH2_NATIVE_POLL_TRIAL)
	if (byteLoop)
		emitEeByteLoop(*byteLoop);
#endif

	// The resident back-edge. Registered on the block so recClear can repoint
	// it to the spill stub (see Arm64BaseBlocks::Remove) — the entry redirect
	// alone can't catch an internal branch, and a cleared self-loop must not
	// keep executing stale code until the next event.
	u8* backedge_site;
	{
		a64::SingleEmissionCheckScope guard(armAsm);
		backedge_site = armGetCurrentCodePointer();
		armAsm->b(&*s_loopTopLabel);
	}

	// Cold spill stub: runtime state here is exactly S0 (the reconcile ran on
	// the fall-through into the event check). Write back the pinned set (S0
	// pessimizes every pin dirty), publish the loop-top pc, and hand off to
	// DispatcherEvent. Re-entry after the event comes through the block head,
	// whose preheader reloads the pins.
	armAsm->Bind(&spill);
	s_pCurBlockEx->backedge_site = reinterpret_cast<uptr>(backedge_site);
	s_pCurBlockEx->backedge_stub = reinterpret_cast<uptr>(armGetCurrentCodePointer());
	armAsm->bti(a64::EmitBTI_jc);
	for (int i = 0; i < NUM_ARM_GPR_REGS; i++)
	{
		const _arm64gprregs& want = s_loopEntryState[i];
		if (want.inuse && (want.mode & MODE_WRITE))
			armStoreEERegPtrRaw(armXRegister(i), &cpuRegs.GPR.r[want.reg].UD[0]);
	}
	armAsm->Mov(RWSCRATCH, s_loopTopPc);
	armAsm->Str(RWSCRATCH, armCpuRegMem(&cpuRegs.pc));
	// This stub is also the registered recClear target. A clear can happen
	// in a C/helper seam before the next event is due. Use the CURRENT delta,
	// reloaded by that seam, exactly as the ordinary block tail does; forcing
	// an early shared event test changes guest scheduling (+201 in the old
	// actual real-clear fixture). S0/PC writes above do not alter RECCYCLE.
	armAsm->Cmp(RECCYCLE, 0);
	armEmitCondBranch(a64::ge, DispatcherEvent);
	armEmitJmp(DispatcherReg);

	if (s_loopManualSmc)
	{
		armAsm->Bind(&smc_mismatch);
		armAsm->bti(a64::EmitBTI_jc);
		// Runtime state is S0 after the same reconcile as the event stub.
		for (int i = 0; i < NUM_ARM_GPR_REGS; i++)
		{
			const _arm64gprregs& want = s_loopEntryState[i];
			if (want.inuse && (want.mode & MODE_WRITE))
				armStoreEERegPtrRaw(armXRegister(i), &cpuRegs.GPR.r[want.reg].UD[0]);
		}
		// S0 is distinct from the nine dedicated lazy-dirty EE pins.
		// Canonicalize all of those only on this cold C-call seam.
		armFlushEEGPRPins();
		armAsm->Mov(RWSCRATCH, s_loopTopPc);
		armAsm->Str(RWSCRATCH, armCpuRegMem(&cpuRegs.pc));
		armAsm->Mov(a64::w0, s_manualLoopSmc.physical_start);
		armAsm->Mov(a64::w1, s_manualLoopSmc.byte_length / 4);
		armEmitJmp(DispatchBlockDiscard);
	}

	s_loopBackedgeEmitted = true;
#if defined(PCSX2_RECOMPILER_TESTS) && PS2_EE_TEST_BLOCK_METADATA
	s_loopResidentBlocks.insert(HWADDR(s_loopTopPc));
#endif
}

#ifdef __SWITCH2__
static bool s_ee_pc_literal_trial;

void recConfigureEePcLiteralTrial(bool enabled)
{
	s_ee_pc_literal_trial = enabled;
}
#endif

static __fi void emitEeKnownPc(u32 guest_pc)
{
	// Keep one-insn immediates in the instruction stream. Only a two-insn
	// guest address benefits from using the block's existing literal pool.
#ifdef __SWITCH2__
	if (s_ee_pc_literal_trial && !armAddressRecorder &&
#if PS2_EE_SOFTMEM_COLD_OOL && defined(PS2_HOST_CODE_BRIDGE)
		!armSoftmemColdActive() &&
#endif
		a64::MacroAssembler::MoveImmediateHelper(nullptr, RWSCRATCH, guest_pc) > 1)
		armAsm->Ldr(RWSCRATCH, static_cast<u64>(guest_pc));
	else
#endif
		armAsm->Mov(RWSCRATCH, guest_pc);
	armAsm->Str(RWSCRATCH, armCpuRegMem(&cpuRegs.pc));
}

void SetBranchImm(u32 imm)
{
	if (s_loopResident && !s_loopBackedgeEmitted && imm == s_loopTopPc)
	{
		SetBranchBackedge();
		return;
	}

	const Cop2VfCacheScope vfCacheScope; // fork tail: preserve compile-time cache state
	g_branch = 1;

	// Zero is an ordinary target here, not a sign that a caller forgot to
	// compute one. A call to an unresolved weak symbol links as `jal 0` behind
	// a null test that always skips it — PS2SDK's libc glue ships four such
	// sites, so any homebrew built against it carries the shape — and SL-03
	// continuation compiles that skipped path, so the emitter meets the zero
	// target even though nothing ever jumps to it. Should something jump there
	// anyway, address 0 resolves like any other: a block in RAM page 0, or the
	// unmapped-page handler. (x86's iR5900.cpp asserts here and would abort on
	// the same ELF.)
	emitEeKnownPc(imm);

	iFlushCall(FLUSH_EVERYTHING);

	// WaitLoop speedhack: when the block scanner detected this block as a
	// pure-nop spin branching back to itself (s_nBlockFF), fast-forward the
	// delta to max(delta, 0) — i.e. cycle to max(cycle, nextEventCycle) —
	// and jump straight to DispatcherEvent. Saves the dozens of iterations
	// the loop would otherwise burn waiting for an event to fire. Matches
	// the x86 path in iR5900.cpp:iBranchTest under Speedhacks.WaitLoop.
	if (EmuConfig.Speedhacks.WaitLoop && s_nBlockFF && imm == s_branchTo)
	{
		const u32 cycles = scaleblockcycles_clear(EeChargeForm::AddImm12);
		if (cycles != 0)
			armAsm->Add(RECCYCLE, RECCYCLE, cycles);
		recEeNoteChargeSite(EeChargeSite::WaitLoopFastFwd);
		armAsm->Cmp(RECCYCLE, 0);
		armAsm->Csel(RECCYCLE, RECCYCLE, a64::xzr, a64::gt);
		armEmitJmp(DispatcherEvent);
		return;
	}

	// Update the pinned cycle delta and check events ONCE.
	emitCycleUpdateAndEventCheck();
#if PS2_NATIVE_CROSSBLOCK_POLL_LATCH
	// All original branch/delay effects and the due-event exit precede this.
	emitGuardedPollLatch(imm);
#endif

	if (emitEeColdRamTail(imm))
		return;

	// Block linking: emit a single B as the patch site. Initially routed
	// through JITCompile via recBlocks.Link(); once the target block is
	// compiled, recBlocks.New() rewrites this B's imm26 to branch to the
	// target's fnptr directly, bypassing the dispatcher.
	{
		emitEeLinkedBranch(imm);
	}
}

// recJAL tail: SetBranchImm's shape plus a call-ret frame push, a BL-form
// link to the callee (hardware RAS push), and a linked landing at the
// return PC. See the call-ret ring comment block above SetBranchReg.
void SetBranchImmCall(u32 imm, u32 return_pc)
{
	// A WaitLoop-FF-shaped block wants the fast-forward path, and a
	// self-spinning JAL never returns anyway — keep the plain tail.
	if (EmuConfig.Speedhacks.WaitLoop && s_nBlockFF && imm == s_branchTo)
	{
		SetBranchImm(imm);
		return;
	}

	const Cop2VfCacheScope vfCacheScope; // fork tail: preserve compile-time cache state
	g_branch = 1;
	// A zero target is legal and reachable here — see SetBranchImm. This is the
	// site the PS2SDK `jal 0` lands on.

	emitEeKnownPc(imm);

	iFlushCall(FLUSH_EVERYTHING);

	// Push before the event check: an event detour still reaches the callee
	// (via DispatcherEvent → DispatcherReg), whose eventual return must find
	// this frame. Only the hardware-RAS pairing is lost on that rare path.
	a64::Label landing;
	emitCallRetPush(return_pc, &landing);

	emitCycleUpdateAndEventCheck();

	// BL-form link site: patched toward JITCompile now, re-patched to the
	// callee block when it compiles — always keeping the BL opcode so the
	// RAS entry gets pushed no matter where the site currently points.
	{
		if (!emitEeColdRamTail(imm, true))
			emitEeLinkedBranch(imm, true);
	}

	// The RET landing: a plain linked B to the return-PC block (the one
	// conscious deviation from FEX/MAMBO-X64 — they translate through the
	// call so the continuation falls through; our block formation ends at
	// the call, and one predicted direct B costs ~a cycle on the hit path).
	armAsm->Bind(&landing);
	{
		if (!emitEeColdRamTail(return_pc))
			emitEeLinkedBranch(return_pc);
	}
}

// =====================================================================================================
//  Block state save/restore for delay slots
// =====================================================================================================

// The full per-fork compile state: everything a branch arm needs restored to
// re-emit from the fork point. Shared by the delay-slot fork
// (SaveBranchState/LoadBranchState) and the superblock side-exit snapshots.
struct BranchCompileState
{
	_arm64gprregs gprs[NUM_ARM_GPR_REGS];
	_arm64neonregs neon[NUM_ARM_NEON_REGS];
	GPR_reg64 constRegs[32];
	u32 hasConstReg, flushedConstReg;
	u32 blockCycles;
	EEINST* instInfo;
	Cop2VfCacheState vfCache;
	bool clampConstsValid; // SL-13: q25/q26 broadcast validity at the fork point

	void capture()
	{
		vfCache = cop2VfCacheGetState();
		clampConstsValid = cop2ClampConstsValid();
		blockCycles = s_nBlockCycles;
		memcpy(constRegs, g_cpuConstRegs, sizeof(g_cpuConstRegs));
		hasConstReg = g_cpuHasConstReg;
		flushedConstReg = g_cpuFlushedConstReg;
		instInfo = g_pCurInstInfo;
		memcpy(gprs, arm64gprs, sizeof(arm64gprs));
		memcpy(neon, arm64neon, sizeof(arm64neon));
	}

	void restore() const
	{
		cop2VfCacheSetState(vfCache);
		cop2ClampConstsSetValid(clampConstsValid);
		s_nBlockCycles = blockCycles;
		memcpy(g_cpuConstRegs, constRegs, sizeof(g_cpuConstRegs));
		g_cpuHasConstReg = hasConstReg;
		g_cpuFlushedConstReg = flushedConstReg;
		g_pCurInstInfo = instInfo;
		memcpy(arm64gprs, gprs, sizeof(arm64gprs));
		memcpy(arm64neon, neon, sizeof(arm64neon));
	}
};

static BranchCompileState s_savedBranchState;

void SaveBranchState()
{
	s_savedBranchState.capture();
}

void LoadBranchState()
{
	s_savedBranchState.restore();
}

// =====================================================================================================
//  SL-03: superblocks — conditional-fallthrough continuation
// =====================================================================================================
// The scanner records forward conditional branches (BEQ/BNE/BLEZ/BGTZ/BLTZ/
// BGEZ; non-likely, non-link) as continuation sites and keeps scanning at the
// fallthrough, so the not-taken path compiles as one straight line: no pc
// store, no event check, no linked-B, no next-head reload — register/const
// residency rides through the former boundary. The taken arm becomes a cold
// side exit outlined after the block tail; it snapshots the compile state at
// the branch, and its emission restores that snapshot, compiles the taken-path
// delay slot (unless TrySwapDelaySlot already hoisted it), and ends with the
// normal SetBranchImm tail. Analysis stays exactly as conservative as today's
// block ends: the liveness backward pass merges all-live at each site (the
// taken path leaves the block there), and the COP2 deferred-commit passes run
// per segment delimited at sites. Event-check coarsening equals today's
// straight-line blocks (one check per exit, range capped by the 4K page).

static constexpr int kMaxContSites = 8;
static constexpr u32 kMaxSuperblockInsns = 128;

static u32 s_contSitePcs[kMaxContSites]; // ascending (scan order)
static int s_numContSites = 0;

struct SuperblockSideExit
{
	// The fixed exit array never moves while VIXL references the label.
	std::optional<a64::Label> label;
	u32 branchTo;
	u32 dsPc;
	bool needDs;
	BranchCompileState state;
};
static SuperblockSideExit s_sideExits[kMaxContSites];
static int s_numSideExits = 0;

bool recSuperblockIsContSite(u32 branch_pc)
{
	for (int i = 0; i < s_numContSites; i++)
		if (s_contSitePcs[i] == branch_pc)
			return true;
	return false;
}

// Snapshot the compile state for the taken path and hand back the label the
// handler's inverted condition branches to. Call after _eeFlushAllDirty and
// (for a swapped slot) after the delay-slot emission; the compare itself is a
// pure post-flush read and may be emitted after the snapshot. pc points at the
// delay slot here.
a64::Label* recSuperblockAddSideExit(u32 branch_target, bool need_delay_slot)
{
	pxAssert(s_numSideExits < kMaxContSites);
	SuperblockSideExit& x = s_sideExits[s_numSideExits++];
	x.label.emplace();
	x.branchTo = branch_target;
	x.dsPc = pc;
	x.needDs = need_delay_slot;
	x.state.capture();
#if defined(PCSX2_RECOMPILER_TESTS) && PS2_EE_TEST_BLOCK_METADATA
	s_curBlockContSites.push_back(pc - 4); // pc is the delay slot; the site is its branch
#endif
	return &*x.label;
}

// SL-10: the in-block footprint of a side exit is one island — a single far B
// (imm26, ±128MB reaches the cold arena) that the site's short-range
// conditional (Tbz ±32KB / B.cond ±1MB) can target. Bound after the tail,
// patched toward the outlined exit body once the cold session has emitted it.
static u8* s_sideExitIslands[kMaxContSites];

// A conditional call-out outlined like a side exit: B.cond to a one-word island
// after the tail, body in the cold arena at block end, B back to the site. The
// body is emitted after the block, so it must not touch allocator state.
struct ColdIsland
{
	std::unique_ptr<a64::Label> label;
	u8* island;
	u8* resume;
	u8* cold;
	std::function<void()> body;
};
static std::vector<ColdIsland> s_coldIslands;

#if (PS2_EE_SOFTMEM_COLD_OOL && defined(PS2_HOST_CODE_BRIDGE)) || PS2_EE_SOFTMEM_BLOCK_TAIL
struct SoftmemColdVeneer
{
	alignas(a64::Label) u8 labelStorage[sizeof(a64::Label)];
	u8* conditional;
	u8* target;
	a64::Label* label() { return reinterpret_cast<a64::Label*>(labelStorage); }
};
static SoftmemColdVeneer s_softmemColdVeneers[kEeSoftmemColdMaxOpsPerBlock];
static size_t s_softmemColdVeneerCount = 0;

struct SoftmemAllocatorState
{
	_arm64gprregs gprs[NUM_ARM_GPR_REGS];
	_arm64neonregs neon[NUM_ARM_NEON_REGS];

	void capture(u32 dirtyGprs, u32 dirtyNeon)
	{
		eeSoftmemForEachRegister(dirtyGprs, [&](int i) { gprs[i] = arm64gprs[i]; });
		eeSoftmemForEachRegister(dirtyNeon, [&](int i) { neon[i] = arm64neon[i]; });
	}

	void restore(u32 dirtyGprs, u32 dirtyNeon) const
	{
		eeSoftmemForEachRegister(dirtyGprs, [&](int i) { arm64gprs[i] = gprs[i]; });
		eeSoftmemForEachRegister(dirtyNeon, [&](int i) { arm64neon[i] = neon[i]; });
	}

	void captureAndInstall(const SoftmemAllocatorState& snapshot, u32 dirtyGprs, u32 dirtyNeon)
	{
		if (dirtyGprs)
		{
			auto* const live = arm64gprs;
			eeSoftmemForEachRegister(dirtyGprs, [&](int i) {
				gprs[i] = live[i];
				live[i] = snapshot.gprs[i];
			});
		}
		if (dirtyNeon)
		{
			auto* const live = arm64neon;
			eeSoftmemForEachRegister(dirtyNeon, [&](int i) {
				neon[i] = live[i];
				live[i] = snapshot.neon[i];
			});
		}
	}
};

struct SoftmemBlockTail
{
	explicit SoftmemBlockTail(a64::MacroAssembler* masm)
		: scratch(*masm->GetScratchRegisterList()), scratchV(*masm->GetScratchVRegisterList()),
		  scratchP(*masm->GetScratchPRegisterList()) {}

	SoftmemAllocatorState state;
	u32 dirtyGprs;
	u32 dirtyNeon;
	u32 guestPc;
	u32 guestCode;
	a64::CPURegList scratch;
	a64::CPURegList scratchV;
	a64::CPURegList scratchP;
	void (*emit)(void*);
	alignas(std::max_align_t) std::byte context[192];
};
// Allocated once on first use. The fixed veneer count bounds this scratch
// space, and each context lives only until the current block is finalized.
static std::vector<SoftmemBlockTail> s_softmemBlockTails;
static bool s_softmemColdHotOpen = false;
static bool s_softmemKeepHandlerWithBlock = false;

static void recEmitSoftmemBlockTail(size_t index)
{
	SoftmemColdVeneer& branch = s_softmemColdVeneers[index];
	const SoftmemBlockTail& body = s_softmemBlockTails[index];
	// The handler writes back only these captured slots. Keep every other
	// slot in the finished block's state and restore the overwritten slots.
	SoftmemAllocatorState finalState;
	finalState.captureAndInstall(body.state, body.dirtyGprs, body.dirtyNeon);
	pc = body.guestPc;
	cpuRegs.code = body.guestCode;
	a64::UseScratchRegisterScope scratch(armAsm);
	*armAsm->GetScratchRegisterList() = body.scratch;
	*armAsm->GetScratchVRegisterList() = body.scratchV;
	*armAsm->GetScratchPRegisterList() = body.scratchP;
	armAsm->Bind(branch.label());
	body.emit(s_softmemBlockTails[index].context);
	finalState.restore(body.dirtyGprs, body.dirtyNeon);
	armEmitJmp(branch.target);
}

static void recClearSoftmemColdVeneers()
{
	for (size_t i = 0; i < s_softmemColdVeneerCount; i++)
	{
		// Every used label is bound before armEndBlock. Reuse fixed storage
		// only after that assembler finalized all label references.
		s_softmemColdVeneers[i].label()->~Label();
	}
	s_softmemColdVeneerCount = 0;
	s_softmemBlockTails.clear();
}

static void recRecordSoftmemBlockTail(const EeSoftmemColdEmitter& emitter)
{
	if (s_softmemBlockTails.capacity() == 0)
		s_softmemBlockTails.reserve(kEeSoftmemColdMaxOpsPerBlock);
	SoftmemBlockTail& body = s_softmemBlockTails.emplace_back(armAsm);
	// The callback owns its live register lists. Writebacks additionally
	// need the dirty allocator slots and guest metadata from this access.
	body.dirtyGprs = emitter.dirtyGprs;
	body.dirtyNeon = emitter.dirtyNeon;
	body.state.capture(body.dirtyGprs, body.dirtyNeon);
	body.guestPc = pc;
	body.guestCode = cpuRegs.code;
	body.emit = emitter.emitMMIO;
	std::memcpy(body.context, emitter.mmioContext, emitter.mmioContextBytes);
	SoftmemColdVeneer& branch = s_softmemColdVeneers[s_softmemColdVeneerCount++];
	new (branch.labelStorage) a64::Label();
	if (emitter.tagged)
		armAsm->Tbnz(a64::x8, EeSoftmemIndirectBit, branch.label());
	else
		armAsm->B(branch.label(), a64::mi);
	emitter.emitRAM(emitter.ramContext);
	branch.target = armGetCurrentCodePointer();
}

#if PS2_EE_SOFTMEM_COLD_OOL && defined(PS2_HOST_CODE_BRIDGE)
struct SoftmemColdStats
{
	u64 requests = 0, emitted = 0, emittedBytes = 0, refusedBytes = 0;
	u64 refusedQuota = 0, refusedCapacity = 0, refusedBlock = 0, refusedContext = 0;
	u64 refusedBootBudget = 0;
	u64 beforeElfBytes = 0, afterElfBytes = 0, maxGenerationBytes = 0, maxBodyBytes = 0;
	u64 generations = 0;
};
static SoftmemColdStats s_softmemColdStats;
static size_t s_softmemColdGenerationBytes = 0;
static bool s_softmemColdHasBootedElf = false;
// Keep most of the existing generation quota available for the guest executable.
// This only limits compile-time outlining; refused operations use the exact inline path.
static constexpr size_t kEeSoftmemColdBootQuota = 32 * 1024;
static_assert(kEeSoftmemColdBootQuota > kEeSoftmemColdBodyCapacity + 15);
#ifndef PS2_EE_SOFTMEM_COLD_BUDGET_1M
#define PS2_EE_SOFTMEM_COLD_BUDGET_1M 0
#endif
// Use more of the existing 2 MiB cold arena without moving its split or
// reducing the hot cache. This is an emission allowance, never an allocation
// promise: actual free space, branch reach, and the exit headroom gate each op.
static constexpr size_t kEeSoftmemColdEffectiveGenerationQuota =
    PS2_EE_SOFTMEM_COLD_BUDGET_1M ? 1024 * 1024 : kEeSoftmemColdGenerationQuota;
static_assert(kEeSoftmemColdEffectiveGenerationQuota > kEeSoftmemColdBodyCapacity + 15);

#ifndef PS2_EE_SOFTMEM_COLD_BUDGET_ARENA
#define PS2_EE_SOFTMEM_COLD_BUDGET_ARENA 0
#endif
static size_t recSoftmemColdGenerationQuota()
{
#if PS2_EE_SOFTMEM_COLD_BUDGET_ARENA
 // The same64MiB EE reserve now assigns4MiB to shared cold output. Keep
 // optional softmem capped at1.8125MiB, even when physical capacity grows;
 // capacity-minus-headroom alone would repeat the rejected R375 starvation.
 if (!s_coldBase || s_coldPtrEnd <= s_coldBase)
  ps2_mtvu_retain_owner("softmem cold quota geometry", __LINE__);
 const size_t capacity = static_cast<size_t>(s_coldPtrEnd - s_coldBase);
 if (capacity <= kEeSoftmemColdExitHeadroom + kEeSoftmemColdBodyCapacity + 15)
  ps2_mtvu_retain_owner("softmem cold quota headroom", __LINE__);
 return std::min(capacity - kEeSoftmemColdExitHeadroom, size_t{1900544});
#else
 return kEeSoftmemColdEffectiveGenerationQuota;
#endif
}


bool recTryEmitSoftmemCold(const EeSoftmemColdEmitter& emitter)
{
	auto& stats = s_softmemColdStats;
	++stats.requests;
	// All admission failures precede any branch, RAM instruction, or cold write.
	// Deferred old side-exit compilation keeps the exact inline implementation.
	if (!s_softmemColdHotOpen || !s_pCurBlockEx ||
		!armCanEmitSoftmemCold((kEeSoftmemColdMaxOpsPerBlock + 2) * 4))
	{
		++stats.refusedContext;
		return false;
	}
	if (s_softmemColdVeneerCount >= kEeSoftmemColdMaxOpsPerBlock)
	{
		++stats.refusedBlock;
		return false;
	}
	if (s_softmemKeepHandlerWithBlock)
	{
		// Every pending arm fits within this primary window even with the
		// full secondary emission allowance per operation and a block tail.
		const size_t count = s_softmemColdVeneerCount;
		if (emitter.mmioContextBytes > sizeof(SoftmemBlockTail::context) ||
			!armCanEmitSoftmemCold((count + 2) * kEeSoftmemColdBodyCapacity))
		{
			++stats.refusedContext;
			return false;
		}
		recRecordSoftmemBlockTail(emitter);
		++stats.refusedContext;
		return true;
	}
	const size_t phaseQuota = s_softmemColdHasBootedElf ? recSoftmemColdGenerationQuota() : kEeSoftmemColdBootQuota;
	if (s_softmemColdGenerationBytes > phaseQuota - kEeSoftmemColdBodyCapacity - 15)
	{
		++stats.refusedQuota;
		if (!s_softmemColdHasBootedElf)
			++stats.refusedBootBudget;
		stats.refusedBytes += kEeSoftmemColdBodyCapacity;
		return false;
	}
	// Only the first cold body in this publication can leave the
	// previously published page. All subsequent bodies pack together.
	const u8* const limit = s_coldPtrEnd - kEeSoftmemColdBodyCapacity - kEeSoftmemColdExitHeadroom;
	u8* cursor = s_softmemColdVeneerCount == 0
		? recNextColdPageIfShortTail(s_coldPtr, limit) : s_coldPtr;
	const size_t quota_slack = phaseQuota - s_softmemColdGenerationBytes - kEeSoftmemColdBodyCapacity - 15;
	if (static_cast<size_t>(cursor - s_coldPtr) > quota_slack)
		cursor = s_coldPtr;
	u8* const cold = reinterpret_cast<u8*>((reinterpret_cast<uptr>(cursor) + 15) & ~uptr{15});
	if (cold < s_coldPtr || cold >= s_coldPtrEnd ||
		static_cast<size_t>(s_coldPtrEnd - cold) < kEeSoftmemColdBodyCapacity + kEeSoftmemColdExitHeadroom)
	{
		++stats.refusedCapacity;
		stats.refusedBytes += kEeSoftmemColdBodyCapacity;
		return false;
	}
	// Both B directions cross at most the existing EE arena (64 MiB in R96).
	// Validate the full 8 KiB reservation before starting the secondary scope.
	const intptr_t far = reinterpret_cast<intptr_t>(cold) - reinterpret_cast<intptr_t>(armGetCurrentCodePointer());
	if ((far & 3) || far <= -(intptr_t{1} << 27) ||
		far >= (intptr_t{1} << 27) - static_cast<intptr_t>(kEeSoftmemColdBodyCapacity))
	{
		++stats.refusedCapacity;
		return false;
	}
	SoftmemColdVeneer& x = s_softmemColdVeneers[s_softmemColdVeneerCount];
	new (x.labelStorage) a64::Label();
	x.target = cold;
	armSetSoftmemColdHotTail((s_softmemColdVeneerCount + 1) * 4);
	if (emitter.tagged)
		armAsm->Tst(a64::x8, EeSoftmemIndirectPage);
	armAsm->EnsureEmitPoolsFor(4);
	x.conditional = armGetCurrentCodePointer();
	armAsm->B(x.label(), emitter.tagged ? a64::ne : a64::mi);
	emitter.emitRAM(emitter.ramContext);
	u8* const resume = armGetCurrentCodePointer();
	const size_t bytes = armEmitSoftmemCold(cold, kEeSoftmemColdBodyCapacity, resume,
		emitter.mmioContext, emitter.emitMMIO);
	const size_t charged = static_cast<size_t>(cold - s_coldPtr) + bytes;
	s_coldPtr = cold + bytes;
	s_softmemColdGenerationBytes += charged;
	++stats.emitted;
	stats.emittedBytes += charged;
	(s_softmemColdHasBootedElf ? stats.afterElfBytes : stats.beforeElfBytes) += charged;
	stats.maxGenerationBytes = std::max<u64>(stats.maxGenerationBytes, s_softmemColdGenerationBytes);
	stats.maxBodyBytes = std::max<u64>(stats.maxBodyBytes, bytes);
	++s_softmemColdVeneerCount;
	return true;
}
#else
bool recTryEmitSoftmemCold(const EeSoftmemColdEmitter& emitter)
{
	constexpr size_t max_tails = 12;
	constexpr size_t mainline_headroom = 48 * _1kb;
	const size_t count = s_softmemColdVeneerCount;
	if (!s_softmemColdHotOpen || !s_softmemKeepHandlerWithBlock || armAddressRecorder ||
		CHECK_FASTMEM || count >= max_tails || emitter.mmioContextBytes > sizeof(SoftmemBlockTail::context))
		return false;

	const size_t used = armAsm->GetSizeOfCodeGenerated();
	// This is the existing primary write window. Reserve headroom for the
	// remainder of the short mainline, pending pools, and every queued arm.
	const size_t limit = std::min<size_t>(armAsm->GetBuffer()->GetCapacity(), _1mb);
	const size_t required = used + mainline_headroom +
		armAsm->GetLiteralPoolMaxSize() + armAsm->GetVeneerPoolMaxSize() +
		(count + 1) * kEeSoftmemColdBodyCapacity;
	if (required >= limit)
		return false;

	recRecordSoftmemBlockTail(emitter);
	return true;
}
#endif

static void recEmitSoftmemColdVeneers()
{
	if (s_softmemKeepHandlerWithBlock && s_softmemColdVeneerCount)
	{
		const u32 finalPc = pc;
		const u32 finalCode = cpuRegs.code;
		for (size_t i = 0; i < s_softmemColdVeneerCount; i++)
			recEmitSoftmemBlockTail(i);
		pc = finalPc;
		cpuRegs.code = finalCode;
		return;
	}
#if PS2_EE_SOFTMEM_COLD_OOL && defined(PS2_HOST_CODE_BRIDGE)
	for (size_t i = 0; i < s_softmemColdVeneerCount; i++)
	{
		SoftmemColdVeneer& x = s_softmemColdVeneers[i];
		armSetSoftmemColdHotTail((s_softmemColdVeneerCount - i - 1) * 4);
		// Pool emission occurs before binding. The branch word follows the label
		// immediately, and the condition's positive imm19 must fit before binding.
		armAsm->EnsureEmitPoolsFor(4);
		const intptr_t near = armGetCurrentCodePointer() - x.conditional;
		if ((near & 3) || near < 0 || near >= (intptr_t{1} << 20))
			ps2_mtvu_retain_owner("softmem hot veneer range", __LINE__);
		armAsm->Bind(x.label());
		a64::SingleEmissionCheckScope one(armAsm);
		const intptr_t far = x.target - armGetCurrentCodePointer();
		if ((far & 3) || far <= -(intptr_t{1} << 27) || far >= (intptr_t{1} << 27))
			ps2_mtvu_retain_owner("softmem hot far veneer range", __LINE__);
		armAsm->b(far >> 2);
	}
#endif
}
#endif

#ifdef PCSX2_RECOMPILER_TESTS
static u32 s_coldIslandBodiesEmitted = 0;
u32 recTestColdIslandBodiesEmitted()
{
	return s_coldIslandBodiesEmitted;
}
#endif

void recEmitColdIslandBranch(a64::Condition cond, std::function<void()> body)
{
	ColdIsland x;
	x.label = std::make_unique<a64::Label>();
	x.body = std::move(body);
	armAsm->B(x.label.get(), cond);
	x.resume = armGetCurrentCodePointer();
	s_coldIslands.push_back(std::move(x));
}

// Emit the per-exit islands after the block tail (still inside the hot
// emission session — they are part of the block and count in x86size).
// The mainline pc/size/recRAMCopy bookkeeping ran before this — pc is dead.
static void recEmitSideExitIslands()
{
	for (int k = 0; k < s_numSideExits; k++)
	{
		SuperblockSideExit& x = s_sideExits[k];
		armAsm->Bind(&*x.label);
		a64::SingleEmissionCheckScope guard(armAsm);
		s_sideExitIslands[k] = armGetCurrentCodePointer();
		armAsm->b(int64_t{0}); // placeholder; patched to the cold exit body
	}
	for (ColdIsland& x : s_coldIslands)
	{
		armAsm->Bind(x.label.get());
		a64::SingleEmissionCheckScope guard(armAsm);
		x.island = armGetCurrentCodePointer();
		armAsm->b(int64_t{0}); // placeholder; patched to the cold body
	}
#if (PS2_EE_SOFTMEM_COLD_OOL && defined(PS2_HOST_CODE_BRIDGE)) || PS2_EE_SOFTMEM_BLOCK_TAIL
	recEmitSoftmemColdVeneers();
#endif
}

#if defined(__SWITCH2__) && !defined(PS2_HOST_CODE_BRIDGE)
static bool eeSideExitFitsImmediateCharge()
{
	const auto fits = [](s8 selector) {
		return eeChargeFitsForm(scaleblockcycles_calculation_for(selector), EeChargeForm::AddImm12);
	};
	if (!fits(EECycleRate::GetEffective()))
		return false;
	const s8 top = EECycleRate::GetConfigured();
	for (int selector = EECycleRate::GetFloor(); selector <= top; ++selector)
	{
		if (!fits(static_cast<s8>(selector)))
			return false;
	}
	return true;
}
#endif

// SL-11: compact cold-exit tail — SetBranchImm's shape with the Str-pc /
// cycle-update / event-check factored into the shared SuperblockExitStub.
// Falls back to SetBranchImm for its special-case shapes (resident back-edge,
// WaitLoop FF), which never apply to a continuation side exit in practice.
static void recEmitSideExitTail(u32 imm)
{
	if ((s_loopResident && !s_loopBackedgeEmitted && imm == s_loopTopPc) ||
		(EmuConfig.Speedhacks.WaitLoop && s_nBlockFF && imm == s_branchTo))
	{
		SetBranchImm(imm);
		return;
	}

	const Cop2VfCacheScope vfCacheScope; // fork tail: preserve compile-time cache state
	g_branch = 1;
	// A zero target is legal and reachable here — see SetBranchImm.

	iFlushCall(FLUSH_EVERYTHING);

#if defined(__SWITCH2__) && !defined(PS2_HOST_CODE_BRIDGE)
	// Keep the shared fallback when the site's single ADD immediate cannot
	// represent the cycle charge at every in-place governor setting.
	if (eeSideExitFitsImmediateCharge())
	{
		armAsm->Mov(RWSCRATCH, imm);
		armAsm->Str(RWSCRATCH, armCpuRegMem(&cpuRegs.pc));
		const u32 cycles = scaleblockcycles_clear(EeChargeForm::AddImm12);
		const EeEmitterView emitter{armAsm, armAsmPtr};
		emitter.masm->Adds(RECCYCLE, RECCYCLE, cycles);
		recEeNoteChargeSiteAt(EeChargeSite::SuperblockExit, emitter.Cursor());
		emitDispatcherCondBranch(emitter, a64::ge, DispatcherEvent, s_condVeneerEvent);
		emitEeLinkedBranch(imm);
		return;
	}
#endif

	armAsm->Mov(RWSCRATCH, imm);
	armAsm->Mov(RWARG2, scaleblockcycles_clear(EeChargeForm::MovzImm16));
	recEeNoteChargeSite(EeChargeSite::SuperblockExit);
#if PS2_EE_INCOMING_PAGE_BATCH && defined(PS2_HOST_CODE_BRIDGE)
	// A taken continuation can use the authoritative dispatcher entry
	// without making its predecessor code page dirty on first resolution.
	// Already-compiled targets keep the established direct-link path.
	const u32 target_pc = HWADDR(imm);
	const BASEBLOCKEX* target = recBlocks.Get(target_pc);
	if (!target || target->startpc != target_pc)
	{
		// These aliases share the canonical RAM dispatch array. Its address
		// can change only on a reset, which also replaces every emitted exit.
		const u32 alias = imm & 0xe0000000u;
		const u32 offset = imm & 0x1fffffffu;
		const bool ram = (alias == 0 || alias == 0x80000000u || alias == 0xa0000000u) &&
			(imm & 3) == 0 && offset < Ps2MemSize::ExposedRam &&
			GETBLOCK(imm) == &recRAM[offset / 4];
		armEmitJmp(ram ? SuperblockRamExitStub : SuperblockDispatchExitStub);
		return;
	}
#endif
	armEmitCall(SuperblockExitStub);

	// RET lands here: the linked B (same patch protocol as SetBranchImm).
	emitEeLinkedBranch(imm);
}

// Island → cold-body patch: same single-word B rewrite + cache maintenance
// protocol as Arm64BaseBlocks link patching (compile-thread only here).
static void recPatchIslandB(u8* site, const u8* target, uptr pending_begin, uptr pending_end)
{
	const intptr_t imm26 = (reinterpret_cast<intptr_t>(target) - reinterpret_cast<intptr_t>(site)) >> 2;
	pxAssertRel(imm26 >= -(1 << 25) && imm26 < (1 << 25), "Cold-exit island out of B imm26 range");
	// armPatchCodeWord opens its own W^X scope: the islands live in the hot
	// block, whose window armEndBlock() already closed by the time the cold
	// session patches them.
	armPatchCodeWord(site, 0x14000000u | (static_cast<u32>(imm26) & 0x03FFFFFFu));
	// A branch wholly inside a fresh, not-yet-published hot range is
	// covered by that block's first publication. Live patches still flush now.
	const uptr address = reinterpret_cast<uptr>(site);
	if (address < pending_begin || address >= pending_end || pending_end - address < sizeof(u32))
		HostSys::FlushInstructionCache(site, sizeof(u32));
}

#if defined(PS2_EE_DEFER_HOT_PUBLICATION)
#if !defined(__SWITCH2__) || defined(PS2_HOST_CODE_BRIDGE)
#error Deferred EE publication requires the Switch 2 dual-alias provider
#endif

void recSetDeferredHotPublicationForTest(bool enabled)
{
	s_deferHotPublication = enabled;
}
#endif

// Emit every pending side exit body into the cold arena (a second emission
// session, after the hot block finalized) and patch the islands to reach
// them. Each body restores its branch-point snapshot, so the exit charges
// exactly the branch-point cycles and the flush writes back exactly what was
// live-dirty there.
static void recEmitColdSideExits([[maybe_unused]] bool first_cold_body,
	[[maybe_unused]] uptr pending_begin = 0, [[maybe_unused]] uptr pending_end = 0, [[maybe_unused]] bool ordered_warm = false)
{
	const int n = s_numSideExits;
	s_numSideExits = 0;
	if (n == 0 && s_coldIslands.empty())
		return;

	const u8* coldStart[kMaxContSites];

#ifdef PS2_HOST_CODE_BRIDGE
	if (first_cold_body)
		s_coldPtr = recNextColdPageIfShortTail(s_coldPtr, s_coldPtrEnd);
#endif
	armSetAsmPtr(s_coldPtr, s_coldPtrEnd - s_coldPtr + _64kb, &s_eeConstantPool);
	armStartBlock();
	for (int k = 0; k < n; k++)
	{
		SuperblockSideExit& x = s_sideExits[k];
		coldStart[k] = armGetCurrentCodePointer();
		x.state.restore();
		g_branch = 0;
		if (x.needDs)
		{
			pc = x.dsPc;
			recompileNextInstruction(true, false);
		}
		recEmitSideExitTail(x.branchTo);
		x.label.reset();
	}
	for (ColdIsland& x : s_coldIslands)
	{
		x.cold = armGetCurrentCodePointer();
		x.body();
		armEmitJmp(x.resume);
		x.label.reset();
#ifdef PCSX2_RECOMPILER_TESTS
		++s_coldIslandBodiesEmitted;
#endif
	}
	pxAssert(armGetCurrentCodePointer() < SysMemory::GetEERecEnd());
#if defined(IZPS2_SWITCH2_EE_WARM_ALIGN) && !defined(PS2_HOST_CODE_BRIDGE)
	s_coldPtr = ordered_warm ? armEndOrderedEeWarmBlock(true) : armEndBlock();
#else
	s_coldPtr = armEndBlock();
#endif

	// Each island patch opens its own write scope (armPatchCodeWord); an
	// arena-wide Begin/EndCodeWrite here would RX-flip a concurrently open
	// MTVU emit window in Legacy mode on its way out.
#ifdef PS2_HOST_CODE_BRIDGE
	// Both hot and cold sessions finalized. Commit/cache these terminal
	// numeric branch patches before clearing metadata or returning to JIT.
	Ps2ColdIslandWordPageBatch patches(SysMemory::GetEERec(), SysMemory::GetEERecEnd());
	for (int k = 0; k < n; k++)
		patches.AddBranch(s_sideExitIslands[k], coldStart[k]);
	for (const ColdIsland& x : s_coldIslands)
		patches.AddBranch(x.island, x.cold);
	patches.Finish();
#else
	for (int k = 0; k < n; k++)
		recPatchIslandB(s_sideExitIslands[k], coldStart[k], pending_begin, pending_end);
	for (const ColdIsland& x : s_coldIslands)
		recPatchIslandB(x.island, x.cold, pending_begin, pending_end);
#endif
#line 2326
	s_coldIslands.clear();

	g_branch = 1;
}

// =====================================================================================================
//  Instruction recompilation
// =====================================================================================================

// AX-05 bracket gate (EE-SRA 2 WS-A): TRUE when a delay-slot instruction can
// observe cpuRegs.branch at runtime, i.e. can reach cpuException (which takes
// it as bd) or the bracket epilogue's exception divert. Fork-verified raiser
// inventory (2026-07-07):
//   - memory ops: the MMIO fallback _ext_memRead/Write (Memory.cpp) calls
//     cpuTlbMissR/W(addr, cpuRegs.branch) from BOTH the fastmem slow path and
//     softmem. The inline load/store paths poll nothing, so the vector divert
//     rides the bracket epilogue and these must keep the bracket.
//   - SYSCALL (IS_BRANCH|BRANCHTYPE_SYSCALL), BREAK, and the trap family
//     TGE..TNE / TGEI..TNEI — all recBranchCall/recCall interpreter fallbacks
//     whose bodies call cpuException(_, cpuRegs.branch). BREAK and the traps
//     carry flags==0 in the opcode table, so they are keyed on opcode fields.
//   - any instruction without native codegen (interpreter fallback via
//     recCall): kept conservatively — an interp body may raise.
// NON-raisers (bracket dropped — the 4-insn win on the common ALU/move/shift/
// lui fillers): integer overflow is never raised by the rec (plain Add/Sub,
// same as x86), FPU/COP2 have no bd path,
// INTC/DMAC/TIMR fire only at event tests (block boundary, branch==0 there),
// and AdEL (RaiseAddressError) is a stub. Pinned by ee_rec_traps_tests.cpp
// delay-slot raiser tests + AluDelaySlotBranchSemanticsSurviveWithoutBracket.
//
// Memory ops compiled on the fastmem path drop the bracket too. Under a
// recompiler neither vtlb_Miss nor _ext_memUnknown raises (they report and
// return), and nothing else a load or store can reach calls cpuException, so
// the bracket's store, reload, branch and clear never observe anything. The
// fastmem backpatch thunk also has no divert to offer: it is generated at
// fault time and cannot name the block's live guest values
// (ee_rec_tlb_divert_tests.cpp). Softmem compiles (fastmem off, or a PC that
// has faulted before) keep the bracket, because the staged TLB-miss divert in
// those tests rides its epilogue. Pinned by ee_rec_delay_slot_bracket_tests.cpp.
#ifdef PCSX2_RECOMPILER_TESTS

namespace EERecFallback
{
	u32 g_groups = 0;
	u32 g_cop2RegMask[kCop2MoveOpCount] = {~0u, ~0u, ~0u, ~0u};
	u64 g_cop2VuMask[kCop2VuIdCount / 64] = {~0ull, ~0ull, ~0ull, ~0ull};
	u64 g_fpuMask[kFpuIdCount / 64] = {~0ull, ~0ull};

	enum : int
	{
		kFpuCvtSW = 0x40, kFpuMfc1, kFpuCfc1, kFpuMtc1, kFpuCtc1, kFpuBc1,
		kFpuLwc1, kFpuSwc1,
	};

	int FpuOpId(u32 code)
	{
		const u32 primary = code >> 26;
		if (primary == 0x31)
			return kFpuLwc1;
		if (primary == 0x39)
			return kFpuSwc1;
		if (primary != 0x11)
			return -1;
		const u32 rs = (code >> 21) & 0x1F;
		if (rs == 0x10)
			return static_cast<int>(code & 0x3F);
		if (rs == 0x14)
			return ((code & 0x3F) == 0x20) ? kFpuCvtSW : -1;
		switch (rs)
		{
			case 0x00: return kFpuMfc1;
			case 0x02: return kFpuCfc1;
			case 0x04: return kFpuMtc1;
			case 0x06: return kFpuCtc1;
			case 0x08: return kFpuBc1;
			default:   return -1;
		}
	}

	const char* FpuOpName(int id)
	{
		switch (id)
		{
			case 0x00: return "add";    case 0x01: return "sub";
			case 0x02: return "mul";    case 0x03: return "div";
			case 0x04: return "sqrt";   case 0x05: return "abs";
			case 0x06: return "mov";    case 0x07: return "neg";
			case 0x16: return "rsqrt";  case 0x18: return "adda";
			case 0x19: return "suba";   case 0x1A: return "mula";
			case 0x1C: return "madd";   case 0x1D: return "msub";
			case 0x1E: return "madda";  case 0x1F: return "msuba";
			case 0x24: return "cvtw";   case 0x28: return "max";
			case 0x29: return "min";    case 0x30: return "cf";
			case 0x32: return "ceq";    case 0x34: return "clt";
			case 0x36: return "cle";
			case kFpuCvtSW: return "cvts"; case kFpuMfc1: return "mfc1";
			case kFpuCfc1:  return "cfc1"; case kFpuMtc1: return "mtc1";
			case kFpuCtc1:  return "ctc1"; case kFpuBc1:  return "bc1";
			case kFpuLwc1:  return "lwc1"; case kFpuSwc1: return "swc1";
			default: return nullptr;
		}
	}

	// Mnemonics parallel to recCOP2_BC2t / recCOP2SPECIAL1t / recCOP2SPECIAL2t
	// (iR5900Misc-arm64.cpp). nullptr marks a hole in the encoding.
	static const char* const kBc2Names[4] = {"vbc2f", "vbc2t", "vbc2fl", "vbc2tl"};

	static const char* const kSpec1Names[64] = {
		"vaddx",   "vaddy",    "vaddz",   "vaddw",   "vsubx",   "vsuby",   "vsubz",   "vsubw",
		"vmaddx",  "vmaddy",   "vmaddz",  "vmaddw",  "vmsubx",  "vmsuby",  "vmsubz",  "vmsubw",
		"vmaxx",   "vmaxy",    "vmaxz",   "vmaxw",   "vminix",  "vminiy",  "vminiz",  "vminiw",
		"vmulx",   "vmuly",    "vmulz",   "vmulw",   "vmulq",   "vmaxi",   "vmuli",   "vminii",
		"vaddq",   "vmaddq",   "vaddi",   "vmaddi",  "vsubq",   "vmsubq",  "vsubi",   "vmsubi",
		"vadd",    "vmadd",    "vmul",    "vmax",    "vsub",    "vmsub",   "vopmsub", "vmini",
		"viadd",   "visub",    "viaddi",  nullptr,   "viand",   "vior",    nullptr,   nullptr,
		"vcallms", "vcallmsr", nullptr,   nullptr,   nullptr,   nullptr,   nullptr,   nullptr,
	};

	static const char* const kSpec2Names[128] = {
		"vaddax",  "vadday",  "vaddaz",  "vaddaw",  "vsubax",  "vsubay",  "vsubaz",  "vsubaw",
		"vmaddax", "vmadday", "vmaddaz", "vmaddaw", "vmsubax", "vmsubay", "vmsubaz", "vmsubaw",
		"vitof0",  "vitof4",  "vitof12", "vitof15", "vftoi0",  "vftoi4",  "vftoi12", "vftoi15",
		"vmulax",  "vmulay",  "vmulaz",  "vmulaw",  "vmulaq",  "vabs",    "vmulai",  "vclip",
		"vaddaq",  "vmaddaq", "vaddai",  "vmaddai", "vsubaq",  "vmsubaq", "vsubai",  "vmsubai",
		"vadda",   "vmadda",  "vmula",   nullptr,   "vsuba",   "vmsuba",  "vopmula", "vnop",
		"vmove",   "vmr32",   nullptr,   nullptr,   "vlqi",    "vsqi",    "vlqd",    "vsqd",
		"vdiv",    "vsqrt",   "vrsqrt",  "vwaitq",  "vmtir",   "vmfir",   "vilwr",   "viswr",
		"vrnext",  "vrget",   "vrinit",  "vrxor",   nullptr,   nullptr,   nullptr,   nullptr,
		// 0x48-0x7F are holes.
	};

	int Cop2VuOpId(u32 code)
	{
		if ((code >> 26) != 0x12)
			return -1;
		const u32 rs = (code >> 21) & 0x1F;
		if (rs == 0x08) // BC2
			return static_cast<int>((code >> 16) & 0x1F);
		if (rs < 0x10) // QMFC2/CFC2/QMTC2/CTC2 and holes are not VU macro ops
			return -1;

		const u32 funct = code & 0x3F;
		if (funct >= 0x3C) // SPECIAL2 escape
			return 0x80 + static_cast<int>((code & 0x3) | ((code >> 4) & 0x7C));
		return 0x40 + static_cast<int>(funct);
	}

	const char* Cop2VuOpName(int id)
	{
		if (id < 0 || id >= kCop2VuIdCount)
			return nullptr;
		if (id < 0x20)
			return (id < 4) ? kBc2Names[id] : nullptr;
		if (id >= 0x40 && id < 0x80)
			return kSpec1Names[id - 0x40];
		if (id >= 0x80)
			return kSpec2Names[id - 0x80];
		return nullptr;
	}

	static u32 s_cop2VuCensus[kCop2VuIdCount];
	static u32 s_cop2VuCensusTotal;
	static u32 s_cop2VuCensusLogged;

	void NoteCop2VuCompiled(u32 code)
	{
		const int id = Cop2VuOpId(code);
		if (id >= 0)
		{
			s_cop2VuCensus[id]++;
			s_cop2VuCensusTotal++;
		}
	}

	std::string DescribeCop2VuCensus()
	{
		std::string s;
		u32 total = 0;
		for (int id = 0; id < kCop2VuIdCount; id++)
		{
			if (s_cop2VuCensus[id] == 0)
				continue;
			total += s_cop2VuCensus[id];
			const char* name = Cop2VuOpName(id);
			if (!s.empty())
				s += " ";
			s += fmt::format("{}={}", name ? name : fmt::format("id0x{:02x}", id), s_cop2VuCensus[id]);
		}
		if (s.empty())
			return "no VU macro ops compiled";
		return fmt::format("{} (total {})", s, total);
	}

	void InitFromEnvOnce()
	{
		static bool s_done = false;
		if (s_done)
			return;
		s_done = true;

		const char* list = std::getenv("ARMSX2_REC_FALLBACK");
		if (!list || !*list)
			return;

		u32 groups = 0;
		u32 reg_masks[kCop2MoveOpCount] = {~0u, ~0u, ~0u, ~0u};
		u64 fpu_mask[kFpuIdCount / 64] = {~0ull, ~0ull};
		u64 vu_mask[kCop2VuIdCount / 64] = {~0ull, ~0ull, ~0ull, ~0ull};
		std::string error;
		if (!ParseGroups(list, &groups, reg_masks, fpu_mask, vu_mask, &error))
		{
			Console.Error("ARMSX2_REC_FALLBACK: %s", error.c_str());
			return;
		}

		g_groups = groups;
		std::memcpy(g_cop2RegMask, reg_masks, sizeof(g_cop2RegMask));
		std::memcpy(g_fpuMask, fpu_mask, sizeof(g_fpuMask));
		std::memcpy(g_cop2VuMask, vu_mask, sizeof(g_cop2VuMask));
		std::string detail;
		if ((g_groups & Fpu) && (fpu_mask[0] != ~0ull || fpu_mask[1] != ~0ull))
		{
			for (int i = 0; i < kFpuIdCount; i++)
			{
				if ((g_fpuMask[i >> 6] & (1ull << (i & 63))) == 0)
					continue;
				if (const char* n = FpuOpName(i))
					detail += detail.empty() ? fmt::format(" (fpu {}", n) : fmt::format(":{}", n);
			}
			if (!detail.empty())
				detail += ")";
		}
		Console.WriteLn(Color_Yellow, "EERecFallback: forcing %s to the interpreter%s",
			DescribeGroups(g_groups).c_str(), detail.c_str());
	}

	void MaybeLogCop2VuCensus(bool end_of_run)
	{
		const char* want = std::getenv("ARMSX2_REC_FALLBACK_CENSUS");
		if (!want || *want == '0')
			return;
		// A reset with nothing new to report is a boot artifact: the first
		// resets fire before a block has reached the emitter, and an empty
		// census there reads as "this game compiles no VU macro ops".
		if (!end_of_run && s_cop2VuCensusTotal == s_cop2VuCensusLogged)
			return;
		s_cop2VuCensusLogged = s_cop2VuCensusTotal;
		Console.WriteLn(Color_Yellow, "COP2VU CENSUS: %s", DescribeCop2VuCensus().c_str());
	}

	bool Selected(u32 code)
	{
		if (g_groups == 0)
			return false;

		const u32 primary = code >> 26;
		u32 group = 0;

		switch (primary)
		{
			case 0x00: // SPECIAL
			{
				const u32 funct = code & 0x3F;
				if (funct == 0x08 || funct == 0x09) // JR / JALR
					group = Branch;
				else if (funct == 0x0A || funct == 0x0B || (funct >= 0x10 && funct <= 0x13))
					group = Move; // MOVZ/MOVN, MFHI/MTHI/MFLO/MTLO
				else if (funct >= 0x18 && funct <= 0x1B)
					group = MultDiv;
				else if (funct <= 0x07 || funct == 0x14 || funct == 0x16 || funct == 0x17 ||
						 funct == 0x38 || funct == 0x3A || funct == 0x3B || funct == 0x3C ||
						 funct == 0x3E || funct == 0x3F)
					group = Shift;
				else if ((funct >= 0x20 && funct <= 0x2F) || funct == 0x28 || funct == 0x29)
					group = Arith; // ADD..NOR, SLT/SLTU, DADD..DSUBU, MFSA/MTSA
				break;
			}
			case 0x01: // REGIMM — branches (trap immediates stay native)
			{
				const u32 rt = (code >> 16) & 0x1F;
				if (rt <= 0x03 || (rt >= 0x10 && rt <= 0x13))
					group = Branch;
				break;
			}
			case 0x02: case 0x03:                                     // J / JAL
			case 0x04: case 0x05: case 0x06: case 0x07:               // BEQ/BNE/BLEZ/BGTZ
			case 0x14: case 0x15: case 0x16: case 0x17:               // *L variants
				group = Branch;
				break;
			case 0x08: case 0x09: case 0x0A: case 0x0B:               // ADDI/ADDIU/SLTI/SLTIU
			case 0x0C: case 0x0D: case 0x0E: case 0x0F:               // ANDI/ORI/XORI/LUI
			case 0x18: case 0x19:                                     // DADDI/DADDIU
				group = Arith;
				break;
			case 0x10: group = Cop0; break;
			case 0x11: group = Fpu; break;                            // COP1
			case 0x12: // COP2 — split by rs so the VU macro ops can be isolated
			{
				const u32 rs = (code >> 21) & 0x1F;
				int moveOp = -1;
				switch (rs)
				{
					case 0x01: group = Qmfc2; moveOp = kQmfc2; break;
					case 0x02: group = Cfc2;  moveOp = kCfc2;  break;
					case 0x05: group = Qmtc2; moveOp = kQmtc2; break;
					case 0x06: group = Ctc2;  moveOp = kCtc2;  break;
					default:   group = Cop2Vu; break;                 // BC2 + CO=1 macro ops
				}
				// Register filter: fs/rd is bits 15-11 for all four move ops.
				if (moveOp >= 0 && (g_cop2RegMask[moveOp] & (1u << ((code >> 11) & 0x1F))) == 0)
					return false;
				break;
			}
			case 0x1C: group = Mmi; break;
			case 0x31: case 0x39: group = Fpu; break;                 // LWC1 / SWC1
			case 0x36: case 0x3E: group = Cop2Ls; break;              // LQC2 / SQC2
			case 0x1A: case 0x1B:                                     // LDL / LDR
			case 0x1E: case 0x1F:                                     // LQ / SQ
			case 0x37: case 0x3F:                                     // LD / SD
				group = LoadStore;
				break;
			default:
				if (primary >= 0x20 && primary <= 0x2E) // LB..SWR
					group = LoadStore;
				break;
		}

		// An unfiltered `fpu` stays every COP1 encoding: the S format's
		// unassigned functs have ids but no mnemonic to filter by.
		if (group == Fpu && (g_fpuMask[0] != ~0ull || g_fpuMask[1] != ~0ull))
		{
			const int id = FpuOpId(code);
			if (id < 0 || (g_fpuMask[id >> 6] & (1ull << (id & 63))) == 0)
				return false;
		}

		if (group == Cop2Vu)
		{
			// Per-op filter: an unnamed/unmapped encoding is never selected, so a
			// mnemonic filter can't accidentally widen to the whole group.
			const int id = Cop2VuOpId(code);
			if (id < 0 || (g_cop2VuMask[id >> 6] & (1ull << (id & 63))) == 0)
				return false;
		}

		return group != 0 && (g_groups & group) != 0;
	}

	static const struct { const char* name; u32 bit; } kGroupNames[] = {
		{"fpu", Fpu}, {"cop2", Cop2All}, {"mmi", Mmi}, {"multdiv", MultDiv},
		{"shift", Shift}, {"arith", Arith}, {"loadstore", LoadStore},
		{"move", Move}, {"cop0", Cop0}, {"branch", Branch},
		{"cop2move", Cop2Move}, {"cop2vu", Cop2Vu}, {"cop2ls", Cop2Ls},
		{"qmfc2", Qmfc2}, {"cfc2", Cfc2}, {"qmtc2", Qmtc2}, {"ctc2", Ctc2},
	};

	// Maps a group bit to its COP2 move-op register-filter slot, or -1.
	static int MoveOpSlotForGroup(u32 bit)
	{
		switch (bit)
		{
			case Qmfc2: return kQmfc2;
			case Cfc2:  return kCfc2;
			case Qmtc2: return kQmtc2;
			case Ctc2:  return kCtc2;
			default:    return -1;
		}
	}

	bool ParseGroups(const std::string_view& list, u32* out, u32* reg_masks, u64* fpu_mask,
		u64* cop2vu_mask, std::string* error)
	{
		u32 local_reg_masks[kCop2MoveOpCount] = {~0u, ~0u, ~0u, ~0u};
		bool reg_filtered[kCop2MoveOpCount] = {false, false, false, false};
		u64 local_fpu_mask[kFpuIdCount / 64] = {~0ull, ~0ull};
		bool fpu_filtered = false;
		u64 local_vu_mask[kCop2VuIdCount / 64] = {~0ull, ~0ull, ~0ull, ~0ull};
		bool vu_filtered = false;
		u32 mask = 0;
		size_t pos = 0;
		while (pos <= list.size())
		{
			const size_t comma = list.find(',', pos);
			const size_t end = (comma == std::string_view::npos) ? list.size() : comma;
			std::string_view tok = list.substr(pos, end - pos);
			while (!tok.empty() && tok.front() == ' ') tok.remove_prefix(1);
			while (!tok.empty() && tok.back() == ' ') tok.remove_suffix(1);

			if (!tok.empty())
			{
				if (tok == "none")
				{
					mask = 0;
				}
				else if (tok == "all")
				{
					for (const auto& g : kGroupNames)
						mask |= g.bit;
				}
				else
				{
					// Optional "<group>:<reg>[:<reg>...]" register filter.
					std::string_view name = tok;
					std::string_view regs;
					const size_t colon = tok.find(':');
					if (colon != std::string_view::npos)
					{
						name = tok.substr(0, colon);
						regs = tok.substr(colon + 1);
					}

					bool found = false;
					for (const auto& g : kGroupNames)
					{
						if (name != g.name)
							continue;
						found = true;
						mask |= g.bit;

						if (!regs.empty() && g.bit == Fpu)
						{
							// "fpu:mul:madd" — narrow to named COP1 ops.
							size_t rp = 0;
							while (rp <= regs.size())
							{
								const size_t rc = regs.find(':', rp);
								const size_t rend = (rc == std::string_view::npos) ? regs.size() : rc;
								const std::string_view mnem = regs.substr(rp, rend - rp);
								int id = -1;
								for (int i = 0; i < kFpuIdCount; i++)
								{
									const char* n = FpuOpName(i);
									if (n && mnem == n)
									{
										id = i;
										break;
									}
								}
								if (id < 0)
								{
									if (error)
									{
										*error = fmt::format("unknown COP1 op '{}' in EE rec fallback "
											"filter '{}'", std::string(mnem), std::string(tok));
									}
									return false;
								}
								if (!fpu_filtered)
								{
									for (auto& w : local_fpu_mask)
										w = 0;
									fpu_filtered = true;
								}
								local_fpu_mask[id >> 6] |= (1ull << (id & 63));
								if (rc == std::string_view::npos)
									break;
								rp = rc + 1;
							}
						}
						else if (!regs.empty() && g.bit == Cop2Vu)
						{
							// "cop2vu:vmulaw:vmaddaz" — narrow to named macro ops.
							size_t rp = 0;
							while (rp <= regs.size())
							{
								const size_t rc = regs.find(':', rp);
								const size_t rend = (rc == std::string_view::npos) ? regs.size() : rc;
								const std::string_view mnem = regs.substr(rp, rend - rp);
								int id = -1;
								for (int i = 0; i < kCop2VuIdCount; i++)
								{
									const char* n = Cop2VuOpName(i);
									if (n && mnem == n)
									{
										id = i;
										break;
									}
								}
								if (id < 0)
								{
									if (error)
									{
										*error = fmt::format("unknown VU macro op '{}' in EE rec fallback "
											"filter '{}'", std::string(mnem), std::string(tok));
									}
									return false;
								}
								if (!vu_filtered)
								{
									for (auto& w : local_vu_mask)
										w = 0;
									vu_filtered = true;
								}
								local_vu_mask[id >> 6] |= (1ull << (id & 63));
								if (rc == std::string_view::npos)
									break;
								rp = rc + 1;
							}
						}
						else if (!regs.empty())
						{
							const int slot = MoveOpSlotForGroup(g.bit);
							if (slot < 0)
							{
								if (error)
								{
									*error = fmt::format("EE rec fallback group '{}' does not take a register "
										"filter (only qmfc2/cfc2/qmtc2/ctc2 take ':<reg>', fpu and cop2vu "
										"take ':<mnemonic>')", std::string(name));
								}
								return false;
							}
							size_t rp = 0;
							while (rp <= regs.size())
							{
								const size_t rc = regs.find(':', rp);
								const size_t rend = (rc == std::string_view::npos) ? regs.size() : rc;
								const std::string_view rtok = regs.substr(rp, rend - rp);
								const std::optional<u32> parsed = StringUtil::FromChars<u32>(rtok, 10);
								const u32 reg = parsed.value_or(0);
								if (rtok.empty() || !parsed.has_value() || reg > 31)
								{
									if (error)
									{
										*error = fmt::format("bad register '{}' in EE rec fallback filter "
											"'{}' (expected 0-31)", std::string(rtok), std::string(tok));
									}
									return false;
								}
								if (!reg_filtered[slot])
								{
									local_reg_masks[slot] = 0;
									reg_filtered[slot] = true;
								}
								local_reg_masks[slot] |= (1u << reg);
								if (rc == std::string_view::npos)
									break;
								rp = rc + 1;
							}
						}
						break;
					}
					if (!found)
					{
						if (error)
						{
							*error = fmt::format("unknown EE rec fallback group '{}'; valid: none, all, "
								"fpu, cop2, mmi, multdiv, shift, arith, loadstore, move, cop0, branch, "
								"cop2move, cop2vu, cop2ls, qmfc2, cfc2, qmtc2, ctc2 (the last four accept "
								"a ':<reg>' filter, e.g. ctc2:27; cop2vu accepts a ':<mnemonic>' filter, "
								"e.g. cop2vu:vmaddaw)",
								std::string(name));
						}
						return false;
					}
				}
			}

			if (comma == std::string_view::npos)
				break;
			pos = comma + 1;
		}

		*out = mask;
		for (int i = 0; i < kCop2MoveOpCount; i++)
			reg_masks[i] = local_reg_masks[i];
		for (int i = 0; i < kFpuIdCount / 64; i++)
			fpu_mask[i] = local_fpu_mask[i];
		for (int i = 0; i < kCop2VuIdCount / 64; i++)
			cop2vu_mask[i] = local_vu_mask[i];
		return true;
	}

	std::string DescribeGroups(u32 groups)
	{
		if (groups == 0)
			return "none (full native codegen)";
		std::string s;
		for (const auto& g : kGroupNames)
		{
			if (groups & g.bit)
			{
				if (!s.empty()) s += ",";
				s += g.name;
			}
		}
		return s;
	}
} // namespace EERecFallback

#endif // PCSX2_RECOMPILER_TESTS

static bool delaySlotNeedsBranchBracket(u32 code, const R5900::OPCODE& op)
{
	if (op.flags & IS_BRANCH)
		return true; // SYSCALL + branch-in-delay-slot interpreter fallback
	if (!op.recompile)
		return true; // conservative: interpreter fallback may raise
#ifdef PCSX2_RECOMPILER_TESTS
	if (EERecFallback::Selected(code))
		return true; // same conservatism for a harness-forced interpreter fallback,
		             // so the bisect switch cannot itself change exception semantics
#endif
	if (op.flags & (IS_LOAD | IS_STORE | IS_MEMORY))
	{
		// Same test every memory emitter makes for its own access (pc is the
		// delay slot's here; it advances after the slot compiles).
		const bool fastmem = CHECK_FASTMEM && !vtlb_IsFaultingPC(pc);
		return !fastmem; // softmem keeps the staged TLB-miss divert's seam
	}
	const u32 primary = code >> 26;
	if (primary == 0x00)
	{
		const u32 funct = code & 0x3F;
		if (funct == 0x0D) // BREAK (flags==0 in the table)
			return true;
		if (funct >= 0x30 && funct <= 0x36) // TGE/TGEU/TLT/TLTU/TEQ/TNE
			return true;
	}
	else if (primary == 0x01)
	{
		const u32 rt = (code >> 16) & 0x1F;
		if (rt >= 0x08 && rt <= 0x0E) // TGEI/TGEIU/TLTI/TLTIU/TEQI/TNEI
			return true;
	}
	return false;
}

// Preserve the VTLB handler and interpreter cache paths. Resolve each
// instruction independently so delay slots can cross a virtual page.
static __fi u32 readEeCompileInstruction(u32 address, bool* direct = nullptr,
	const u8** direct_page = nullptr)
{
	if (CHECK_EEREC && (address & 3) == 0)
	{
		const auto mapping = vtlb_private::vtlbdata.vmap[address >> vtlb_private::VTLB_PAGE_BITS];
		if (!mapping.isHandler(address))
		{
			u32 instruction;
			if (direct)
				*direct = true;
			if (direct_page)
			{
				const u32 page = address & ~(vtlb_private::VTLB_PAGE_SIZE - 1);
				if (!mapping.isHandler(page) && !mapping.isHandler(page + vtlb_private::VTLB_PAGE_SIZE - 1))
					*direct_page = reinterpret_cast<const u8*>(mapping.assumePtr(page));
			}
			std::memcpy(&instruction, reinterpret_cast<const void*>(mapping.assumePtr(address)), sizeof(instruction));
			return instruction;
		}
	}
	return memRead32(address);
}


#ifdef PS2_EE_SCAN_PAGE_WITNESS
extern "C" {
u64 ps2_ee_scan_page_lookups = 0;
u64 ps2_ee_scan_page_direct_reads = 0;
}
#define EE_SCAN_LOOKUP() (++ps2_ee_scan_page_lookups)
#define EE_SCAN_DIRECT() (++ps2_ee_scan_page_direct_reads)
#else
#define EE_SCAN_LOOKUP() ((void)0)
#define EE_SCAN_DIRECT() ((void)0)
#endif

// Cache the first direct instruction page during the initial block scan.
// Lookahead can reach the next page; the original reader handles it and
// the cached pointer is retired before any fallback can invoke a handler.
struct EeInstructionScan
{
	u32 page;
	const u8* bytes = nullptr;

	explicit EeInstructionScan(u32 start) : page(start & ~u32{0xfff})
	{
		if (CHECK_EEREC && (start & 3) == 0)
		{
			EE_SCAN_LOOKUP();
			const auto mapping = vtlb_private::vtlbdata.vmap[page >> vtlb_private::VTLB_PAGE_BITS];
			if (!mapping.isHandler(page) && !mapping.isHandler(page + 0xfff))
				bytes = reinterpret_cast<const u8*>(mapping.assumePtr(page));
		}
	}

	u32 Read(u32 address)
	{
		if (bytes && address - page < 0x1000)
		{
			EE_SCAN_DIRECT();
			u32 instruction;
			std::memcpy(&instruction, bytes + (address - page), sizeof(instruction));
			return instruction;
		}
		bytes = nullptr;
		if (CHECK_EEREC && (address & 3) == 0)
			EE_SCAN_LOOKUP();
		return readEeCompileInstruction(address);
	}
};

extern "C" __attribute__((visibility("hidden"))) bool ps2_patch_has_active_dynamic_patches();

// Applies to hot emission and the deferred cold delay slots of this block.
static bool s_eeMayHaveDynamicPatches = true;


#ifdef PS2_EE_DELAY_FETCH_WITNESS
extern "C" {
u64 ps2_ee_delay_fetch_reads = 0;
u64 ps2_ee_delay_fetch_reused = 0;
}
#define EE_DELAY_FETCH_READ() (++ps2_ee_delay_fetch_reads)
#define EE_DELAY_FETCH_REUSE() (++ps2_ee_delay_fetch_reused)
#else
#define EE_DELAY_FETCH_READ() ((void)0)
#define EE_DELAY_FETCH_REUSE() ((void)0)
#endif

// These instructions emit only native integer operations and cannot
// change the guest instruction mapping during compilation. All other
// emitters retire the cached mapping before the next forward fetch.
static bool eeEmitterKeepsInstructionPage(u32 code)
{
	switch (code >> 26)
	{
		case 0x09: // ADDIU
		case 0x0a: // SLTI
		case 0x0b: // SLTIU
		case 0x0c: // ANDI
		case 0x0d: // ORI
		case 0x0e: // XORI
		case 0x19: // DADDIU
			return true;
		default:
			return code == 0; // NOP
	}
}

// These SPECIAL handlers only read allocator state and assemble register
// operations. Any other SPECIAL instruction keeps the mapping barrier.
static bool eeForwardEmitterKeepsInstructionPage(u32 code)
{
	const u32 primary = code >> 26;
	if (primary != 0)
		return primary == 0x0f || eeEmitterKeepsInstructionPage(code); // LUI or existing immediate path

	switch (code & 63)
	{
		case 0x00: case 0x02: case 0x03: // SLL, SRL, SRA
		case 0x04: case 0x06: case 0x07: // SLLV, SRLV, SRAV
		case 0x0a: case 0x0b: // MOVZ, MOVN
		case 0x14: case 0x16: case 0x17: // DSLLV, DSRLV, DSRAV
		case 0x21: case 0x23: case 0x2d: case 0x2f: // ADDU, SUBU, DADDU, DSUBU
		case 0x24: case 0x25: case 0x26: case 0x27: // AND, OR, XOR, NOR
		case 0x2a: case 0x2b: // SLT, SLTU
		case 0x38: case 0x3a: case 0x3b: // DSLL, DSRL, DSRA
		case 0x3c: case 0x3e: case 0x3f: // DSLL32, DSRL32, DSRA32
			return true;
		default:
			return false;
	}
}

#ifdef PS2_EE_SOFTMEM_COP2_BATCH
namespace R5900::Dynarec::OpcodeImpl
{
bool recTryCop2SoftmemBatch(const u32* ops, unsigned count);
}
#endif

#ifdef PS2_EE_SOFTMEM_FPR_PAGE_GROUP
namespace R5900::Dynarec::OpcodeImpl
{
bool recTryFprLoadPageGroup(const u32* ops, unsigned count, s32 lowest, s32 highest);
}
#endif

static void recompileEeInstruction(EEINST*& current_inst_info, bool delayslot, bool swapped_delay_slot, const u32* decoded,
	_arm64gprregs* gpr_regs, _arm64neonregs* neon_regs, const u8** forward_page = nullptr)
{
	// Apply GameDB DynamicPatch pattern-matches during recompilation, matching
	// x86 recompileNextInstruction. Without this, per-instruction dynamic
	// patches from the game database never take effect under the arm64 EE rec.
	// Upstream 7ee62b822.
	const bool apply_patches = s_eeMayHaveDynamicPatches && EmuConfig.EnablePatches;
	if (apply_patches)
		Patch::ApplyDynamicPatches(pc);

	const u32 old_code = cpuRegs.code;
	EEINST* old_inst_info = current_inst_info;

	// The swap decoder has no side effects between a direct read and this call.
	// Handlers and dynamic patches retain the original second read.
	if (decoded && !apply_patches)
	{
		EE_DELAY_FETCH_REUSE();
		cpuRegs.code = *decoded;
	}
	else
	{
		if (swapped_delay_slot)
			EE_DELAY_FETCH_READ();
		if (forward_page && !apply_patches && *forward_page)
		{
			const size_t offset = pc & (vtlb_private::VTLB_PAGE_SIZE - 1);
			std::memcpy(&cpuRegs.code, *forward_page + offset, sizeof(cpuRegs.code));
		}
		else
		{
			if (apply_patches && forward_page)
				*forward_page = nullptr;
			cpuRegs.code = readEeCompileInstruction(pc, nullptr, apply_patches ? nullptr : forward_page);
		}
	}

#if defined(PS2_EE_SOFTMEM_TAGGED_PAGES) && defined(PS2_EE_SOFTMEM_GPR_LOAD_GROUP)
	// The unified softmem plane checks both direct RAM and the eager
	// write guard. A mixed group may use that translation for every op.
	const u32 first_memory = cpuRegs.code;
	const auto first_access = eeDecodeScalarAccess(first_memory);
	const u32 memory_base = (first_memory >> 21) & 31;
	if ((first_access.bytes || eeIsUnalignedDwordHalf(first_memory >> 26)) &&
		!delayslot && !swapped_delay_slot && !apply_patches &&
		forward_page && *forward_page && !s_loopResident && !CHECK_FASTMEM &&
		!g_eeSoftmemUseCanonicalMap && !EmuConfig.Trace.Enabled &&
		!CBreakPoints::GetNumMemchecks() && !CBreakPoints::GetNumBreakpoints() &&
		!GPR_IS_CONST1(memory_base))
	{
		using namespace vtlb_private;
		const u32 code_offset = pc & VTLB_PAGE_MASK;
		const u32 available = std::min(s_nEndBlock - pc, VTLB_PAGE_SIZE - code_offset) / 4;
		std::array<u32, 12> ops;
		EeMemoryPageSpan span;
		bool loads = false;
		bool mixedWidth = false;
		unsigned memoryCount = 0;
		unsigned gaps = 0;
		unsigned lastMemoryEnd = 0;
		unsigned count = 0;
		EeUnalignedDwordPair unaligned;
		if (!first_access.bytes && available >= 2)
		{
			u32 next;
			std::memcpy(&next, *forward_page + code_offset + 4, sizeof(next));
			unaligned = eeDecodeUnalignedDwordPair(first_memory, next);
			if (unaligned.valid)
			{
				ops[0] = first_memory;
				ops[1] = next;
				// Eight consecutive bytes, with no alignment constraint.
				span = {unaligned.offset, unaligned.offset + 8, unaligned.offset, 1};
				lastMemoryEnd = 2;
			}
		}
		for (; first_access.bytes && count < std::min(available, static_cast<u32>(ops.size())); ++count)
		{
			u32 next;
			std::memcpy(&next, *forward_page + code_offset + count * 4, sizeof(next));
			const auto access = eeDecodeScalarAccess(next);
			const u32 guest = (next >> 16) & 31;
			if (!access.bytes)
			{
				if (gaps == 2 || !eeCanGroupAddressIndependentOp(next, memory_base))
					break;
				++gaps;
				ops[count] = next;
				continue;
			}
			if (((next >> 21) & 31) != memory_base ||
				(!access.store && (!guest || guest == memory_base)))
				break;
			const s32 limit = (loads || !access.store ? 192 : 512) + first_access.bytes;
			if (!span.extend(static_cast<s16>(next), access.bytes, limit))
				break;
			loads |= !access.store;
			mixedWidth |= access.bytes != first_access.bytes;
			++memoryCount;
			lastMemoryEnd = count + 1;
			ops[count] = next;
		}
		// Only consume ALU operations with another grouped access after them.
		count = lastMemoryEnd;
#ifdef PCSX2_RECOMPILER_TESTS
		const bool forced = std::any_of(ops.begin(), ops.begin() + count, EERecFallback::Selected);
#else
		constexpr bool forced = false;
#endif
		// A naturally aligned pair fits one page without a separate
		// span check. Keep the scalar arm for every other address.
		const bool loadPairAcrossZero = count == 3 && eeIsLoadPairAcrossZero(ops[0], ops[1], ops[2]);
		const bool alignedLoadPair = loadPairAcrossZero;
		// Leave homogeneous quad sequences to the existing align-down path.
		if (!forced && (unaligned.valid || alignedLoadPair ||
			((mixedWidth || first_access.bytes < 16) && memoryCount >= (loads ? 3u : 5u))))
		{
			iFlushCall(FLUSH_EVERYTHING);
			const auto base = _eeGetGPRSourceReg(a64::w9, memory_base);
			armAsm->Add(a64::w9, base, span.first);
			a64::Label scalar, done;
			if (alignedLoadPair)
			{
				static_assert(VTLB_PAGE_SIZE % 16 == 0);
				armAsm->Tst(a64::w9, first_access.bytes * 2 - 1);
				armAsm->B(&scalar, a64::ne);
			}
			else if (span.alignment > 1)
			{
				auto aligned = a64::w9;
				const u32 delta = static_cast<u32>(span.anchor - span.first) & (span.alignment - 1);
				if (delta)
				{
					armAsm->Add(a64::w8, a64::w9, delta);
					aligned = a64::w8;
				}
				armAsm->Tst(aligned, span.alignment - 1);
				armAsm->B(&scalar, a64::ne);
			}
			if (!alignedLoadPair)
			{
				armAsm->And(a64::w8, a64::w9, VTLB_PAGE_MASK);
				armAsm->Cmp(a64::w8, VTLB_PAGE_SIZE - (span.end - span.first));
				armAsm->B(&scalar, a64::hi);
			}
			armAsm->Lsr(a64::w8, a64::w9, VTLB_PAGE_BITS);
			armAsm->Ldr(a64::x8, a64::MemOperand(RFASTMEMBASE, a64::x8, a64::LSL, 3));
			armAsm->Tbnz(a64::x8, EeSoftmemIndirectBit, &scalar);
			armAsm->Add(a64::x8, a64::x8, a64::Operand(a64::w9, a64::UXTW));

			const auto source = [](u32 guest, const a64::Register& scratch) {
				if (!guest)
					return scratch.Is64Bits() ? a64::Register(a64::xzr) : a64::Register(a64::wzr);
				if (const auto* pin = armEEPinForGPR(guest))
					return scratch.Is64Bits() ? *pin : pin->W();
				// The fast arm can have written this guest register after
				// the flush. Its current memory value wins over compile-time
				// constants, which still describe entry to the scalar arm.
				armAsm->Ldr(scratch, armCpuRegMem(&cpuRegs.GPR.r[guest].UD[0]));
				return scratch;
			};
			const auto commit = [](u32 guest, const a64::Register* pin, const a64::Register& result) {
				if (!pin)
					armStoreEERegPtr(result, &cpuRegs.GPR.r[guest].UD[0]);
			};
			if (unaligned.valid)
			{
				const u32 guest = (first_memory >> 16) & 31;
				const a64::MemOperand memory(a64::x8);
				if (unaligned.store)
					armAsm->Str(source(guest, a64::x10), memory);
				else
				{
					const auto* pin = armEEPinForGPR(guest);
					const auto result = pin ? *pin : a64::Register(a64::x10);
					armAsm->Ldr(result, memory);
					commit(guest, pin, result);
				}
			}

			// This register is independent of both loads. Move the zeroing
			// after the pair only in direct RAM. The scalar arm runs in order.
			if (loadPairAcrossZero)
				std::swap(ops[1], ops[2]);
			for (unsigned i = unaligned.valid ? count : 0; i < count; ++i)
			{
				const u32 op = ops[i];
				const u32 guest = (op >> 16) & 31;
				const auto access = eeDecodeScalarAccess(op);
				if (!access.bytes)
				{
					const unsigned zero = eeGroupZeroDestination(op);
					if (zero < 32)
					{
						if (const auto* pin = armEEPinForGPR(zero))
							armAsm->Mov(*pin, 0);
						else if (zero)
							commit(zero, nullptr, a64::xzr);
						continue;
					}
					if (!guest)
						continue;
					const u32 sourceGuest = (op >> 21) & 31;
					const auto* pin = armEEPinForGPR(guest);
					const auto result = pin ? *pin : a64::Register(a64::x10);
					const auto operand = source(sourceGuest, a64::x10);
					const u32 imm = static_cast<u16>(op);
					switch (op >> 26)
					{
					case 0x09:
						armAsm->Add(result.W(), operand.W(), static_cast<s16>(imm));
						armAsm->Sxtw(result, result.W());
						break;
					case 0x0c:
						armAsm->And(result, operand, imm);
						break;
					case 0x0d:
						armAsm->Orr(result, operand, imm);
						break;
					case 0x0e:
						armAsm->Eor(result, operand, imm);
						break;
					default:
						pxAssert(false); // Only eeCanGroupAddressIndependentOp reaches this arm.
					}
					commit(guest, pin, result);
					continue;
				}
				const s32 offset = static_cast<s16>(op);
				const s32 disp = offset - span.first;
				const auto memory = a64::MemOperand(a64::x8, disp);
				const unsigned bytes = access.bytes;
				if (bytes == 16)
				{
					if (!access.store)
					{
						armAsm->Ldr(RQSCRATCH, memory);
						armStoreEEGPRQuad(RQSCRATCH, guest);
					}
					else
					{
						if (guest)
						{
							armAsm->Ldr(RQSCRATCH, armCpuRegMem(&cpuRegs.GPR.r[guest]));
							armMergeEEPinIntoQuad(RQSCRATCH, guest);
						}
						else
						{
							armAsm->Movi(RQSCRATCH.V16B(), 0);
						}
						armAsm->Str(RQSCRATCH, memory);
					}
					continue;
				}
				const auto* pin = armEEPinForGPR(guest);
				const auto result = pin ? *pin : a64::Register(a64::x10);

				if (bytes >= 4 && disp % bytes == 0 && disp <= static_cast<s32>(63 * bytes) && i + 1 < count)
				{
					const u32 next = ops[i + 1];
					const u32 other_guest = (next >> 16) & 31;
					if ((next >> 26) == (op >> 26) && static_cast<s16>(next) == offset + static_cast<s32>(bytes) &&
						(access.store || guest != other_guest))
					{
						if (access.store)
						{
							const auto first = source(guest, bytes == 8 ? a64::Register(a64::x10) : a64::Register(a64::w10));
							const auto second = source(other_guest, bytes == 8 ? a64::Register(a64::x17) : a64::Register(a64::w17));
							armAsm->Stp(first, second, memory);
						}
						else
						{
							const auto* other_pin = armEEPinForGPR(other_guest);
							const auto other_result = other_pin ? *other_pin : a64::Register(a64::x17);
							if (bytes == 8)
								armAsm->Ldp(result, other_result, memory);
							else if (access.sign)
								armAsm->Ldpsw(result, other_result, memory);
							else
								armAsm->Ldp(result.W(), other_result.W(), memory);
							commit(guest, pin, result);
							commit(other_guest, other_pin, other_result);
						}
						++i;
						continue;
					}
				}

				if (access.store)
				{
					const auto value = source(guest, bytes == 8 ? a64::Register(a64::x10) : a64::Register(a64::w10));
					switch (bytes)
					{
					case 1: armAsm->Strb(value, memory); break;
					case 2: armAsm->Strh(value, memory); break;
					default: armAsm->Str(value, memory); break;
					}
					continue;
				}
				switch (bytes)
				{
				case 1:
					if (access.sign) armAsm->Ldrsb(result, memory);
					else armAsm->Ldrb(result.W(), memory);
					break;
				case 2:
					if (access.sign) armAsm->Ldrsh(result, memory);
					else armAsm->Ldrh(result.W(), memory);
					break;
				case 4:
					if (access.sign) armAsm->Ldrsw(result, memory);
					else armAsm->Ldr(result.W(), memory);
					break;
				case 8: armAsm->Ldr(result, memory); break;
				}
				commit(guest, pin, result);
			}
			armAsm->B(&done);
			armAsm->Bind(&scalar);
			if (loadPairAcrossZero)
				std::swap(ops[1], ops[2]);
			for (unsigned i = 0; i < count; ++i)
				recompileEeInstruction(current_inst_info, false, false, &ops[i], gpr_regs, neon_regs);
			iFlushCall(FLUSH_EVERYTHING);
			armAsm->Bind(&done);
			*forward_page = nullptr;
			return;
		}
	}
#endif

#if defined(PS2_EE_SOFTMEM_TAGGED_PAGES) && !defined(PS2_EE_SOFTMEM_GPR_LOAD_GROUP)
	// Stack and structure stores need one translation when the entire span
	// is aligned, direct and inside one guest page. Keep guest store order.
	const u32 first_store = cpuRegs.code;
	const u32 store_base = (first_store >> 21) & 31;
	if ((first_store >> 26) == 0x2b && !delayslot && !swapped_delay_slot &&
		!apply_patches && forward_page && *forward_page && !s_loopResident &&
		!CHECK_FASTMEM && !g_eeSoftmemUseCanonicalMap && !EmuConfig.Trace.Enabled &&
		!CBreakPoints::GetNumMemchecks() && !CBreakPoints::GetNumBreakpoints() &&
		!GPR_IS_CONST1(store_base))
	{
		using namespace vtlb_private;
		const u32 code_offset = pc & (VTLB_PAGE_SIZE - 1);
		const u32 available = std::min(s_nEndBlock - pc, VTLB_PAGE_SIZE - code_offset) / 4;
		std::array<u32, 12> ops;
		ops[0] = first_store;
		s32 lowest = static_cast<s16>(first_store);
		s32 highest = lowest;
		unsigned count = 1;
		for (; count < std::min(available, static_cast<u32>(ops.size())); ++count)
		{
			u32 next;
			std::memcpy(&next, *forward_page + code_offset + count * 4, sizeof(next));
			const s32 offset = static_cast<s16>(next);
			if (((next ^ first_store) >> 21) || ((next ^ first_store) & 3))
				break;
			const s32 lo = std::min(lowest, offset);
			const s32 hi = std::max(highest, offset);
			if (hi - lo > 512)
				break;
			lowest = lo;
			highest = hi;
			ops[count] = next;
		}
#ifdef PCSX2_RECOMPILER_TESTS
		const bool forced_interp = std::any_of(ops.begin(), ops.begin() + count, EERecFallback::Selected);
#else
		constexpr bool forced_interp = false;
#endif
		if (count >= 5 && !forced_interp)
		{
			iFlushCall(FLUSH_EVERYTHING);
			const auto base = _eeGetGPRSourceReg(a64::w9, store_base);
			armAsm->Add(a64::w9, base, lowest);

			a64::Label scalar, done;
			armAsm->Tst(a64::w9, 3);
			armAsm->B(&scalar, a64::ne);
			armAsm->And(a64::w8, a64::w9, VTLB_PAGE_SIZE - 1);
			armAsm->Cmp(a64::w8, VTLB_PAGE_SIZE - (highest - lowest + 4));
			armAsm->B(&scalar, a64::hi);
			armAsm->Lsr(a64::w8, a64::w9, VTLB_PAGE_BITS);
			armAsm->Ldr(a64::x8, a64::MemOperand(RFASTMEMBASE, a64::x8, a64::LSL, 3));
			armAsm->Tbnz(a64::x8, EeSoftmemIndirectBit, &scalar);
			armAsm->Add(a64::x8, a64::x8, a64::Operand(a64::w9, a64::UXTW));
			for (unsigned i = 0; i < count; ++i)
			{
				const u32 rt = (ops[i] >> 16) & 31;
				const auto value = rt ? _eeGetGPRSourceReg(a64::w10, rt) : a64::wzr;
				armAsm->Str(value, a64::MemOperand(a64::x8, static_cast<s16>(ops[i]) - lowest));
			}
			armAsm->B(&done);

			armAsm->Bind(&scalar);
			for (unsigned i = 0; i < count; ++i)
				recompileEeInstruction(current_inst_info, false, false, &ops[i], gpr_regs, neon_regs);
			iFlushCall(FLUSH_EVERYTHING);
			armAsm->Bind(&done);
			*forward_page = nullptr;
			return;
		}
	}
#endif

#ifdef PS2_EE_SOFTMEM_TAGGED_PAGES
	// Register save and restore sequences can access many guest quads on one
	// virtual page. Group only straight-line, consecutive LQ or SQ instructions.
	if ((cpuRegs.code >> 27) == 0xf && !delayslot && !swapped_delay_slot &&
		!apply_patches && forward_page && *forward_page && !s_loopResident &&
		!CHECK_FASTMEM && !g_eeSoftmemUseCanonicalMap && !EmuConfig.Trace.Enabled &&
		!CBreakPoints::GetNumMemchecks() && !CBreakPoints::GetNumBreakpoints())
	{
		using namespace vtlb_private;
		const u32 first = cpuRegs.code;
		const bool load = (first >> 26) == 0x1e;
		const u32 base = (first >> 21) & 31;
		const s32 displacement = static_cast<s16>(first);
		const u32 offset = pc & (VTLB_PAGE_SIZE - 1);
		const u32 available = std::min(s_nEndBlock - pc, VTLB_PAGE_SIZE - offset) / 4;
		std::array<u32, 12> ops;
		ops[0] = first;
		unsigned count = 1;
		for (; count < std::min(available, static_cast<u32>(ops.size())); ++count)
		{
			// A load may replace the address base only on the last access.
			if (load && base && ((ops[count - 1] >> 16) & 31) == base)
				break;
			u32 next;
			std::memcpy(&next, *forward_page + offset + count * 4, sizeof(next));
			if (((next ^ first) >> 21) || static_cast<s16>(next) != displacement + static_cast<s32>(count * 16))
				break;
			ops[count] = next;
		}
#ifdef PCSX2_RECOMPILER_TESTS
		const bool forcedInterp = std::any_of(ops.begin(), ops.begin() + count, EERecFallback::Selected);
#else
		constexpr bool forcedInterp = false;
#endif
		if (count >= 6 && !forcedInterp)
		{
			// Both paths enter and leave with the same canonical allocator state.
			// Lazy GPR pins remain authoritative for their lower halves.
			iFlushCall(FLUSH_EVERYTHING);
			_eeMoveGPRtoR(a64::w9, base);
			if (displacement)
				armAsm->Add(a64::w9, a64::w9, displacement);
			armAsm->And(a64::w9, a64::w9, ~0xfu);

			a64::Label scalar, done;
			armAsm->And(a64::w8, a64::w9, VTLB_PAGE_SIZE - 1);
			armAsm->Cmp(a64::w8, VTLB_PAGE_SIZE - count * 16);
			armAsm->B(&scalar, a64::hi);
			armAsm->Lsr(a64::w8, a64::w9, VTLB_PAGE_BITS);
			armAsm->Ldr(a64::x8, a64::MemOperand(RFASTMEMBASE, a64::x8, a64::LSL, 3));
			armAsm->Tbnz(a64::x8, EeSoftmemIndirectBit, &scalar);
			armAsm->Add(a64::x8, a64::x8, a64::Operand(a64::w9, a64::UXTW));

			for (unsigned i = 0; i < count; ++i)
			{
				const u32 reg = (ops[i] >> 16) & 31;
				const auto memory = a64::MemOperand(a64::x8, i * 16);
				if (load)
				{
					armAsm->Ldr(RQSCRATCH, memory);
					if (reg)
						armStoreEEGPRQuad(RQSCRATCH, reg);
				}
				else
				{
					if (reg)
					{
						armAsm->Ldr(RQSCRATCH, armCpuRegMem(&cpuRegs.GPR.r[reg]));
						armMergeEEPinIntoQuad(RQSCRATCH, reg);
					}
					else
					{
						armAsm->Movi(RQSCRATCH.V16B(), 0);
					}
					armAsm->Str(RQSCRATCH, memory);
				}
			}
			armAsm->B(&done);

			// A crossing, wraparound or handler-backed page executes each
			// original operation through its existing emitter and cycle accounting.
			armAsm->Bind(&scalar);
			for (unsigned i = 0; i < count; ++i)
				recompileEeInstruction(current_inst_info, false, false, &ops[i], gpr_regs, neon_regs);
			iFlushCall(FLUSH_EVERYTHING);
			armAsm->Bind(&done);
			*forward_page = nullptr;
			return;
		}
	}
#endif

#ifdef PS2_EE_SOFTMEM_COP2_BATCH
	if (!delayslot && !swapped_delay_slot && !apply_patches && forward_page && *forward_page &&
		!EmuConfig.Trace.Enabled && !CBreakPoints::GetNumMemchecks())
	{
		const u32 first = cpuRegs.code;
		const u32 op = first >> 26;
		const u32 base = (first >> 21) & 31;
		const u32 rt = (first >> 16) & 31;
		const s32 imm = static_cast<s16>(first);
		const u32 offset = pc & (vtlb_private::VTLB_PAGE_SIZE - 1);
		const u32 available = std::min(s_nEndBlock - pc, vtlb_private::VTLB_PAGE_SIZE - offset) / 4;
		if ((op == 0x36 || op == 0x3e) && available >= 3 && rt && rt <= 29)
		{
			u32 ops[4] = {first};
			unsigned count = 1;
			for (; count < std::min({available, 4u, 32 - rt}); ++count)
			{
				u32 next;
				std::memcpy(&next, *forward_page + offset + count * 4, sizeof(next));
				if ((next >> 26) != op || ((next >> 21) & 31) != base ||
					((next >> 16) & 31) != rt + count ||
					static_cast<s16>(next) != imm + static_cast<s32>(count * 16))
					break;
				ops[count] = next;
			}
			if (count >= 3)
			{
#ifdef PCSX2_RECOMPILER_TESTS
				const bool forcedInterp = std::any_of(ops, ops + count, EERecFallback::Selected);
#else
				constexpr bool forcedInterp = false;
#endif
				g_cpuFlushedPC = false;
				g_cpuFlushedCode = false;
				if (!forcedInterp && R5900::Dynarec::OpcodeImpl::recTryCop2SoftmemBatch(ops, count))
				{
					const u32 scale = 2 - ((cpuRegs.CP0.n.Config >> 18) & 1);
					s_nBlockCycles += R5900::GetInstruction(first).cycles * count * scale;
					g_maySignalException = false;
					*forward_page = nullptr;
					return;
				}
			}
		}
	}
#endif

#ifdef PS2_EE_SOFTMEM_FPR_PAGE_GROUP
	if ((cpuRegs.code >> 26) == 0x31 && !delayslot && !swapped_delay_slot && !apply_patches &&
		forward_page && *forward_page && !s_loopResident && !CHECK_FASTMEM && !g_eeSoftmemUseCanonicalMap &&
		!EmuConfig.Trace.Enabled && !CBreakPoints::GetNumMemchecks() && !CBreakPoints::GetNumBreakpoints())
	{
		using namespace vtlb_private;
		const u32 first = cpuRegs.code;
		const u32 code_offset = pc & (VTLB_PAGE_SIZE - 1);
		const u32 available = std::min(s_nEndBlock - pc, VTLB_PAGE_SIZE - code_offset) / 4;
		std::array<u32, 4> ops{first};
		u32 destinations = 1u << ((first >> 16) & 31);
		s32 lowest = static_cast<s16>(first), highest = lowest;
		unsigned count = 1;
		for (; count < std::min(available, static_cast<u32>(ops.size())); ++count)
		{
			u32 next;
			std::memcpy(&next, *forward_page + code_offset + count * 4, sizeof(next));
			const s32 offset = static_cast<s16>(next);
			const u32 destination = 1u << ((next >> 16) & 31);
			if (((next ^ first) >> 21) || ((next ^ first) & 3) || (destinations & destination))
				break;
			const s32 lo = std::min(lowest, offset), hi = std::max(highest, offset);
			if (hi - lo > 512)
				break;
			lowest = lo;
			highest = hi;
			destinations |= destination;
			ops[count] = next;
		}
#ifdef PCSX2_RECOMPILER_TESTS
		const bool forced = std::any_of(ops.begin(), ops.begin() + count, EERecFallback::Selected);
#else
		constexpr bool forced = false;
#endif
		if (count >= 3 && !forced && R5900::Dynarec::OpcodeImpl::recTryFprLoadPageGroup(ops.data(), count, lowest, highest))
		{
#ifdef IZPS2_LOCAL_EE_FPR_GROUP_TRACE
			std::fprintf(stderr, "LOCAL_EE_FPR_GROUP pc=%08x count=%u span=%d\n", pc - count * 4, count, highest - lowest + 4);
#endif
			s_nBlockCycles += R5900::GetInstruction(first).cycles * count * (2 - ((cpuRegs.CP0.n.Config >> 18) & 1));
			_clearNeededArm64GPRregs(gpr_regs);
			_clearNeededNEONregs(neon_regs);
			g_cpuFlushedPC = false;
			g_cpuFlushedCode = false;
			g_maySignalException = false;
			*forward_page = nullptr;
			return;
		}
	}
#endif

#ifdef PS2_EE_SOFTMEM_FPR_PAIRS
	if (!delayslot && !swapped_delay_slot && !apply_patches && forward_page && *forward_page &&
		(s_nEndBlock - pc >= 8))
	{
		const u32 first = cpuRegs.code;
		const u32 op = first >> 26;
		const u32 offset = pc & (vtlb_private::VTLB_PAGE_SIZE - 1);
		if ((op == 0x31 || op == 0x39) && offset < vtlb_private::VTLB_PAGE_SIZE - 4)
		{
			u32 second;
			std::memcpy(&second, *forward_page + offset + 4, sizeof(second));
#ifdef PCSX2_RECOMPILER_TESTS
			const bool forcedInterp = EERecFallback::Selected(first) || EERecFallback::Selected(second);
#else
			constexpr bool forcedInterp = false;
#endif
			if (!forcedInterp && R5900::Dynarec::OpcodeImpl::recTryFprMemoryPair(second))
			{
				const u32 cycles = R5900::GetInstruction(first).cycles + R5900::GetInstruction(second).cycles;
				s_nBlockCycles += cycles * (2 - ((cpuRegs.CP0.n.Config >> 18) & 0x1));
				_clearNeededArm64GPRregs(gpr_regs);
				_clearNeededNEONregs(neon_regs);
				g_cpuFlushedPC = false;
				g_cpuFlushedCode = false;
				g_maySignalException = false;
				*forward_page = nullptr;
				return;
			}
		}
	}
#endif

	// EP-2b: default-flush seam — any op that is not a cache-aware
	// hand-rolled COP2 macro op kills the VF residency cache (dirty
	// writebacks emit here, before the op's own code). Whitelist over
	// blocklist: an emitter this policy doesn't know about can never
	// corrupt or be corrupted by the cache.
	if (!cop2OpPreservesVfCache(cpuRegs.code))
		cop2VfCacheFlush();

	if (!delayslot)
	{
		pc += 4;
		g_cpuFlushedPC = false;
		g_cpuFlushedCode = false;
	}
	else
	{
		// For delay slots, increment pc after recompiling (at the end of this function)
		g_recompilingDelaySlot = true;
	}

	current_inst_info++;

	// Branch/jump in a delay slot. Verbatim port of the x86 check_branch_delay
	// block (iR5900.cpp:1742-1803, "new code by FlatOut"). When the delay slot is
	// itself a branch, the inner branch is squashed — emit NOTHING (not even an
	// interpreter call) and return before cycle counting, so cpuRegs.cycle and pc
	// match x86 exactly. This matches x86's behavior, not the interpreter's: the
	// EE interpreter (_doBranch_shared) fully EXECUTES the inner branch, so this
	// path deliberately diverges from interp on this undefined-behavior corner
	// (ps2autotests: HW returns 2, both JIT and interp imperfect). Opcode set is
	// the exact x86 switch (jr/jalr; bltz/bgez(al)(l) family; j/jal/b{eq,ne,lez,
	// gtz}(l)). The downstream isBranchInDelaySlot fallback is now unreachable for
	// these but kept as defense-in-depth.
	if (delayslot)
	{
		bool check_branch_delay = false;
		switch (_Opcode_)
		{
			case 0:
				switch (_Funct_)
				{
					case 8:  // jr
					case 9:  // jalr
						check_branch_delay = true;
						break;
				}
				break;

			case 1:
				switch (_Rt_)
				{
					case 0:
					case 1:
					case 2:
					case 3:
					case 0x10:
					case 0x11:
					case 0x12:
					case 0x13:
						check_branch_delay = true;
						break;
				}
				break;

			case 2:
			case 3:
			case 4:
			case 5:
			case 6:
			case 7:
			case 0x14:
			case 0x15:
			case 0x16:
			case 0x17:
				check_branch_delay = true;
				break;
		}

		if (check_branch_delay)
		{
			DevCon.Warning("Branch %x in delay slot!", cpuRegs.code);
			_clearNeededArm64GPRregs();
			_clearNeededNEONregs();
			pc += 4;
			g_cpuFlushedPC = false;
			g_cpuFlushedCode = false;
			if (g_maySignalException)
				armClearCauseBD();

			g_recompilingDelaySlot = false;
			return;
		}
	}

	// Bracket can-raise delay-slot bodies with cpuRegs.branch = 1 / 0,
	// mirroring the interpreter's _doBranch_shared. Every exception raiser
	// (trap/SYSCALL/BREAK, and the aarch64 vtlb TLB-miss path) passes
	// cpuRegs.branch as the bd argument to cpuException — with no store the
	// rec always raised BD=0 with a delay-slot resume address, and the kernel
	// would ERET straight to the delay slot, losing the branch. The epilogue
	// below also uses the flag to detect that an exception fired
	// (cpuException zeroes it) and divert to the dispatcher — without that,
	// the enclosing branch's static dispatch overwrites the exception vector
	// and the branch target executes as if nothing happened. (AX-05)
	// Gated to raiser classes only (EE-SRA 2 WS-A): cpuRegs.branch was the
	// single hottest state field at 4.2% of ALL emitted EE instructions when
	// every non-NOP slot paid the 4-insn bracket; the common ALU/move/shift
	// fillers can't observe it (see delaySlotNeedsBranchBracket).
	const R5900::OPCODE* const opcode = cpuRegs.code != 0 ? &R5900::GetCurrentInstruction() : nullptr;
	const bool dsExceptionBracket = delayslot && opcode && delaySlotNeedsBranchBracket(cpuRegs.code, *opcode);
	if (dsExceptionBracket)
	{
		armAsm->Mov(RWSCRATCH, 1);
		armAsm->Str(RWSCRATCH, armCpuRegMem(&cpuRegs.branch));
	}

	// NOP gets cycle counted but no codegen (matching x86 behavior)
	if (!opcode)
	{
		s_nBlockCycles += 9 * (2 - ((cpuRegs.CP0.n.Config >> 18) & 0x1));
	}
	else
	{
		s_nBlockCycles += opcode->cycles * (2 - ((cpuRegs.CP0.n.Config >> 18) & 0x1));

		// Guard: branch/jump in a delay slot would cause infinite
		// compile-time recursion. Use interpreter for the instruction.
		const bool isBranchInDelaySlot = delayslot && (opcode->flags & IS_BRANCH);
#ifdef PCSX2_RECOMPILER_TESTS
		// Harness-only: --rec-fallback bisect switch (see EERecFallback).
		const bool forcedInterp = EERecFallback::Selected(cpuRegs.code);
#else
		constexpr bool forcedInterp = false;
#endif
#ifdef IZPS2_SWITCH2_STARTUP_TRACE
        if (s_ee_trace_reference_lwc1 && !delayslot && pc - 4 == s_ee_trace_window_start &&
            (cpuRegs.code >> 26) == 0x31) {
            if (forward_page)
                *forward_page = nullptr;
            recCall(eeTraceReferenceLwc1);
        }
        else
#endif
		if (isBranchInDelaySlot || !opcode->recompile || forcedInterp)
		{
			if (forward_page)
				*forward_page = nullptr;
			if ((opcode->flags & IS_BRANCH) && !isBranchInDelaySlot)
				recBranchCall(opcode->interpret);
			else
				recCall(opcode->interpret);
		}
		else
		{
			if (forward_page && !eeForwardEmitterKeepsInstructionPage(cpuRegs.code))
				*forward_page = nullptr;
			opcode->recompile();
		}
	}

	// SP misalignment check disabled: MMI/COP2 instructions legitimately use
	// r29 as SIMD data, causing massive false-positive spam.

	if (!swapped_delay_slot)
	{
		_clearNeededArm64GPRregs(gpr_regs);
		_clearNeededNEONregs(neon_regs);
	}

	if (delayslot)
	{
		if (dsExceptionBracket)
		{
			// cpuException zeroes cpuRegs.branch; still-1 means the delay
			// slot completed without raising. On an exception, divert to the
			// dispatcher on the vector PC before the enclosing branch's
			// static dispatch can clobber it. Cycle undercount on this
			// exceptional path is accepted. (AX-05)
			const EeEmitterView emitter{armAsm, armAsmPtr};
			emitter.masm->Ldr(RWSCRATCH, armCpuRegMem(&cpuRegs.branch));
			emitDispatcherCbz(emitter, RWSCRATCH, DispatcherReg, s_condVeneerReg);
			emitter.masm->Str(a64::wzr, armCpuRegMem(&cpuRegs.branch));
		}

		pc += 4;
		g_cpuFlushedPC = false;
		g_cpuFlushedCode = false;
		if (g_maySignalException) // mirrors x86 iR5900.cpp:1830 (dormant: always false)
			armClearCauseBD();
		g_recompilingDelaySlot = false;
	}

	g_maySignalException = false; // mirrors x86 iR5900.cpp:1835

	// When called from TrySwapDelaySlot (swapped_delay_slot=true), restore
	// cpuRegs.code so that the caller's _Rs_/_Rt_/_Rd_ macros still work.
	// Matches x86 at iR5900.cpp:1918-1921.
	if (swapped_delay_slot)
	{
		cpuRegs.code = old_code;
		current_inst_info = old_inst_info;
	}
}

void recompileNextInstruction(bool delayslot, bool swapped_delay_slot)
{
	recompileEeInstruction(g_pCurInstInfo, delayslot, swapped_delay_slot, nullptr,
		swapped_delay_slot ? nullptr : arm64gprs, swapped_delay_slot ? nullptr : arm64neon);
}

#if PS2_EE_SOFTMEM_COLD_OOL && defined(PS2_HOST_CODE_BRIDGE)
// A delay slot whose native emitter only changes integer registers can
// share the hot tail. Retain the cold path for patches, handlers and emitters
// that might introduce additional outlined sections.
static bool recCanCompactDelaySlot(u32 address)
{
	if (!CHECK_EEREC || (address & 3) || (s_eeMayHaveDynamicPatches && EmuConfig.EnablePatches))
		return false;
	const auto mapping = vtlb_private::vtlbdata.vmap[address >> vtlb_private::VTLB_PAGE_BITS];
	if (mapping.isHandler(address))
		return false;
	u32 code;
	std::memcpy(&code, reinterpret_cast<const void*>(mapping.assumePtr(address)), sizeof(code));
#ifdef PCSX2_RECOMPILER_TESTS
	if (EERecFallback::Selected(code))
		return false;
#endif
	return eeEmitterKeepsInstructionPage(code);
}

static void recEmitCompactSideExits()
{
	// A hoisted delay slot needs only its terminal tail. A deferred slot
	// can emit arbitrary code, so leave the original cold path in charge.
	const int count = s_numSideExits;
	if (count == 0)
		return;

	for (int k = 0; k < count; k++)
	{
		const SuperblockSideExit& exit = s_sideExits[k];
		if ((exit.needDs && !recCanCompactDelaySlot(exit.dsPc)) ||
			(s_loopResident && !s_loopBackedgeEmitted && exit.branchTo == s_loopTopPc) ||
			(EmuConfig.Speedhacks.WaitLoop && s_nBlockFF && exit.branchTo == s_branchTo))
			return;
	}

	// At a continuation site GPR/FPR allocations and constants were
	// written back before the snapshot. Allow for the remaining VF cache
	// writebacks, the terminal tails, the other islands and the SMC stub.
	const size_t headroom = 384 * count + 128 +
		(s_coldIslands.size() + s_softmemColdVeneerCount) * 4;
	const size_t page_left = _4kb - (reinterpret_cast<uptr>(armGetCurrentCodePointer()) & (_4kb - 1));
	if (page_left < headroom || !armCanEmitSoftmemCold(headroom))
		return;

	// TBZ/TBNZ has the shortest reach of any continuation branch.
	// Include pending pools and the tail allowance before binding any label.
	const size_t pools = armAsm->GetLiteralPoolMaxSize() + armAsm->GetVeneerPoolMaxSize();
	constexpr size_t branch_reach = 32 * 1024;
	if (pools > page_left - headroom ||
		armAsm->GetSizeOfCodeGenerated() + pools + headroom >= branch_reach)
		return;

	// The main path has already branched away. The existing conditional
	// labels can now bind directly to their tails, without an island patch.
	for (int k = 0; k < count; k++)
	{
		SuperblockSideExit& exit = s_sideExits[k];
		armAsm->Bind(&*exit.label);
		exit.state.restore();
		g_branch = 0;
		if (exit.needDs)
		{
			pc = exit.dsPc;
			recompileNextInstruction(true, false);
		}
		recEmitSideExitTail(exit.branchTo);
		exit.label.reset();
	}
	s_numSideExits = 0;
}
#endif


// Verbatim port of the x86 EE TrySwapDelaySlot decode/safety table
// (pcsx2/x86/ix86-32/iR5900.cpp:901). This is a pure instruction-decode +
// hazard check with NO codegen — it only decides whether the instruction in
// the branch's delay slot can be safely hoisted ahead of the branch (so its
// result is available when the branch evaluates its condition). Keep this in
// lockstep with the x86 table (comments and all, including the dead `case 64`
// quirk) so it can be diffed line-for-line on future re-syncs; behavioral
// divergence from x86 here is a bug, not an arm64 optimization.
bool TrySwapDelaySlot(u32 rs, u32 rt, u32 rd, bool allow_loadstore)
{
	if (g_recompilingDelaySlot)
		return false;

	bool direct = false;
	const u32 opcode_encoded = readEeCompileInstruction(pc, &direct);
	if (opcode_encoded == 0) // NOP
	{
		recompileEeInstruction(g_pCurInstInfo, true, true, direct ? &opcode_encoded : nullptr, nullptr, nullptr);
		return true;
	}

	const u32 opcode_rs = ((opcode_encoded >> 21) & 0x1F);
	const u32 opcode_rt = ((opcode_encoded >> 16) & 0x1F);
	const u32 opcode_rd = ((opcode_encoded >> 11) & 0x1F);

	switch (opcode_encoded >> 26)
	{
		case 8: // ADDI
		case 9: // ADDIU
		case 10: // SLTI
		case 11: // SLTIU
		case 12: // ANDIU
		case 13: // ORI
		case 14: // XORI
		case 24: // DADDI
		case 25: // DADDIU
		{
			if ((rs != 0 && rs == opcode_rt) || (rt != 0 && rt == opcode_rt) || (rd != 0 && (rd == opcode_rs || rd == opcode_rt)))
				goto is_unsafe;
		}
		break;

		case 26: // LDL
		case 27: // LDR
		case 30: // LQ
		case 31: // SQ
		case 32: // LB
		case 33: // LH
		case 34: // LWL
		case 35: // LW
		case 36: // LBU
		case 37: // LHU
		case 38: // LWR
		case 39: // LWU
		case 40: // SB
		case 41: // SH
		case 42: // SWL
		case 43: // SW
		case 44: // SDL
		case 45: // SDR
		case 46: // SWR
		case 55: // LD
		case 63: // SD
		{
			// We can't allow loadstore swaps for BC0x/BC2x, since they could affect the condition.
			if (!allow_loadstore || (rs != 0 && rs == opcode_rt) || (rt != 0 && rt == opcode_rt) || (rd != 0 && (rd == opcode_rs || rd == opcode_rt)))
				goto is_unsafe;
		}
		break;

		case 15: // LUI
		{
			if ((rs != 0 && rs == opcode_rt) || (rt != 0 && rt == opcode_rt) || (rd != 0 && rd == opcode_rt))
				goto is_unsafe;
		}
		break;

		case 49: // LWC1
		case 57: // SWC1
		case 54: // LQC2
		case 62: // SQC2
			break;

		case 0: // SPECIAL
		{
			switch (opcode_encoded & 0x3F)
			{
				case 0: // SLL
				case 2: // SRL
				case 3: // SRA
				case 4: // SLLV
				case 6: // SRLV
				case 7: // SRAV
				case 10: // MOVZ
				case 11: // MOVN
				case 20: // DSLLV
				case 22: // DSRLV
				case 23: // DSRAV
				case 24: // MULT
				case 25: // MULTU
				case 32: // ADD
				case 33: // ADDU
				case 34: // SUB
				case 35: // SUBU
				case 36: // AND
				case 37: // OR
				case 38: // XOR
				case 39: // NOR
				case 42: // SLT
				case 43: // SLTU
				case 44: // DADD
				case 45: // DADDU
				case 46: // DSUB
				case 47: // DSUBU
				case 56: // DSLL
				case 58: // DSRL
				case 59: // DSRA
				case 60: // DSLL32
				case 62: // DSRL31
				case 64: // DSRA32
				{
					if ((rs != 0 && rs == opcode_rd) || (rt != 0 && rt == opcode_rd) || (rd != 0 && (rd == opcode_rs || rd == opcode_rt)))
						goto is_unsafe;
				}
				break;

				case 15: // SYNC
				case 26: // DIV
				case 27: // DIVU
					break;

				default:
					goto is_unsafe;
			}
		}
		break;

		case 16: // COP0
		{
			switch ((opcode_encoded >> 21) & 0x1F)
			{
				case 0: // MFC0
				case 2: // CFC0
				{
					if ((rs != 0 && rs == opcode_rt) || (rt != 0 && rt == opcode_rt) || (rd != 0 && rd == opcode_rt))
						goto is_unsafe;
				}
				break;

				case 4: // MTC0
				case 6: // CTC0
					break;

				case 16: // TLB (technically would be safe, but we don't use it anyway)
				default:
					goto is_unsafe;
			}
			break;
		}
		break;

		case 17: // COP1
		{
			switch ((opcode_encoded >> 21) & 0x1F)
			{
				case 0: // MFC1
				case 2: // CFC1
				{
					if ((rs != 0 && rs == opcode_rt) || (rt != 0 && rt == opcode_rt) || (rd != 0 && rd == opcode_rt))
						goto is_unsafe;
				}
				break;

				case 4: // MTC1
				case 6: // CTC1
				case 16: // S
				{
					const u32 funct = (opcode_encoded & 0x3F);
					if (funct == 50 || funct == 52 || funct == 54) // C.EQ, C.LT, C.LE
					{
						// affects flags that we're comparing
						goto is_unsafe;
					}
				}
					[[fallthrough]];

				case 20: // W
				{
				}
				break;

				default:
					goto is_unsafe;
			}
		}
		break;

		case 18: // COP2
		{
			switch ((opcode_encoded >> 21) & 0x1F)
			{
				case 8: // BC2XX
					goto is_unsafe;

				case 1: // QMFC2
				case 2: // CFC2
				{
					if ((rs != 0 && rs == opcode_rt) || (rt != 0 && rt == opcode_rt) || (rd != 0 && rd == opcode_rt))
						goto is_unsafe;
				}
				break;

				default:
					break;
			}
		}
		break;

		case 28: // MMI
		{
			switch (opcode_encoded & 0x3F)
			{
				case 8: // MMI0
				case 9: // MMI1
				case 10: // MMI2
				case 40: // MMI3
				case 41: // MMI3
				case 52: // PSLLH
				case 54: // PSRLH
				case 55: // LSRAH
				case 60: // PSLLW
				case 62: // PSRLW
				case 63: // PSRAW
				{
					if ((rs != 0 && rs == opcode_rd) || (rt != 0 && rt == opcode_rd) || (rd != 0 && rd == opcode_rd))
						goto is_unsafe;
				}
				break;

				default:
					goto is_unsafe;
			}
		}
		break;

		default:
			goto is_unsafe;
	}

	recompileEeInstruction(g_pCurInstInfo, true, true, direct ? &opcode_encoded : nullptr, nullptr, nullptr);
	return true;

is_unsafe:
	return false;
}

// =====================================================================================================
//  Memory management and block clearing
// =====================================================================================================

#ifdef PS2_SWITCH2_EE_REDIRECT_BATCH
#if !defined(__SWITCH2__) || defined(PS2_HOST_CODE_BRIDGE)
#error Batched EE redirects require the Switch 2 dual-alias provider
#endif
static bool s_batchEeRedirects = true;
void recSetEeRedirectBatchForTest(bool enabled)
{
	s_batchEeRedirects = enabled;
}
#endif

static void recClear(u32 addr, u32 size)
{
    // Clear's size is a count of guest words. An empty half-open interval
    // cannot overlap a compiled instruction, including when addr lies in one.
    if (size == 0)
    {
        ++s_emptyClearZero;
        return;
    }
	addr = HWADDR(addr);
	const u32 end = addr + size * 4;

#if defined(PCSX2_RECOMPILER_TESTS) && PS2_EE_TEST_BLOCK_METADATA
	// Keep the loop-residency introspection in sync with the live block set.
	for (auto it = s_loopResidentBlocks.begin(); it != s_loopResidentBlocks.end();)
	{
		if (*it >= addr && *it < end)
			it = s_loopResidentBlocks.erase(it);
		else
			++it;
	}
#endif

	if (size <= (UINT32_MAX - addr) / 4 && !eeClearMayHitCode(addr, end))
		return;

#ifdef PS2_SWITCH2_EE_REDIRECT_BATCH
	const bool batch_redirects = s_batchEeRedirects && !CHECK_FASTMEM;
#else
	constexpr bool batch_redirects = false;
#endif
	int blockidx = recBlocks.LastIndex(end - 4);
	if (blockidx == -1)
		return;
    // LastIndex clamps to index0 below the first start. In that case no
    // block starts before end; keep the unrelated first block and its LUT.
    // Only compare a non-wrapping interval; leave existing wrap behavior alone.
    if (end > addr && recBlocks[blockidx]->startpc >= end)
    {
        ++s_emptyClearBelowFirst;
        return;
    }

#ifdef PS2_HOST_CODE_BRIDGE
    // Open a transaction only when the scan finds a block to remove.
    bool owns_clear_publication = false;
    bool checked_clear_publication = false;
#endif

	// No write scope here: the only code writes below are Remove()'s entry
	// stubs, and those open their own per-site scope (armPatchCodeWord).
	// SetFnptr/recLUT targets are ordinary heap data. The arena-wide
	// Begin/EndCodeWrite this used to hold was worse than useless on iOS
	// Legacy mode — the range-window emit scope bypasses the refcount, so
	// when recClear runs mid-compile (the stale-overlap walk in
	// recRecompile) the paired EndCodeWrite RX-flipped the open emit window
	// and the next emitted instruction faulted.

	// Track the EE-address span of all blocks we touch so the post-walk
	// tail can reset interior BLOCKs across the *full* extent of the
	// removed blocks (a straddler can extend well past `end` or below
	// `addr`). `ceiling` clamps the tail at the next surviving block's
	// startpc so we never trample its interior.
	//
	// There is deliberately no matching floor clamp. A survivor skipped by the
	// scan below can sit INSIDE a removed straddler's extent, and raising
	// lowerextent to that survivor's end would leave the straddler's own start
	// word still pointing at its removed stub — the fnptr-assert case
	// StraddlerBlockRecClearResetsStartFnptr pins. Resetting a survivor's entry
	// instead merely costs it a recompile.
	u32 lowerextent = static_cast<u32>(-1);
	u32 upperextent = 0;
	u32 ceiling = static_cast<u32>(-1);

	if (BASEBLOCKEX* peb_above = recBlocks[blockidx + 1])
		ceiling = peb_above->startpc;

	int toRemoveLast = blockidx;

	// Walk down through blocks overlapping [addr, end). For each, reset
	// BLOCK->fnptr at the block's actual start (the straddle-from-below
	// case is load-bearing — Arm64BaseBlocks::Remove() patches only the
	// compiled-code stub, so any BLOCK->fnptr left pointing at a stub
	// trips the recRecompile fnptr assertion on the next dispatch).
	//
	// Skip s_pCurBlock if we hit it: it's the block currently being
	// compiled, and yanking it mid-emit corrupts the in-progress block.
	// Splitting the Remove range around it preserves it. Mirrors x86
	// recClear (pcsx2/x86/ix86-32/iR5900.cpp:786).
	while (BASEBLOCKEX* pexblock = recBlocks[blockidx])
	{
        ++s_clearMetadataVisits;
		const u32 blockstart = pexblock->startpc;
		const u32 blockend = blockstart + pexblock->size * 4;
		BASEBLOCK* pblock = GETBLOCK(blockstart);

		if (pblock == s_pCurBlock)
		{
			if (toRemoveLast != blockidx)
				recBlocks.Remove(blockidx + 1, toRemoveLast, batch_redirects);
			toRemoveLast = --blockidx;
			continue;
		}

		if (blockend <= addr)
		{
			// Not overlapping — but recBlocks is ordered by startpc, NOT by
			// end address, so a block further down can still be long enough
			// to reach [addr, end); this one is no proof the rest miss too.
			// Keep it, splitting the pending remove range around it exactly
			// as for s_pCurBlock, and carry on until even the longest block
			// compiled since the last reset could not span the gap.
			if (blockstart + s_maxBlockBytes <= addr)
				break;

#if PS2_EE_CLEAR_BUCKET_INDEX && PS2_EE_OVERLAP_BUCKET_INDEX
            // The completed-block index is a monotonic upper bound. A bucket
            // ending at/below addr cannot contain an overlapping block. Keep
            // the original global reach stop above this jump (R161), and do
            // not change legacy wrapping intervals or unindexed ROM ranges.
            if (end > addr && blockstart < Ps2MemSize::TotalRam &&
                s_overlapUpperEnd[blockstart >> kEeOverlapBucketShift] <= addr)
            {
                ++s_clearBucketsSkipped;
                // Remove the already examined overlapping suffix before
                // jumping over survivors. Erasing above blockidx leaves all
                // lower indices stable; no surviving LUT entry is restored.
                if (toRemoveLast != blockidx)
                    recBlocks.Remove(blockidx + 1, toRemoveLast, batch_redirects);
                toRemoveLast = blockidx;
                const u32 floor = (blockstart >> kEeOverlapBucketShift) << kEeOverlapBucketShift;
                if (floor == 0)
                    break;
                const int previous = recBlocks.LastIndex(floor - 4);
                // LastIndex clamps to0 below the first start. Never revisit
                // that first block when its entire bucket is known to miss.
                if (previous >= blockidx)
                    break;
                blockidx = previous;
                toRemoveLast = blockidx;
                continue;
            }
#endif
			if (toRemoveLast != blockidx)
				recBlocks.Remove(blockidx + 1, toRemoveLast, batch_redirects);
			toRemoveLast = --blockidx;
			continue;
		}

#ifdef PS2_HOST_CODE_BRIDGE
		if (!checked_clear_publication)
		{
			checked_clear_publication = true;
			owns_clear_publication = !ps2_code_host_ee_transaction_active() &&
				ps2_mtvu_caller_is_ee() && ps2_mtvu_code_host_caller_idle() &&
				!ps2ArmCodeWriteWindowActive();
			if (owns_clear_publication)
				armBeginEEPublicationTransaction(SysMemory::GetEERec(),
					static_cast<size_t>(SysMemory::GetEERecEnd() - SysMemory::GetEERec()));
		}
#endif
		lowerextent = std::min(lowerextent, blockstart);
		upperextent = std::max(upperextent, blockend);
		pblock->SetFnptr((uptr)JITCompile);

		--blockidx;
	}

	if (toRemoveLast != blockidx)
		recBlocks.Remove(blockidx + 1, toRemoveLast, batch_redirects);

	upperextent = std::min(upperextent, ceiling);

#ifdef PCSX2_DEVBUILD
	// The walk above must leave no surviving block overlapping [addr, end).
	// Upstream x86 asserts the same thing and calls it "Impossible block
	// clearing failure" (iR5900.cpp) — and it was reachable there, because the
	// same commit that added the check (801d71f7f0, 2009) also added the
	// startpc-ordered early break that lets a straddler-from-below slip past.
	//
	// Upstream rescans every block in the array; bound it instead the way the
	// walk itself is bounded. A survivor overlapping [addr, end) has to start
	// within s_maxBlockBytes below addr, so the window is the same size the
	// walk already covers rather than O(live blocks) on every clear.
	{
		const u32 floor = (addr > s_maxBlockBytes) ? (addr - s_maxBlockBytes) : 0;
		for (int i = std::max(0, recBlocks.LastIndex(floor)); BASEBLOCKEX* peb = recBlocks[i]; i++)
		{
			if (peb->startpc >= end)
				break;
			if (GETBLOCK(peb->startpc) == s_pCurBlock)
				continue; // spared on purpose — it is mid-compile

			const u32 peb_end = peb->startpc + peb->size * 4;
			if (peb_end > addr) [[unlikely]]
			{
				Console.Error("[EE] Impossible block clearing failure: block %08X..%08X survived clear of %08X..%08X",
					peb->startpc, peb_end, addr, end);
				pxFail("[EE] Impossible block clearing failure");
			}
		}
	}
#endif

	// Reset interior BLOCKs across the full removed-block extent. Without
	// this, interior fnptrs of straddler blocks can stay non-JITCompile
	// from a prior compilation, leading to wrong dispatch on a later JR
	// into the middle of a freshly-recompiled block.
	if (upperextent > lowerextent)
		iopClearRecLUT(GETBLOCK(lowerextent), upperextent - lowerextent);
#ifdef PS2_HOST_CODE_BRIDGE
    if (owns_clear_publication)
        armCommitEEPublicationTransaction();
#endif
}

static void iopClearRecLUT(BASEBLOCK* base, int count)
{
	for (int i = 0; i < count / 4; i++)
		base[i].SetFnptr((uptr)JITCompile);
}

static void dyna_block_discard(u32 start, u32 sz)
{
	eeWriteShadowRecordDiscard();
	DevCon.WriteLn("%.8X rec block discard (sz=%d)", start, sz);
	recClear(start, sz);
}

static void dyna_page_reset(u32 start, u32 sz)
{
	recClear(start & ~0xFFF, 0x400); // clear 4KB page
	manual_counter[start >> 12]++;
	mmap_MarkCountedRamPage(start);
}

#ifdef __SWITCH2__
static bool s_ee_smc_live_literal;
static bool s_ee_smc_inline;
static bool s_ee_smc_addr_pair;

void recConfigureEeSmcAddressPairTrial(bool enabled)
{
	s_ee_smc_addr_pair = enabled;
}

void recConfigureEeSmcInlineTrial(bool enabled)
{
	s_ee_smc_inline = enabled;
}

void recConfigureEeSmcLiteralTrial(bool enabled)
{
	s_ee_smc_live_literal = enabled;
}
#endif

static void emitEeSmcLiveAddress(const u8* live)
{
#ifdef __SWITCH2__
	if (s_ee_smc_live_literal && !armAddressRecorder)
	{
		// Keep short register-only forms. The native A/B measures the cost
		// of substituting a literal load for a longer address sequence.
		const uptr address = reinterpret_cast<uptr>(live);
		const s64 page_displacement = static_cast<s64>(address >> 12) -
			static_cast<s64>(reinterpret_cast<uptr>(armGetCurrentCodePointer()) >> 12);
		if (!vixl::IsInt21(page_displacement) &&
			a64::MacroAssembler::MoveImmediateHelper(nullptr, RSCRATCHADDR, address) >= 3)
		{
			armAsm->Ldr(RSCRATCHADDR, static_cast<u64>(address));
			return;
		}
	}
#endif
#ifdef IZPS2_EE_SMC_FAR_LIVE_LITERAL
	if (!armAddressRecorder)
	{
		// Do not trade an ADRP or a short MOV sequence for a dependent
		// load. Only replace expensive far addresses.
		const uptr address = reinterpret_cast<uptr>(live);
		const s64 displacement = static_cast<s64>(address >> 12) -
			static_cast<s64>(reinterpret_cast<uptr>(armGetCurrentCodePointer()) >> 12);
		constexpr s64 edge = (s64{1} << 20) + 256;
		if ((displacement < -edge || displacement >= edge) &&
			a64::MacroAssembler::MoveImmediateHelper(nullptr, RSCRATCHADDR, address) >= 3)
		{
			armAsm->Ldr(RSCRATCHADDR, static_cast<u64>(address));
			return;
		}
	}
#endif
	armMoveAddressToReg(RSCRATCHADDR, live);
}

// Only replace far live and frozen addresses together.
// Both PC-relative loads are independent. The exact compare still reads
// the current guest bytes and the original immutable snapshot.
static bool emitEeSmcAddressPair(const u8* live, const u8* expected)
{
#ifdef __SWITCH2__
	if (!s_ee_smc_addr_pair || s_ee_smc_live_literal || armAddressRecorder)
		return false;
	const auto live_address = reinterpret_cast<uptr>(live);
	const auto expected_address = reinterpret_cast<uptr>(expected);
	const s64 pc_page = reinterpret_cast<uptr>(armGetCurrentCodePointer()) >> 12;
	constexpr s64 near_limit = (s64{1} << 20) + 256;
	const auto outside_adrp = [pc_page](uptr address) {
		const s64 delta = static_cast<s64>(address >> 12) - pc_page;
		return delta < -near_limit || delta >= near_limit;
	};
	if (!outside_adrp(live_address) || !outside_adrp(expected_address))
		return false;
	const int live_cost = a64::MacroAssembler::MoveImmediateHelper(nullptr, RSCRATCHADDR, live_address);
	const int expected_cost = a64::MacroAssembler::MoveImmediateHelper(nullptr, RXSCRATCH, expected_address);
	if (live_cost < 2 || expected_cost < 2 || live_cost + expected_cost < 5)
		return false;
	armAsm->Ldr(RSCRATCHADDR, static_cast<u64>(live_address));
	armAsm->Ldr(RXSCRATCH, static_cast<u64>(expected_address));
	return true;
#else
	return false;
#endif
}

// Embed short exact snapshots when their constants take no more
// instructions than materializing and reading the snapshot pointer.
static bool emitShortSmcCompare(const u8* live, u32 bytes)
{
#ifdef __SWITCH2__
	const bool use_literals = s_ee_smc_live_literal && !armAddressRecorder;
#else
	constexpr bool use_literals = false;
#endif
	if (bytes == 4)
	{
		u32 expected;
		std::memcpy(&expected, live, sizeof(expected));
		emitEeSmcLiveAddress(live);
		armAsm->Ldr(a64::w9, a64::MemOperand(RSCRATCHADDR));
		if (use_literals && !a64::Assembler::IsImmAddSub(expected) &&
		    a64::MacroAssembler::MoveImmediateHelper(nullptr, a64::w2, expected) > 1)
		{
			armAsm->Ldr(a64::w2, expected);
			armAsm->Cmp(a64::w9, a64::w2);
		}
		else
			armAsm->Cmp(a64::w9, expected);
		return true;
	}
	if (bytes != 8 && bytes != 12)
		return false;

	u64 first;
	u32 tail = 0;
	std::memcpy(&first, live, sizeof(first));
	if (bytes == 12)
		std::memcpy(&tail, live + sizeof(first), sizeof(tail));
	const int first_cost = a64::MacroAssembler::MoveImmediateHelper(nullptr, a64::x2, first);
	const int tail_cost = tail <= 31 ? 0 : a64::MacroAssembler::MoveImmediateHelper(nullptr, a64::w2, tail);
	// A short raw literal in the emitted block avoids publishing a
	// separate snapshot. Recorded addresses keep their existing path.
	if (!use_literals && first_cost + tail_cost > (bytes == 8 ? 2 : 3))
		return false;

	emitEeSmcLiveAddress(live);
	armAsm->Ldr(a64::x9, a64::MemOperand(RSCRATCHADDR));
	if (use_literals && first_cost > 1)
		armAsm->Ldr(a64::x2, first);
	else
		armAsm->Mov(a64::x2, first);
	armAsm->Cmp(a64::x9, a64::x2);
	if (bytes == 12)
	{
		armAsm->Ldr(a64::w9, a64::MemOperand(RSCRATCHADDR, 8));
		if (tail <= 31)
			armAsm->Ccmp(a64::w9, tail, a64::NoFlag, a64::eq);
		else
		{
			if (use_literals && tail_cost > 1)
				armAsm->Ldr(a64::w2, tail);
			else
				armAsm->Mov(a64::w2, tail);
			armAsm->Ccmp(a64::w9, a64::w2, a64::NoFlag, a64::eq);
		}
	}
	return true;
}

// The literal pool contains the frozen words themselves. Checking small
// spans uses the original live address and exactly the same guest bytes.
static bool emitEeSmallLiteralSmcCompare(const u8* live, u32 bytes)
{
#if defined(IZPS2_SWITCH2_SHORT_SMC_EXPECTATIONS) || (defined(__SWITCH2__) && !defined(PS2_HOST_CODE_BRIDGE))
	if (bytes < 8 || bytes > 28 || (bytes & 3) || armAddressRecorder)
		return false;
#ifdef __SWITCH2__
	if (s_ee_smc_inline || s_ee_smc_addr_pair || s_ee_smc_live_literal)
		return false;
#endif

	emitEeSmcLiveAddress(live);
	bool first = true;
	u32 offset = 0;
	const auto compare = [&](const a64::Register& actual, const a64::Register& frozen, u64 expected) {
		if (first && a64::Assembler::IsImmAddSub(expected))
			armAsm->Cmp(actual, expected);
		else if (!first && expected <= 31)
			armAsm->Ccmp(actual, expected, a64::NoFlag, a64::eq);
		else
		{
			// A one-instruction value needs no literal-pool access.
			if (a64::MacroAssembler::MoveImmediateHelper(nullptr, frozen, expected) == 1)
				armAsm->Mov(frozen, expected);
			else
				armAsm->Ldr(frozen, expected);
			if (first)
				armAsm->Cmp(actual, frozen);
			else
				armAsm->Ccmp(actual, frozen, a64::NoFlag, a64::eq);
		}
		first = false;
	};
	while (bytes - offset >= 16)
	{
		u64 low, high;
		std::memcpy(&low, live + offset, sizeof(low));
		std::memcpy(&high, live + offset + sizeof(low), sizeof(high));
		armAsm->Ldp(a64::x9, a64::x10, a64::MemOperand(RSCRATCHADDR, offset));
		compare(a64::x9, a64::x2, low);
		compare(a64::x10, a64::x3, high);
		offset += 16;
	}
	if (bytes - offset >= 8)
	{
		u64 expected;
		std::memcpy(&expected, live + offset, sizeof(expected));
		armAsm->Ldr(a64::x9, a64::MemOperand(RSCRATCHADDR, offset));
		compare(a64::x9, a64::x2, expected);
		offset += 8;
	}
	if (offset != bytes)
	{
		u32 expected;
		std::memcpy(&expected, live + offset, sizeof(expected));
		armAsm->Ldr(a64::w9, a64::MemOperand(RSCRATCHADDR, offset));
		compare(a64::w9, a64::w2, expected);
	}
	return true;
#else
	return false;
#endif
}

// Small immutable snapshots can share the block's code lifetime. A
// nearby unconditional discard transfer skips the snapshot on mismatch;
// the matching path jumps straight over both to the block body.
struct EeInlineSmcSnapshot
{
	static constexpr u32 kMaxBytes = 32;
	a64::Label location;
	std::array<u32, kMaxBytes / sizeof(u32)> data;
	u32 bytes = 0;

	bool Capture(const u8* live, u32 size)
	{
#ifndef __SWITCH2__
		return false;
#else
		if (!s_ee_smc_inline || armAddressRecorder || size < 8 || size > kMaxBytes || (size & 3))
			return false;
		std::memcpy(data.data(), live, size);
		bytes = size;
		return true;
#endif
	}

	void Emit()
	{
		if (!bytes)
			return;
		vixl::EmissionCheckScope reserve(armAsm, bytes + sizeof(u32));
		if (armAsm->GetCursorOffset() & 7)
			armAsm->dc32(0);
		armAsm->Bind(&location);
		for (u32 i = 0; i < bytes / sizeof(u32); ++i)
			armAsm->dc32(data[i]);
	}
};

// Self-modifying code detection — generates inline memory comparison checks
// for blocks in manually-protected pages, and sets up page protection for new pages.
// Port of x86 memory_protect_recompiled_code(). Returns true when the block
// got an inline manual SMC check — such blocks are excluded from SL-1 loop
// residency (the entry check must run every iteration).
#if PS2_EE_MANUAL_SMC_SIMD_ENTRY
// Exact 48–60 byte entry checks. For the last partial vector read the
// final 16 bytes, overlapping earlier bytes without accessing either end
// of the range out of bounds. All eight vector registers are free at entry.
static bool emitEeMediumSnapshotDifference(u32 bytes)
{
	if (bytes < 48 || bytes >= 64 || (bytes & 3))
		return false;

	const a64::VRegister lhs0(0, 128), lhs1(1, 128), lhs2(2, 128), lhs3(3, 128);
	const a64::VRegister rhs0(4, 128), rhs1(5, 128), rhs2(6, 128), rhs3(7, 128);
	armAsm->Ld1(lhs0.V16B(), lhs1.V16B(), lhs2.V16B(), a64::MemOperand(RSCRATCHADDR));
	armAsm->Ld1(rhs0.V16B(), rhs1.V16B(), rhs2.V16B(), a64::MemOperand(RXSCRATCH));
	if (bytes != 48)
	{
		armAsm->Ldr(lhs3, a64::MemOperand(RSCRATCHADDR, bytes - 16));
		armAsm->Ldr(rhs3, a64::MemOperand(RXSCRATCH, bytes - 16));
	}
	armAsm->Eor(lhs0.V16B(), lhs0.V16B(), rhs0.V16B());
	armAsm->Eor(lhs1.V16B(), lhs1.V16B(), rhs1.V16B());
	armAsm->Eor(lhs2.V16B(), lhs2.V16B(), rhs2.V16B());
	if (bytes != 48)
	{
		armAsm->Eor(lhs3.V16B(), lhs3.V16B(), rhs3.V16B());
		armAsm->Orr(lhs2.V16B(), lhs2.V16B(), lhs3.V16B());
	}
	armAsm->Orr(lhs0.V16B(), lhs0.V16B(), lhs1.V16B());
	armAsm->Orr(lhs0.V16B(), lhs0.V16B(), lhs2.V16B());
	armAsm->Umaxv(lhs0.S(), lhs0.V4S());
	armAsm->Fmov(a64::w9, lhs0.S());
	return true;
}
#endif

#ifdef IZPS2_EE_SMC_EARLY_EXIT
static bool emitEeSmcEarlyExit(u32 startpc, const u8* live, u32 bytes,
	a64::Label* mismatch, a64::Label* accepted)
{
	if (bytes < 64 || armAddressRecorder || s_numContSites == 0 || EmuConfig.Trace.Enabled ||
		!CBreakPoints::GetBreakpoints(BREAKPOINT_EE, true).empty() || CBreakPoints::GetNumMemchecks() ||
		(EmuConfig.EnablePatches && ps2_patch_has_active_dynamic_patches()))
		return false;

	u32 words[3];
	std::memcpy(words, live, sizeof(words));
	const auto rule = DecodeEeSmcExit(words, startpc, s_nEndBlock);
	if (!rule || s_contSitePcs[0] != startpc + (rule->prefixWords - 2) * 4)
		return false;
#ifdef PCSX2_RECOMPILER_TESTS
	if (std::any_of(words, words + rule->prefixWords, EERecFallback::Selected))
		return false;
#endif

	// Check every instruction that the taken path can execute. The
	// duplicate predicate has no guest side effects. The unchanged
	// superblock still executes its prefix and charges its normal cycles.
	emitEeSmcLiveAddress(live);
	EmitEeSmcExitPrefix(*armAsm, RSCRATCHADDR, words, rule->prefixWords, mismatch);
	const auto gpr = [](unsigned guest, const a64::Register& scratch) {
		if (guest == 0)
			return a64::xzr;
		if (const auto* pin = armEEPinForGPR(guest))
			return *pin;
		armAsm->Ldr(scratch, armCpuRegMem(&cpuRegs.GPR.r[guest].UD[0]));
		return scratch;
	};
	const auto source = gpr(rule->source, a64::x9);
	const auto compare = gpr(rule->compare, a64::x10);
	EmitEeSmcExitPredicate(*armAsm, *rule, source, compare, accepted);
	return true;
}
#endif


static bool memory_protect_recompiled_code(u32 startpc, u32 size, bool needs_loop_snapshot, a64::Label* discard,
	EeInlineSmcSnapshot& inline_snapshot)
{
	s_manualLoopSmc = {};
	u32 inpage_ptr = HWADDR(startpc);
	const u32 inpage_sz = size * 4;

	// The kernel context register is stored @ 0x800010C0-0x80001300
	// The EENULL thread context register is stored @ 0x81000-....
	const bool contains_thread_stack = ((startpc >> 12) == 0x81) || ((startpc >> 12) == 0x80001);

	// Native host exception ownership is preserved. Tracked RAM must use
	// the existing snapshot/dispatch invalidator without write-fault recovery.
	// ROM/untracked pages retain their original NotRequired behavior.
	const vtlb_ProtectionMode queried_page_type = mmap_GetRamPageInfo(inpage_ptr);
	const bool force_manual_ram = queried_page_type != ProtMode_NotRequired;
	const vtlb_ProtectionMode PageType = force_manual_ram ? ProtMode_Manual : queried_page_type;

	switch (PageType)
	{
		case ProtMode_NotRequired:
			break;

		case ProtMode_None:
		case ProtMode_Write:
			mmap_MarkCountedRamPage(inpage_ptr);
			manual_page[inpage_ptr >> 12] = 0;
			break;

		case ProtMode_Manual:
		{
			// FX-03a fast compare (mirrors the AetherSX2 leak's hoisted-base
			// wide-compare shape, module 1030, which strides 8 bytes; we take
			// it to 16): snapshot the source words into the constant pool,
			// hoist both base addresses once, then Ldp 16-byte pairs through
			// a single Cmp/Ccmp flag chain with ONE exit branch. ~1.3
			// insns/word vs ~8 for the per-word fallback below. Post-index
			// addressing dodges the Ldp imm7 range limit on page-size blocks.
			// The snapshot must be OUR frozen copy — comparing against
			// recRAMCopy would alias later recompiles of overlapping blocks.
			const auto* live = static_cast<const u8*>(PSM(inpage_ptr));
			const bool direct_main = force_manual_ram && inpage_ptr < Ps2MemSize::ExposedRam &&
				inpage_sz <= Ps2MemSize::ExposedRam - inpage_ptr && live == &eeMem->Main[inpage_ptr];
			if (direct_main && !needs_loop_snapshot && inpage_sz &&
				inpage_sz <= vtlb_private::VTLB_PAGE_SIZE - (inpage_ptr & vtlb_private::VTLB_PAGE_MASK))
			{
				eeWriteShadowMarkCandidate(inpage_ptr);
				if (eeAllowEagerCode() && !armAddressRecorder)
				{
					recEeObserveCodeSpan(live, inpage_sz);
					break;
				}
			}
			if (!needs_loop_snapshot && direct_main && ((inpage_sz <= 12 && emitShortSmcCompare(live, inpage_sz)) ||
				emitEeSmallLiteralSmcCompare(live, inpage_sz)))
			{
				armAsm->B(discard, a64::ne);
				break;
			}
#if PS2_EE_MANUAL_SMC_SIMD_ENTRY
			if (!needs_loop_snapshot && direct_main && !armAddressRecorder &&
				inpage_sz >= 32 && inpage_sz <= 44
#ifdef __SWITCH2__
				&& !s_ee_smc_inline && !s_ee_smc_addr_pair && !s_ee_smc_live_literal
#endif
				)
			{
				emitEeSmcLiveAddress(live);
				EmitEeSmcVectorLiteral(*armAsm, RSCRATCHADDR, live, inpage_sz, a64::w9);
				armAsm->Cbnz(a64::w9, discard);
				break;
			}
#endif
			const bool local_snapshot = !needs_loop_snapshot && direct_main && inline_snapshot.Capture(live, inpage_sz);
			const u8* expected_blob = local_snapshot ? nullptr : recImmutableSmcSnapshot(live, inpage_sz);
			if (needs_loop_snapshot && force_manual_ram && expected_blob &&
				manualLoopSmcSpanTracked(startpc, inpage_ptr, inpage_sz, live))
			{
				s_manualLoopSmc = {expected_blob, live,
					inpage_ptr, inpage_sz, true};
			}
			if (local_snapshot || expected_blob)
			{
#ifdef IZPS2_EE_SMC_EARLY_EXIT
				a64::Label short_path;
				const bool early_exit = !local_snapshot && !needs_loop_snapshot && direct_main &&
					emitEeSmcEarlyExit(startpc, live, inpage_sz, discard, &short_path);
#endif
				// No pool can intervene between the ADR and inline raw data.
				// A 32-byte compare, discard transfer and payload fit within 160 bytes.
				vixl::EmissionCheckScope near_snapshot(local_snapshot ? armAsm : nullptr, 160);
				// Clobbers x2/x3/x8/x9/x10/x17 — all dead at block head.
				if (local_snapshot || !emitEeSmcAddressPair(live, expected_blob))
				{
					emitEeSmcLiveAddress(live);
					if (local_snapshot)
						armAsm->Adr(RXSCRATCH, &inline_snapshot.location);
					else
						armMoveAddressToReg(RXSCRATCH, expected_blob);
				}
				u32 rem = inpage_sz;
				bool first = true;
				bool vectorResult = false;
#if PS2_EE_MANUAL_SMC_SIMD_ENTRY
				if (force_manual_ram && emitEeMediumSnapshotDifference(rem))
				{
					rem = 0;
					vectorResult = true;
				}
				// All external entries have no live allocator NEON values:
				// linked/indirect/return/fallthrough tails FREE_XMM, and the
				// dispatcher C seams carry no NEON allocations. Loop-resident
				// backedges target the later loop-top, never this check. q0/q1
				// through q7 are therefore dead here; q29-q31 are reserved scratches.
				// Pinned q8/q9/q11 and lazy clamp q25/q26 remain untouched.
				// Use local register descriptors, not new TU-static objects.
				// Two 32 B chunks amortize the three-instruction reduction.
				// At 64 B the compare body takes 14 instructions instead of 16.
				if (force_manual_ram && rem >= 64 && (rem & 3) == 0)
				{
					const a64::VRegister lhs0(0, 128), lhs1(1, 128);
					const a64::VRegister accum(29, 128), rhs0(30, 128), rhs1(31, 128);
					const auto compare_pair = [&](bool initialize)
					{
						// Never read beyond either frozen span: only full 32 B
						// chunks, then the unchanged 16/8/4 B remainder below.
						armAsm->Ldp(lhs0, lhs1, a64::MemOperand(RSCRATCHADDR, 32, a64::PostIndex));
						armAsm->Ldp(rhs0, rhs1, a64::MemOperand(RXSCRATCH, 32, a64::PostIndex));
						armAsm->Eor(lhs0.V16B(), lhs0.V16B(), rhs0.V16B());
						armAsm->Eor(lhs1.V16B(), lhs1.V16B(), rhs1.V16B());
						if (initialize)
							armAsm->Orr(accum.V16B(), lhs0.V16B(), lhs1.V16B());
						else
						{
							armAsm->Orr(lhs0.V16B(), lhs0.V16B(), lhs1.V16B());
							armAsm->Orr(accum.V16B(), accum.V16B(), lhs0.V16B());
						}
					};
					// Cap generated code for long checks. Four 64 B groups per
					// iteration amortize the counter and backward branch.
					constexpr u32 chunk_bytes = 256;
					if (rem >= 1024)
					{
						a64::Label compare_chunk;
						armAsm->Movi(accum.V16B(), 0);
						armAsm->Mov(a64::w3, rem / chunk_bytes);
						armAsm->Bind(&compare_chunk);
						for (u32 offset = 0; offset < chunk_bytes; offset += 64)
							emitEeSnapshotDifference64(false);
						armAsm->Sub(a64::w3, a64::w3, 1);
						armAsm->Cbnz(a64::w3, &compare_chunk);
						rem %= chunk_bytes;
						first = false;
					}
					while (rem >= 64)
					{
						emitEeSnapshotDifference64(first);
						first = false;
						rem -= 64;
					}
					if (rem >= 32)
					{
						compare_pair(first);
						first = false;
						rem -= 32;
					}
#ifdef IZPS2_EE_SMC_VECTOR_TAIL
					if (EmitEeSmcVectorTail(*armAsm, RSCRATCHADDR, RXSCRATCH, accum, rem))
						rem = 0;
#endif
					// OR of XOR differences cannot cancel. Integer reduction
					// and bitwise FMOV do not modify guest FPR/FPSR/FPCR.
					armAsm->Umaxv(accum.S(), accum.V4S());
					armAsm->Fmov(a64::w9, accum.S());
					vectorResult = rem == 0;
					if (!vectorResult)
						armAsm->Cmp(a64::w9, 0); // CCMP tails preserve a prior mismatch.
				}
#endif
				// Small scalar spans fit in the immediate fields. Keep
				// the two base registers fixed across their loads.
				const bool fixed_base = rem < 64;
				u32 offset = 0;
				const auto address = [&](const a64::Register& base, u32 width)
				{
					return fixed_base ? a64::MemOperand(base, offset) :
						a64::MemOperand(base, width, a64::PostIndex);
				};
				while (rem >= 16)
				{
					armAsm->Ldp(a64::x9, a64::x10, address(RSCRATCHADDR, 16));
					armAsm->Ldp(a64::x2, a64::x3, address(RXSCRATCH, 16));
					if (first)
						armAsm->Cmp(a64::x9, a64::x2);
					else
						armAsm->Ccmp(a64::x9, a64::x2, a64::NoFlag, a64::eq);
					armAsm->Ccmp(a64::x10, a64::x3, a64::NoFlag, a64::eq);
					first = false;
					if (fixed_base)
						offset += 16;
					rem -= 16;
				}
				if (rem >= 8)
				{
					armAsm->Ldr(a64::x9, address(RSCRATCHADDR, 8));
					armAsm->Ldr(a64::x2, address(RXSCRATCH, 8));
					if (first)
						armAsm->Cmp(a64::x9, a64::x2);
					else
						armAsm->Ccmp(a64::x9, a64::x2, a64::NoFlag, a64::eq);
					first = false;
					if (fixed_base)
						offset += 8;
					rem -= 8;
				}
				if (rem)
				{
					armAsm->Ldr(a64::w9, a64::MemOperand(RSCRATCHADDR, offset));
					armAsm->Ldr(a64::w2, a64::MemOperand(RXSCRATCH, offset));
					if (first)
						armAsm->Cmp(a64::w9, a64::w2);
					else
						armAsm->Ccmp(a64::w9, a64::w2, a64::NoFlag, a64::eq);
				}
				if (local_snapshot)
				{
					a64::Label matched;
					if (vectorResult)
						armAsm->Cbz(a64::w9, &matched);
					else
						armAsm->B(&matched, a64::eq);
					armAsm->Mov(a64::w0, inpage_ptr);
					armAsm->Mov(a64::w1, size);
					armEmitJmp(DispatchBlockDiscard);
					inline_snapshot.Emit();
					armAsm->Bind(&matched);
				}
				else
				{
					if (vectorResult)
						armAsm->Cbnz(a64::w9, discard);
					else
						armAsm->B(discard, a64::ne);
				}
#ifdef IZPS2_EE_SMC_EARLY_EXIT
				if (early_exit)
					armAsm->Bind(&short_path);
#endif
			}
			else
			{
				// Pool full — per-word fallback (the original emission).
				u32 lpc = inpage_ptr;
				u32 stg = inpage_sz;

				while (stg > 0)
				{
					const u32 expected = *(u32*)PSM(lpc);

					// Load current memory word
					armMoveAddressToReg(RSCRATCHADDR, (void*)PSM(lpc));
					armAsm->Ldr(RWSCRATCH, a64::MemOperand(RSCRATCHADDR));

					// Compare with compile-time snapshot
					armAsm->Mov(a64::w9, expected);
					armAsm->Cmp(RWSCRATCH, a64::w9);
					armAsm->B(discard, a64::ne);

					stg -= 4;
					lpc += 4;
				}
			}

			// Counted blocks: track how often this block runs. If the counter overflows,
			// reset the page to write-protected mode (faster than manual checks).
			if (!force_manual_ram && !contains_thread_stack && manual_counter[inpage_ptr >> 12] <= 3)
			{
				armMoveAddressToReg(RSCRATCHADDR, &manual_page[inpage_ptr >> 12]);
				armAsm->Ldrh(RWSCRATCH, a64::MemOperand(RSCRATCHADDR));
				armAsm->Add(RWSCRATCH, RWSCRATCH, size);
				armAsm->Strh(RWSCRATCH, a64::MemOperand(RSCRATCHADDR));
				// Check for u16 overflow (bit 16+ set means wrapped past 0xFFFF)
				armAsm->Tst(RWSCRATCH, 0xFFFF0000u);
				armEmitCondBranch(a64::ne, DispatchPageReset);
			}
			break;
		}
	}

	return PageType == ProtMode_Manual;
}

// =====================================================================================================
//  Reserve / Reset / Shutdown / Execute
// =====================================================================================================

// Guest RAM the LUT, recRAM and recRAMCopy below were last sized for (Ps2MemSize::ExposedRam):
// 32 MB, or 128 MB while a Namco System 246/256 arcade game runs (memSetExtraMemMode).
static u32 s_reservedRam = 0;
static bool s_eeAddressMapReady = false;
static const void* s_eeUnmappedDefault = nullptr;

static void recReserveRAM()
{
	s_reservedRam = Ps2MemSize::ExposedRam;
	s_eeAddressMapReady = false;
	s_eeUnmappedDefault = nullptr;
	recLutEntries = (Ps2MemSize::ExposedRam + Ps2MemSize::Rom + Ps2MemSize::Rom1 + Ps2MemSize::Rom2) / 4;

	if (recLutReserve_RAM.size() != recLutEntries)
		recLutReserve_RAM.resize(recLutEntries);

	// A new allocation has no initialized dispatch entries, even when
	// it reuses the same address as the previous allocation.
	s_eeLutDefault = nullptr;
	const size_t pages = (recLutEntries + kEeLutSlotsPerPage - 1) / kEeLutSlotsPerPage;
	s_eeLutDirtyPages.assign((pages + 63) / 64, 0);

	recLutUnmapped.resize(_64kb / 4);

	BASEBLOCK* curpos = recLutReserve_RAM.data();
	recRAM = curpos;
	curpos += (Ps2MemSize::ExposedRam / 4);
	recROM = curpos;
	curpos += (Ps2MemSize::Rom / 4);
	recROM1 = curpos;
	curpos += (Ps2MemSize::Rom1 / 4);
	recROM2 = curpos;
	curpos += (Ps2MemSize::Rom2 / 4);

	// ExposedRam, like everything else that covers guest RAM here: recLutEntries and the recRAM
	// advance above, the alias mask and page loop in recResetRaw, and manual_page / manual_counter
	// (x86 iR5900.cpp:564-577 does the same). They have to move together: a buffer wider than the
	// LUT would hand two blocks aliased to one LUT entry two different snapshots.
	if (recRAMCopy.size() != Ps2MemSize::ExposedRam)
	{
		recRAMCopy.resize(Ps2MemSize::ExposedRam);
		std::memset(recRAMCopy.data(), 0, recRAMCopy.size());
		EE_SNAPSHOT_RESET_BYTES(recRAMCopy.size());
	}
}

static void recReserve()
{
	Console.WriteLn(Color_Green, "EE: ARM64 Recompiler reserved.");
#if PS2_EE_SOFTMEM_COLD_OOL && defined(PS2_HOST_CODE_BRIDGE)
	s_softmemColdStats = {};
	s_softmemColdGenerationBytes = 0;
	recClearSoftmemColdVeneers();
	s_softmemColdHotOpen = false;
#endif

	// 256KB: the FX-03a manual-check snapshots are blobs, not dedup'd
	// literals — a manual-heavy title banks ~70-80KB of them per rec
	// generation (SotC 17k words, UYA 19k). Overflow falls back to the
	// per-word check emission, so this is a soft ceiling.
	const u32 poolSize = 2 * _1mb;
	u8* poolBase = SysMemory::GetEERecEnd() - poolSize;
	s_eeSmcSnapshots.Reset();
	s_eeSmcStorage.Reset();
	s_eeConstantPool.Init(poolBase, poolSize);

	// Code region: everything below the pool, minus the 64KB slop a single
	// compile may overhang past recPtrEnd (armSetAsmPtr grants capacity
	// recPtrEnd - recPtr + 64KB; the cache-full reset triggers on the NEXT
	// compile). recPtrEnd must sit BELOW poolBase by the slop, or a block
	// compiled near the full mark overwrites the pool — whose first bytes
	// are the dispatcher stubs' far-call veneers (bl → mov x16/../br), so
	// the corruption fires on the next event dispatch, far from the cause.
	// (Latent since the pool moved to the cache tail; surfaced by SL-03
	// superblocks growing per-block emission enough for the fuzz soak to
	// fill the cache into the overlap.)
	// SL-10: cold side-exit arena between the hot code region and the pool.
	// Same slop discipline as the hot region: a session's capacity grant is
	// (end - ptr + 64KB), so each region's ptr-end sits one slop below the
	// next region's base and an overhanging emission can never cross it.
	const u32 coldArenaSize = 4 * _1mb;
	s_coldBase = poolBase - coldArenaSize;
	s_coldPtr = s_coldBase;
	s_coldPtrEnd = poolBase - _64kb;

	recPtr = SysMemory::GetEERec();
	recPtrEnd = s_coldBase - _64kb;
	pxAssertRel(recPtrEnd > recPtr && recPtrEnd + _64kb <= s_coldBase &&
		s_coldPtrEnd + _64kb <= poolBase,
		"EE rec code region and cold arena must not reach the constant pool");
#ifdef IZPS2_RUN24_EE_TIMING
	std::fprintf(stderr, "IZPS2_EE_CACHE hot_bytes=%zu cold_bytes=%zu pool_bytes=%u\n",
		static_cast<size_t>(recPtrEnd - recPtr), static_cast<size_t>(s_coldPtrEnd - s_coldBase), poolSize);
#endif

	recReserveRAM();

	pxAssertRel(!s_pInstCache, "InstCache not allocated");
	s_nInstCacheSize = 128;
	s_pInstCache = (EEINST*)malloc(sizeof(EEINST) * s_nInstCacheSize);
	if (!s_pInstCache)
		pxFailRel("Failed to allocate R5900 InstCache array.");
}

// The rebuild's own measured duration, in GetCPUTicks() units. Published to
// EECycleRate at the end of every reset, because the caller that most wants it
// (EECycleRate::ApplyEffective) usually does not get it from timing its own
// Cpu->Reset() call: recResetEE defers when the EE is mid-execute, so the call
// returns long before the rebuild happens.
static u64 s_lastResetRawTicks = 0;

static void recResetMappedLut()
{
	if (s_eeLutDefault != JITCompile)
	{
		iopClearRecLUT(recLutReserve_RAM.data(), static_cast<int>(recLutEntries * 4));
		EE_LUT_RESET_SLOTS(recLutEntries);
		s_eeLutDefault = JITCompile;
	}
	else
	{
		for (size_t word = 0; word < s_eeLutDirtyPages.size(); ++word)
		{
			u64 pages = s_eeLutDirtyPages[word];
			while (pages)
			{
				const size_t page = word * 64 + std::countr_zero(pages);
				const size_t first = page * kEeLutSlotsPerPage;
				const size_t count = std::min(kEeLutSlotsPerPage, recLutEntries - first);
				iopClearRecLUT(recLutReserve_RAM.data() + first, static_cast<int>(count * 4));
				EE_LUT_RESET_SLOTS(count);
				pages &= pages - 1;
			}
		}
	}
	std::fill(s_eeLutDirtyPages.begin(), s_eeLutDirtyPages.end(), u64{0});
}

static void recClearOverlapSnapshots()
{
	const size_t size = recRAMCopy.size();
	constexpr size_t page_size = size_t{1} << kEeCodePageShift;
	const size_t bitmap_words = (size + page_size * kEeCodePageBits - 1) /
		(page_size * kEeCodePageBits);
	// Every snapshot store is preceded by marking its guest RAM span.
	// Clears leave those bits set until reset, even for removed blocks.
	for (size_t word = 0; word < bitmap_words; ++word)
	{
		u64 pages = s_eeCodePages[word];
		while (pages)
		{
			const size_t offset = (word * kEeCodePageBits + std::countr_zero(pages)) * page_size;
			if (offset >= size)
				break;
			const size_t bytes = std::min(page_size, size - offset);
			std::memset(recRAMCopy.data() + offset, 0, bytes);
			EE_SNAPSHOT_RESET_BYTES(bytes);
			pages &= pages - 1;
		}
	}
}


#ifdef PS2_EE_MAP_RESET_WITNESS
extern "C" {
u64 ps2_ee_reset_map_assignments = 0;
u64 ps2_ee_reset_unmapped_slots = 0;

u64 ps2_ee_reset_map_digest()
{
	u64 hash = 14695981039346656037ull;
	const uptr mapped = reinterpret_cast<uptr>(recLutReserve_RAM.data());
	const uptr unmapped = reinterpret_cast<uptr>(recLutUnmapped.data());
	const auto mix = [&](u64 value) {
		hash = (hash ^ value) * 1099511628211ull;
	};
	for (u32 page = 0; page < 0x10000; ++page)
	{
		const u32 pc = (page << 16) | 0x20;
		const BASEBLOCK* entry = GETBLOCK(pc);
		const uptr address = reinterpret_cast<uptr>(entry);
		if (address - mapped < recLutReserve_RAM.size() * sizeof(BASEBLOCK))
		{
			mix(1);
			mix(address - mapped);
			mix(entry->GetFnptr() == reinterpret_cast<uptr>(JITCompile));
		}
		else if (address - unmapped < recLutUnmapped.size() * sizeof(BASEBLOCK))
		{
			mix(2);
			mix(address - unmapped);
			mix(entry->GetFnptr() == reinterpret_cast<uptr>(UnmappedRecLUTPage));
		}
		else
		{
			mix(0);
		}
		mix(HWADDR(pc));
	}
	return hash;
}
}
#define EE_RESET_MAP_PAGE(...) (++ps2_ee_reset_map_assignments, recLUT_SetPage(__VA_ARGS__))
#define EE_RESET_UNMAPPED_SLOTS(count) (ps2_ee_reset_unmapped_slots += (count))
#else
#define EE_RESET_MAP_PAGE(...) recLUT_SetPage(__VA_ARGS__)
#define EE_RESET_UNMAPPED_SLOTS(count) ((void)0)
#endif

// The region aliases depend only on the table allocations and exposed RAM.
// Resetting compiled entries does not change where these aliases point.
static void recBuildAddressMap()
{
	BASEBLOCK* unmapped = recLutUnmapped.data();

	for (int i = 0; i < 0x10000; i++)
		EE_RESET_MAP_PAGE(recLUT, hwLUT, unmapped, i, 0, 0);

	// Map EE RAM
	for (int i = 0; i < static_cast<int>(Ps2MemSize::ExposedRam / _64kb); i++)
	{
		u32 mask = (Ps2MemSize::ExposedRam / _64kb) - 1;
		EE_RESET_MAP_PAGE(recLUT, hwLUT, recRAM, 0x0000, i, i & mask);
		EE_RESET_MAP_PAGE(recLUT, hwLUT, recRAM, 0x2000, i, i & mask);
		EE_RESET_MAP_PAGE(recLUT, hwLUT, recRAM, 0x3000, i, i & mask);
		EE_RESET_MAP_PAGE(recLUT, hwLUT, recRAM, 0x8000, i, i & mask);
		EE_RESET_MAP_PAGE(recLUT, hwLUT, recRAM, 0xa000, i, i & mask);
		EE_RESET_MAP_PAGE(recLUT, hwLUT, recRAM, 0xb000, i, i & mask);
		EE_RESET_MAP_PAGE(recLUT, hwLUT, recRAM, 0xc000, i, i & mask);
		EE_RESET_MAP_PAGE(recLUT, hwLUT, recRAM, 0xd000, i, i & mask);
	}

	// Map BIOS ROM
	for (int i = 0x1fc0; i < 0x2000; i++)
	{
		EE_RESET_MAP_PAGE(recLUT, hwLUT, recROM, 0x0000, i, i - 0x1fc0);
		EE_RESET_MAP_PAGE(recLUT, hwLUT, recROM, 0x8000, i, i - 0x1fc0);
		EE_RESET_MAP_PAGE(recLUT, hwLUT, recROM, 0xa000, i, i - 0x1fc0);
	}

	// Map ROM1 (full 4 MB / 64 pages). EROM lives inside ROM1 at a variable
	// offset, so games using EROM above page 0x1e04 must dispatch to mapped JIT
	// pages. recROM1 is already reserved for Rom1/4 entries above; matches x86
	// iR5900.cpp:573 and the arm64 IOP twin. Upstream fix 8fbb1e556.
	for (int i = 0x1e00; i < 0x1e40; i++)
	{
		EE_RESET_MAP_PAGE(recLUT, hwLUT, recROM1, 0x0000, i, i - 0x1e00);
		EE_RESET_MAP_PAGE(recLUT, hwLUT, recROM1, 0x8000, i, i - 0x1e00);
		EE_RESET_MAP_PAGE(recLUT, hwLUT, recROM1, 0xa000, i, i - 0x1e00);
	}

	// Map ROM2 (EROM2, Chinese BIOS extension, phys 0x1e400000-0x1e800000,
	// full 4 MB / 64 pages). x86 EE (iR5900.cpp:580-584), x86 IOP, and our
	// own arm64 IOP all map it; without it EE fetches from EROM2 dispatch to
	// UnmappedRecLUTPage and pause the VM. (AX-06)
	for (int i = 0x1e40; i < 0x1e80; i++)
	{
		EE_RESET_MAP_PAGE(recLUT, hwLUT, recROM2, 0x0000, i, i - 0x1e40);
		EE_RESET_MAP_PAGE(recLUT, hwLUT, recROM2, 0x8000, i, i - 0x1e40);
		EE_RESET_MAP_PAGE(recLUT, hwLUT, recROM2, 0xa000, i, i - 0x1e40);
	}
}

static void recResetRaw()
{
#if PS2_EE_SOFTMEM_COLD_OOL && defined(PS2_HOST_CODE_BRIDGE)
	if (s_softmemColdHotOpen || armSoftmemColdActive() || s_softmemColdVeneerCount)
		ps2_mtvu_retain_owner("softmem cold reset while active", __LINE__);
	s_softmemColdGenerationBytes = 0;
	++s_softmemColdStats.generations;
#endif
	s_manualLoopSmc = {};
	s_loopManualSmc = false;
	s_loopSemanticEntryHook = false;
	const u64 reset_t0 = GetCPUTicks();

	Console.WriteLn(Color_Green, "iR5900-ARM64 Recompiler reset.");
	EECycleRate::NoteEeReset();

	// The RAM map changed size since the last reset (an arcade session's 128 MB, or back to 32 MB
	// after one). Mirrors x86 recResetRaw; everything that points into the old arrays is rebuilt
	// below.
	if (s_reservedRam != Ps2MemSize::ExposedRam)
		recReserveRAM();

#if defined(PCSX2_RECOMPILER_TESTS) && PS2_EE_TEST_BLOCK_METADATA
	s_loopResidentBlocks.clear();
	s_blockContSites.clear();
	s_curBlockContSites.clear();
#endif

#ifdef PCSX2_RECOMPILER_TESTS
	// Before the first block compiles: a group forced to the interpreter here
	// changes what every later block emits. The census runs to the same beat
	// but reports the generation being thrown away, so a run long enough to
	// exhaust the cache leaves a trail even if it never shuts down cleanly.
	EERecFallback::InitFromEnvOnce();
	EERecFallback::MaybeLogCop2VuCensus(false);
#endif

	// COP2 macro-mode emitters read their clamp/mask constants from the pack
	// ([RSTATE, #imm]) — (re)write them before any block compiles.
	cop2RecWritePackConstants();

	// recQFSRV reads this ramp as its TBL index base ([RSTATE, #imm]); rewrite
	// it here for the same reason as cop2RecWritePackConstants above.
	for (u8 i = 0; i < 16; i++)
		_cpuRegistersPack.byteRamp[i] = i;

	// Both the immutable blobs and their lookup entries expire with the JIT generation.
	s_eeSmcSnapshots.Reset();
	s_eeSmcStorage.Reset();
	s_eeConstantPool.Reset();
	eeResetCodeWriteTracking();

	armSetAsmPtr(SysMemory::GetEERec(), SysMemory::GetEERecEnd() - SysMemory::GetEERec(), &s_eeConstantPool);
	armStartBlock();
	const u8* dispStart = armGetCurrentCodePointer();
	_DynGen_Dispatchers();
	// Reset drops old landing pointers. The freshly generated dispatcher
	// supplies the landing for zero-PC sentinel entries.
	eeCallRetResetRing();
#if defined(PS2_HOST_CODE_BRIDGE) || PS2_EE_SOFTMEM_SHARED_CALLS
	extern __attribute__((visibility("hidden"))) void recEmitEeMemoryCalls();
	recEmitEeMemoryCalls();
#endif

	// SL-11: the shared cold-exit tail. Contract on entry: guest state fully
	// flushed, RWSCRATCH = target guest pc, RWARG2 (w1) = scaled block cycles
	// (zero-extended). Adds with a zero register still sets N/Z from RECCYCLE,
	// matching emitCycleUpdateAndEventCheck's Cmp-on-zero-cycles shape. The
	// no-event path RETs to the BL site's linked B; the event path discards
	// the link register (pc is already stored, DispatcherEvent re-enters
	// through the dispatcher).
	SuperblockExitStub = armGetCurrentCodePointer();
	armAsm->bti(a64::EmitBTI_jc);
	armAsm->Str(RWSCRATCH, armCpuRegMem(&cpuRegs.pc));
	armAsm->Adds(RECCYCLE, RECCYCLE, a64::x1);
	armEmitCondBranch(a64::ge, DispatcherEvent);
	armAsm->Ret();

#if PS2_EE_INCOMING_PAGE_BATCH && defined(PS2_HOST_CODE_BRIDGE)
	SuperblockDispatchExitStub = armGetCurrentCodePointer();
	armAsm->bti(a64::EmitBTI_jc);
	armAsm->Str(RWSCRATCH, armCpuRegMem(&cpuRegs.pc));
	armAsm->Adds(RECCYCLE, RECCYCLE, a64::x1);
	armEmitCondBranch(a64::ge, DispatcherEvent);
	armEmitJmp(DispatcherReg);

	SuperblockRamExitStub = armGetCurrentCodePointer();
	armAsm->bti(a64::EmitBTI_jc);
	armAsm->Str(RWSCRATCH, armCpuRegMem(&cpuRegs.pc));
	armAsm->Adds(RECCYCLE, RECCYCLE, a64::x1);
	armEmitCondBranch(a64::ge, DispatcherEvent);
	armAsm->Ubfx(a64::w0, RWSCRATCH, 2, 27);
	armAsm->Mov(RSCRATCHADDR, reinterpret_cast<uptr>(recRAM));
	static_assert(sizeof(BASEBLOCK) == 8);
	armAsm->Ldr(RSCRATCHADDR, a64::MemOperand(RSCRATCHADDR, a64::x0, a64::LSL, 3));
	armAsm->Br(RSCRATCHADDR);
#endif

	// Until a block passes out of their reach, the dispatchers are their own
	// veneers.
	s_condVeneerEvent = static_cast<const u8*>(DispatcherEvent);
	s_condVeneerReg = static_cast<const u8*>(DispatcherReg);

	const u8* dispEnd = armGetCurrentCodePointer();
	recPtr = armEndBlock();

	s_coldPtr = s_coldBase;

	Console.WriteLn(Color_Green, "EE ARM64: Dispatcher generated at %p (%zu bytes)", dispStart, (size_t)(dispEnd - dispStart));

	recResetMappedLut();

	if (!s_eeAddressMapReady)
	{
		recBuildAddressMap();
		s_eeAddressMapReady = true;
	}
	if (s_eeUnmappedDefault != UnmappedRecLUTPage)
	{
		BASEBLOCK* unmapped = recLutUnmapped.data();
		for (int i = 0; i < _64kb / 4; i++)
			unmapped[i].SetFnptr((uptr)UnmappedRecLUTPage);
		EE_RESET_UNMAPPED_SLOTS(_64kb / 4);
		s_eeUnmappedDefault = UnmappedRecLUTPage;
	}

	if (s_pInstCache)
		memset(s_pInstCache, 0, sizeof(EEINST) * s_nInstCacheSize);

	recBlocks.Reset();
	s_eeChargeSites = 0;
	s_eeFormChangeSites = 0;
	s_eeFormChangeBlocks = 0;
	s_eeBlockHasFormChange = false;
	// Both arenas rewind here, so this is the one place a charge record can
	// dangle. clear() keeps the capacity, since a cache-full reset is followed
	// by rebuilding the same working set.
	s_eeChargeRecords.clear();
	s_eeColdChargeRecords.clear();
	s_eeChargePending = false;
#ifdef PCSX2_RECOMPILER_TESTS
	std::memset(s_eeChargeSiteByEmitter, 0, sizeof(s_eeChargeSiteByEmitter));
#endif
	// The code cache is about to be rewound, so every fastmem backpatch record
	// points at code that no longer exists. vtlb_AddLoadStoreInfo overwrites a
	// colliding code_address, so a stale record cannot mispatch — but nothing
	// else prunes the map, and it would otherwise accumulate for the whole VM
	// session. Mirrors x86 iR5900.cpp.
	vtlb_ClearLoadStoreInfo();
	maxrecmem = 0;
	s_maxBlockBytes = 0;
	memset(s_overlapUpperEnd, 0, sizeof(s_overlapUpperEnd));
	recClearOverlapSnapshots();
	memset(s_eeCodePages, 0, sizeof(s_eeCodePages));

	memset(manual_page, 0, sizeof(manual_page));
	memset(manual_counter, 0, sizeof(manual_counter));

	g_branch = 0;

	s_lastResetRawTicks = GetCPUTicks() - reset_t0;
	EECycleRate::NoteEeRebuildCost(s_lastResetRawTicks);
}

u64 recEeGetLastResetTicks()
{
	return s_lastResetRawTicks;
}

// =====================================================================================================
//  Immediate patching: a cycle-rate transition without a reset
//
// The scaled charge is a pure function of the raw block-cycle count and the
// selector, and the remainder the scaler retains between sites in one block is
// `& 0x7` for every selector from -3 through +1. So for any governor window
// whose top is +1 or below, rewriting a site's immediate produces exactly the
// bytes a fresh compile at the new selector would have emitted
// (EmittedCodeAfterPatchingMatchesAFreshCompile pins this byte for byte).
//
// This avoids recResetRaw()'s block-pointer LUT rewrite and recRAMCopy memset,
// and the lazy re-JIT of everything the next frame touches.
// =====================================================================================================

// A pass that has to throw away more than this share of the live blocks is not
// buying anything. Each invalidated block pays a full recompile on next
// dispatch, which is what the reset pays anyway, and the reset at least pays it
// once instead of spread over the next second.
//
// The value is a judgement call, not a measured knee. It only keeps a
// pathological workload from turning a repair into a slow reset.
static constexpr u32 kEeMaxInvalidatedBlockPercent = 10;

// Scratch, held across passes so a transition does not allocate. One planned
// write per patchable site, and one entry per block that has to be thrown away.
namespace
{
	struct EePatchWrite
	{
		u8* site;
		u32 word;
	};
} // namespace

static std::vector<EePatchWrite> s_eePatchWritesHot;
static std::vector<EePatchWrite> s_eePatchWritesCold;
static std::vector<u32> s_eeDoomedBlocks;

// Walk one arena's records and work out what the pass would do, WITHOUT
// touching a byte of code. Returns nullptr on success, or the reason the whole
// pass has to be refused.
//
// A site whose word no longer holds the instruction it was recorded as is either
// an ENTRY record whose block was removed (its code is orphaned; see
// kEeChargeAtBlockEntry) or the recompiler contradicting itself, which only the
// full reset can answer.
static const char* recEePlanArena(const std::vector<EeChargeRecord>& table, u8* base, s8 new_selector,
	std::vector<EePatchWrite>& out, u32* orphaned)
{
	out.clear();
	out.reserve(table.size());

	for (const EeChargeRecord& r : table)
	{
		if (r.flags & kEeChargeUnpatchable)
		{
			s_eeDoomedBlocks.push_back(r.owner_startpc);
			continue;
		}

		u8* const site = base + r.offset;
		const EeChargeForm form = static_cast<EeChargeForm>(r.form);

		u32 word = 0;
		std::memcpy(&word, armGetWritableCodePtr(site), sizeof(word));
		if (!eeChargeSiteHasForm(word, form))
		{
			if (r.flags & kEeChargeAtBlockEntry)
			{
				// The redirect stub, not a charge. Nothing executes this block
				// any more, so there is nothing to repair.
				(*orphaned)++;
				continue;
			}
			return "a recorded charge site no longer holds its charge instruction";
		}

		// The production scaler at the target selector, so there is no second
		// copy of the formula to drift.
		s_nBlockCycles = r.raw;
		const u32 charge = scaleblockcycles_calculation_for(new_selector);
		if (!eeChargeFitsForm(charge, form))
			return "a charge outgrew its encoding form the classifier said it kept";

		const u32 patched = eeEncodeChargeWord(word, form, charge);
		if (patched != word)
			out.push_back(EePatchWrite{site, patched});
	}

	return nullptr;
}

bool recEeRepatchCycleCharges(s8 new_selector)
{
	const u64 t0 = GetCPUTicks();
	u32 unpatchable_blocks = 0;
	const auto refuse = [t0, &unpatchable_blocks](const char* why) {
		EECycleRate::TransitionPath path;
		path.patched = false;
		path.reason = why;
		path.ticks = GetCPUTicks() - t0;
		path.blocks_unpatchable = unpatchable_blocks;
		EECycleRate::NoteEePatchPass(path);
		return false;
	};

	// Only the CPU thread executes EE recompiled code — Cpu->Execute() is
	// called from VMManager::Execute() on this thread, MTVU only ever reaches
	// CpuVU1->Execute, and the GS thread runs no guest code at all. That is
	// what makes single-PE cache maintenance sufficient below.
	pxAssertMsg(VMManager::Internal::IsOnCPUThread(),
		"recEeRepatchCycleCharges rewrites live EE code — CPU thread only.");
	// And never from inside a compile: the arena-wide write scopes below would
	// RX-flip an open emit window on their way out, which is the failure
	// recClear documents having hit on iOS Legacy mode.
	pxAssertMsg(!armHasBlock(), "recEeRepatchCycleCharges must not run mid-compile.");

	if (Cpu != &recCpu)
		return refuse("the EE recompiler is not the active provider");

	if (!recPtr)
		return refuse("the EE recompiler has no code cache yet");

	// The remainder the scaler retains between sites in one block widens to
	// & 0xF at +2 and & 0x1F at +3, so above a configured +1 the raw a later
	// site in the same block sees is no longer selector-independent and every
	// recorded raw is simply wrong for the other selectors in the window.
	if (EECycleRate::GetConfigured() > 1)
		return refuse("configured rate above +1 widens the retained remainder");

	// A reset is already queued for the next safe boundary, so repairing the
	// code it is about to throw away is work for nothing.
	if (eeRecNeedsReset)
		return refuse("an EE recompiler reset is already pending");

	s_eeDoomedBlocks.clear();
	u32 orphaned_sites = 0;
	const u32 saved_block_cycles = s_nBlockCycles;
	const char* refused =
		recEePlanArena(s_eeChargeRecords, SysMemory::GetEERec(), new_selector, s_eePatchWritesHot, &orphaned_sites);
	if (!refused)
		refused =
			recEePlanArena(s_eeColdChargeRecords, s_coldBase, new_selector, s_eePatchWritesCold, &orphaned_sites);
	s_nBlockCycles = saved_block_cycles;
	if (refused)
		return refuse(refused);

	std::sort(s_eeDoomedBlocks.begin(), s_eeDoomedBlocks.end());
	s_eeDoomedBlocks.erase(std::unique(s_eeDoomedBlocks.begin(), s_eeDoomedBlocks.end()), s_eeDoomedBlocks.end());
	unpatchable_blocks = static_cast<u32>(s_eeDoomedBlocks.size());

	const u32 live_blocks = recBlocks.size();
	if (s_eeDoomedBlocks.size() * 100 > static_cast<size_t>(live_blocks) * kEeMaxInvalidatedBlockPercent)
	{
		DevCon.WriteLn("EE cycle rate: %zu of %u live blocks cannot be repaired in place, over the %u%% cap — "
		               "resetting instead of patching",
			s_eeDoomedBlocks.size(), live_blocks, kEeMaxInvalidatedBlockPercent);
		return refuse("more unpatchable blocks than the cap allows");
	}

	// One write scope per arena for the whole pass, not one per word.
	// armPatchCodeWord opens its own page-granular scope, which suits a handful
	// of link sites but would be an mprotect pair per word here on iOS Legacy mode.
	//
	// Flipping the whole used arena to RW mid-execute is safe, unlike during a
	// compile: nothing executes EE code between the Begin and the End (this is
	// the CPU thread, running straight-line C), and live return addresses are
	// only returned into after the End restores RX.
	ArmCodePatchFlusher flusher;
	u32 patched_sites = 0;

	const auto write_arena = [&flusher, &patched_sites](std::vector<EePatchWrite>& writes, u8* base, u8* end) {
		if (writes.empty())
			return;

		HostSys::BeginCodeWriteRange(base, static_cast<size_t>(end - base));
		for (const EePatchWrite& w : writes)
		{
			// Aligned 32-bit stores are single-copy atomic on AArch64, so even
			// the hypothetical concurrent reader never sees a torn word.
			std::memcpy(armGetWritableCodePtr(w.site), &w.word, sizeof(w.word));
			flusher.Note(w.site);
			patched_sites++;
		}
		HostSys::EndCodeWriteRange(base, static_cast<size_t>(end - base));
	};

	write_arena(s_eePatchWritesHot, SysMemory::GetEERec(), recPtr);
	write_arena(s_eePatchWritesCold, s_coldBase, s_coldPtr);
	flusher.Finish();

	// What could not be repaired is thrown away one block at a time: reset the
	// LUT entry so the next dispatch recompiles at the new rate, and stamp the
	// redirect stub over the entry so a link site still branching straight into
	// the old code re-dispatches instead. This is what recClear does, minus the
	// array erase, which is O(live blocks) per block.
	//
	// After the write scopes, not inside them: Arm64BaseBlocks::Remove-style
	// patching opens its own scope per site, and nesting one inside an open
	// range makes iOS Legacy mode RX-flip a window somebody else still holds.
	u32 invalidated = 0;
	for (const u32 startpc : s_eeDoomedBlocks)
	{
		const int idx = recBlocks.Index(startpc);
		if (idx < 0)
			continue;
		const BASEBLOCKEX* b = recBlocks[idx];
		if (!b || b->startpc != startpc)
			continue; // the record is stale; its block is already gone

		GETBLOCK(startpc)->SetFnptr((uptr)JITCompile);
		recBlocks.Invalidate(idx);
		invalidated++;
	}

	EECycleRate::TransitionPath path;
	path.patched = true;
	path.reason = "patched in place";
	path.ticks = GetCPUTicks() - t0;
	path.sites = patched_sites;
	path.sites_orphaned = orphaned_sites;
	path.blocks_invalidated = invalidated;
	path.blocks_unpatchable = unpatchable_blocks;
	EECycleRate::NoteEePatchPass(path);
	return true;
}

static void recShutdown()
{
#if PS2_EE_SOFTMEM_COLD_OOL && defined(PS2_HOST_CODE_BRIDGE)
	if (s_softmemColdHotOpen || armSoftmemColdActive() || s_softmemColdVeneerCount)
		ps2_mtvu_retain_owner("softmem cold shutdown while active", __LINE__);
	const auto& sc = s_softmemColdStats;
    Console.WriteLn("PS2_EE_CLEAR_SCAN diagnosticOnly=1 ownershipAuthority=0 metadataVisits=%llu bucketsSkipped=%llu",
        static_cast<unsigned long long>(s_clearMetadataVisits), static_cast<unsigned long long>(s_clearBucketsSkipped));
    Console.WriteLn("PS2_EE_EMPTY_CLEAR diagnosticOnly=1 ownershipAuthority=0 zeroWords=%llu belowFirst=%llu",
        static_cast<unsigned long long>(s_emptyClearZero), static_cast<unsigned long long>(s_emptyClearBelowFirst));
	Console.WriteLn("PS2_EE_OVERLAP_SCAN diagnosticOnly=1 ownershipAuthority=0 metadataVisits=%llu bucketsSkipped=%llu bucketBytes=256 indexBytes=%zu",
		static_cast<unsigned long long>(s_overlapMetadataVisits), static_cast<unsigned long long>(s_overlapBucketsSkipped), sizeof(s_overlapUpperEnd));
	Console.WriteLn("EE softmem cold compile: requests=%llu emitted=%llu bytes=%llu refusedBytes=%llu quota=%zu capacity=%zu maxGeneration=%llu maxBody=%llu preElfBytes=%llu postElfBytes=%llu refusedQuota=%llu refusedCapacity=%llu refusedBlock=%llu refusedContext=%llu generations=%llu bootQuota=%zu refusedBootBudget=%llu",
		static_cast<unsigned long long>(sc.requests), static_cast<unsigned long long>(sc.emitted),
		static_cast<unsigned long long>(sc.emittedBytes), static_cast<unsigned long long>(sc.refusedBytes),
		recSoftmemColdGenerationQuota(), s_coldBase ? static_cast<size_t>(s_coldPtrEnd - s_coldBase) : 0,
		static_cast<unsigned long long>(sc.maxGenerationBytes), static_cast<unsigned long long>(sc.maxBodyBytes),
		static_cast<unsigned long long>(sc.beforeElfBytes), static_cast<unsigned long long>(sc.afterElfBytes),
		static_cast<unsigned long long>(sc.refusedQuota), static_cast<unsigned long long>(sc.refusedCapacity),
		static_cast<unsigned long long>(sc.refusedBlock), static_cast<unsigned long long>(sc.refusedContext),
		static_cast<unsigned long long>(sc.generations), kEeSoftmemColdBootQuota,
		static_cast<unsigned long long>(sc.refusedBootBudget));
#endif
	s_manualLoopSmc = {};
	s_loopManualSmc = false;
	s_loopSemanticEntryHook = false;
#ifdef PCSX2_RECOMPILER_TESTS
	// The only dump a short run reaches: every reset it saw came before the
	// first block compiled.
	EERecFallback::MaybeLogCop2VuCensus(true);
#endif

	s_eeSmcSnapshots.Reset();
	s_eeSmcStorage.Reset();
	s_eeConstantPool.Destroy();
	recRAMCopy.deallocate();
	recLutReserve_RAM.deallocate();
	recLutUnmapped.deallocate();

	safe_free(s_pInstCache);
	s_nInstCacheSize = 0;

	recPtr = nullptr;
	recPtrEnd = nullptr;
	s_coldBase = nullptr;
	s_coldPtr = nullptr;
	s_coldPtrEnd = nullptr;
}

static void recResetEE()
{
	if (eeCpuExecuting)
	{
		eeRecNeedsReset = true;
		recSafeExitExecution();
		return;
	}

	recResetRaw();
}

static void recStep()
{
}

static void recExitExecution()
{
	fastjmp_jmp(&m_SetJmp_StateCheck, 1);
}

static void recSafeExitExecution()
{
	// Callable from ANY thread: the Android pause/stop JNI fires this cross-thread
	// against a running EE (desktop hosts only call it from the CPU thread). The
	// flag is the whole mechanism — recEventTest consumes it at the next event
	// test, at most one scheduler horizon (~an hblank of guest time) away.
	//
	// The x86-inherited accelerants this used to carry were cross-thread poison:
	//
	//  - `cpuRegs.nextEventCycle = 0` raced the pinned cycle delta (RECCYCLE =
	//    cycle - nextEventCycle). A hit landing while the EE was mid-chain made
	//    the next armFlushCycleDelta reconstruct cycle = delta + 0, warping the
	//    EE clock back to near VM birth (observed: -142e9 cycles). Counter
	//    baselines were then "ahead" of cycle, and the old u32 rcntSyncCounter
	//    blowup minted the GoW2 poisoned-savestate scar from exactly this seam.
	//    On x86 the store was safe AND useful — block tails compare cycle
	//    against nextEventCycle in MEMORY, so zeroing it forced the very next
	//    tail into the event test. arm64 tails test the pinned delta's sign, so
	//    here it never even accelerated the exit.
	//
	//  - the iopCycleEE/iopBreak fold raced the IOP timeslice bookkeeping on
	//    the CPU thread. It only ever shaved one IOP slice of exit latency.
	eeRecExitRequested.store(true, std::memory_order_release);
}

static void recCancelInstruction()
{
	// The interpreter's version longjmps out of the instruction it is inside;
	// a recompiled block has no such seam. x86 asserts here instead; its only
	// rec-reachable caller is RaiseAddressError, which just logs.
}

static void recExecute()
{
	if (eeRecNeedsReset)
	{
		eeRecNeedsReset = false;
		recResetRaw();
	}

#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
	NativePoll::StartTrialEntry();
#endif

#ifdef __APPLE__
	// [iOS] Ensure the JIT region is executable before dispatching. Under the
	// Legacy (mprotect-toggle) W^X mode the region can be left RW by code
	// emission, and the OS can revoke the JIT grant between boots;
	// LegacyEnsureExecutable re-arms the RX protection. No-op on macOS and on
	// the dual-map modes. (Ported from ARMSX2 master's aR5900 recExecute.)
	DarwinMisc::LegacyEnsureExecutable();
#endif

#ifdef IZPS2_SWITCH2_STARTUP_TRACE
	if (s_ee_startup_compiles || s_ee_startup_events)
		Console.WriteLn("IZPS2_EE_STARTUP phase=entry pc=%08x entry=%p dispatcher=%p compile=%p",
			cpuRegs.pc, EnterRecompiledCode, DispatcherReg, JITCompile);
#endif
	Console.WriteLn(Color_Green, "EE ARM64: Entering recompiled code (pc=0x%08X)", cpuRegs.pc);

	if (!fastjmp_set(&m_SetJmp_StateCheck))
	{
		eeCpuExecuting = true;
		((void (*)())EnterRecompiledCode)();
	}

	eeCpuExecuting = false;
#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
	NativePoll::Invalidate();
#endif
}

#ifdef PCSX2_RECOMPILER_TESTS
// Harness entry. Not part of R5900cpu; called directly by EeRecTestHarness
// for a bounded number of guest cycles ending at park_pc. Forces
// nextEventCycle = cpuRegs.cycle so iBranchTest at every block tail routes
// through DispatcherEvent (where recEventTest's harnessShouldExit check
// can observe parking-PC arrival or cycle exhaust). Returns the cycle
// delta consumed in this run.
s32 recEeExecuteBlock(s32 cycles, u32 park_pc)
{
#if PS2_NATIVE_CROSSBLOCK_POLL_LATCH && PS2_NATIVE_POLL_WRITER_PROFILE == 2
	NativePoll::Invalidate();
#endif
	// Match recExecute: service a deferred reset before dispatch.
	if (eeRecNeedsReset)
	{
		eeRecNeedsReset = false;
		recResetRaw();
	}

#if PS2_NATIVE_CROSSBLOCK_POLL_LATCH && PS2_NATIVE_POLL_WRITER_PROFILE == 2
	// Re-derive after deferred reset, before the actual EE entry. No lease
	// is persisted in guest savestates, config files or host RPC metadata.
	NativePoll::lease = NativePoll::GetEntryRejection() ? 0 : 1;
#endif
	const s32 cap = std::min(cycles, kExecuteBlockSafetyCap);

	g_eeHarnessActive = true;
	g_eeHarnessParkPc = park_pc;
	g_eeHarnessCycleBudget = cap;
	g_eeHarnessCycleStart = cpuRegs.cycle;
	eeRecExitRequested.store(false, std::memory_order_relaxed);

	cpuRegs.nextEventCycle = cpuRegs.cycle;

	if (!fastjmp_set(&m_SetJmp_StateCheck))
	{
		eeCpuExecuting = true;
		((void (*)())EnterRecompiledCode)();
	}

	eeCpuExecuting = false;
	g_eeHarnessActive = false;
#if PS2_NATIVE_CROSSBLOCK_POLL_LATCH && PS2_NATIVE_POLL_WRITER_PROFILE == 2
	NativePoll::Invalidate();
#endif
	return static_cast<s32>(cpuRegs.cycle - g_eeHarnessCycleStart);
}

// Test-harness link introspection. Forwards to Arm64BaseBlocks::IsLinked,
// which walks the link multimap for any patch site within the block
// containing src_pc that targets dst_pc.
bool recEeIsBlockLinked(u32 src_pc, u32 dst_pc)
{
	return recBlocks.IsLinked(src_pc, dst_pc);
}

// SL-1 introspection: the registered back-edge patch site + spill stub of the
// block at pc_query, so tests can assert Remove()'s repoint after a recClear.
bool recEeLoopBackedgeInfo(u32 pc_query, uptr* site, uptr* stub)
{
	BASEBLOCKEX* b = recBlocks.Get(HWADDR(pc_query));
	if (!b || !b->backedge_site)
		return false;
	*site = b->backedge_site;
	*stub = b->backedge_stub;
	return true;
}

// SL-03 introspection: guest-insn size of the compiled block starting at
// pc_query (0 = no block), so tests can assert superblock formation (the
// range spans past a continuation branch) vs termination (it doesn't).
u32 recEeBlockGuestSize(u32 pc_query)
{
	BASEBLOCKEX* b = recBlocks.Get(HWADDR(pc_query));
	return b ? b->size : 0;
}

// The block entry at pc_query as the linker sees it (BASEBLOCKEX fnptr +
// host byte size) plus the LUT dispatch target, so tests can assert that
// the two dispatch routes agree. A mid-compile recClear whose removed-
// straddler extent covers the in-progress startpc can wipe the LUT entry;
// if nothing restores it, the next dispatch recompiles the block a second
// time into the surviving BASEBLOCKEX. Returns false if no entry starts
// exactly at pc_query.
bool recEeBlockHostInfo(u32 pc_query, uptr* fnptr, u32* host_size, uptr* lut_fnptr)
{
	BASEBLOCKEX* b = recBlocks.Get(HWADDR(pc_query));
	if (!b || b->startpc != HWADDR(pc_query))
		return false;
	*fnptr = b->fnptr;
	*host_size = b->x86size;
	*lut_fnptr = GETBLOCK(pc_query)->GetFnptr();
	return true;
}

// Test-harness recLUT coverage introspection: does this guest PC dispatch to
// a real (compile-on-first-hit) LUT page rather than UnmappedRecLUTPage?
// The harness can't execute code from ROM regions (it's hardwired to EE
// RAM), so region-mapping fixes like the ROM2 one (AX-06) are pinned by
// asserting page coverage directly.
bool recEeIsPcMapped(u32 pc_query)
{
	const BASEBLOCK* b = GETBLOCK(pc_query);
	return b && (b->GetFnptr() != (uptr)UnmappedRecLUTPage);
}
#endif

// =====================================================================================================
//  Timeout Loop Speedhack
// =====================================================================================================

// Detects and skips timeout loops like:
//   addiu v0,v0,-1 / nop*N / bne v0,zero,loop / nop
// Instead of spinning, advances the cycle counter and decrements the register.
// Port of x86 skipMPEG_By_Pattern (pcsx2/x86/ix86-32/iR5900.cpp). The IOP FMV
// path's sceMpegIsEnd compiles to the 3-instruction leaf
//     lw  reg, 0x40(a0) ; jr ra ; lw v0, 0(reg)
// When CHECK_SKIPMPEGHACK is on, recognize that signature at a block boundary
// and replace the whole block with "v0 = 1; pc = ra" — telling the game the
// video already finished so the (often unplayable/looping) FMV is skipped.
// Several games hard-depend on this to boot — Katamari (our benchmark) among
// them. Emits a complete self-contained block (like recSkipTimeoutLoop) and
// makes the caller skip normal codegen by returning true. Restores Skip-MPEG
// host hook B4 on arm64 (DT-03); was a silent no-op before this.
static bool skipMPEG_By_Pattern(u32 sPC)
{
	if (!CHECK_SKIPMPEGHACK)
		return false;

	// sceMpegIsEnd: lw reg, 0x40(a0); jr ra; lw v0, 0(reg)
	if ((s_nEndBlock == sPC + 12) && (memRead32(sPC + 4) == 0x03e00008))
	{
		const u32 code = memRead32(sPC);
		const u32 p1 = 0x8c800040;
		const u32 p2 = 0x8c020000 | (code & 0x1f0000) << 5;
		if ((code & 0xffe0ffff) != p1)
			return false;
		if (memRead32(sPC + 8) != p2)
			return false;

		// v0 = 1 (low) / 0 (high); pc = ra.
		armAsm->Mov(RWSCRATCH, 1);
		armStoreEERegPtr(RWSCRATCH, &cpuRegs.GPR.n.v0.UL[0]);
		armStoreEERegPtr(a64::wzr, &cpuRegs.GPR.n.v0.UL[1]);
		armLoadEERegPtr(a64::w0, &cpuRegs.GPR.n.ra.UL[0]);
		armAsm->Str(a64::w0, armCpuRegMem(&cpuRegs.pc));

		// x86 iBranchTest() tail (newpc == 0xffffffff path): commit the block's
		// cycles into the delta, then route to DispatcherEvent if an event is
		// due (delta >= 0), else DispatcherReg to run the block at pc=ra.
		// s_nBlockCycles is still 0 here (no instruction emitted yet) — matches
		// x86, where iBranchTest's scaleblockcycles() also sees a zero count.
		emitCycleUpdateAndEventCheck();
		armEmitJmp(DispatcherReg);

		g_branch = 1;
		pc = s_nEndBlock;
		Console.WriteLn(Color_StrongGreen, "sceMpegIsEnd pattern found! Recompiling skip video fix...");
		return true;
	}
	return false;
}

// Port of x86 recSkipTimeoutLoop().
static bool recSkipTimeoutLoop(s32 reg, bool is_timeout_loop)
{
	if (!EmuConfig.Speedhacks.WaitLoop || !is_timeout_loop)
		return false;

	DevCon.WriteLn("[EE] Skipping timeout loop at 0x%08X -> 0x%08X (reg=%d)",
		s_pCurBlockEx->startpc, s_nEndBlock, reg);

	// Logic: skip the loop by advancing cycles based on the register value.
	// In delta terms (delta = cycle - nextEventCycle; subtract nextEventCycle
	// from every absolute quantity in the x86 original):
	// new_delta = min(reg * 8 + delta, 0)
	// new_reg = reg - (new_delta - delta) / 8
	// if new_reg > 0, jump to dispatcher (an event interrupted the loop)
	// else loop finished, continue at s_nEndBlock

	// if (delta >= 0) goto DispatcherEvent
	armAsm->Cmp(RECCYCLE, 0);
	armEmitCondBranch(a64::ge, DispatcherEvent);

	// w9 = reg value (the decrementing counter). Scratches are w9/x10/w8
	// (reserved — w4-w7 are allocatable and may hold live values in the
	// surrounding EE block). MUST go through the pin helper: the counter can
	// be a pinned GPR (UYA idles on $at; any pinned register can be the
	// counter), and under lazy-dirty the loop's
	// decrements live in the pin while canonical memory is stale — a raw Ldr
	// here read a stale counter and the pin-aware store below then clobbered
	// the real one, collapsing UYA's frame pacing (600 frames in 1s).
	armLoadEERegPtr(a64::w9, &cpuRegs.GPR.r[reg].UL[0]);

	// x10 = reg * 8 + delta (estimated end delta, s64)
	armAsm->Add(a64::x10, RECCYCLE, a64::Operand(a64::x9, a64::LSL, 3));

	// x10 = min(x10, 0) — can't run past the next event
	armAsm->Cmp(a64::x10, 0);
	armAsm->Csel(a64::x10, a64::xzr, a64::x10, a64::gt); // if x10 > 0, clamp to 0

	// w8 = (new_delta - old_delta) >> 3 = iterations consumed (deltas share
	// the same nextEventCycle base, so their difference equals the absolute
	// cycle difference).
	armAsm->Sub(RWSCRATCH, a64::w10, RECCYCLE.W());
	armAsm->Lsr(RWSCRATCH, RWSCRATCH, 3);

	// Commit the new delta into RECCYCLE (no memory store — DispatcherEvent
	// converts and flushes it if we exit there; otherwise the next block-tail
	// event check uses RECCYCLE directly).
	armAsm->Mov(RECCYCLE, a64::x10);

	// reg -= iterations consumed; sign-extend into the 64-bit guest reg
	// (the full UD[0] store covers the UL[0] half).
	armAsm->Sub(a64::w9, a64::w9, RWSCRATCH);
	armAsm->Sxtw(a64::x9, a64::w9);
	armStoreEERegPtr(a64::x9, &cpuRegs.GPR.r[reg].UD[0]);

	// if reg != 0, event interrupted the loop — go to dispatcher
	armEmitCbnz(a64::w9, DispatcherEvent);

	// Loop finished — set PC to end of block and dispatch
	armAsm->Mov(RWSCRATCH, s_nEndBlock);
	armAsm->Str(RWSCRATCH, armCpuRegMem(&cpuRegs.pc));
	armEmitJmp(DispatcherReg);

	g_branch = 1;
	pc = s_nEndBlock;

	return true;
}

// =====================================================================================================
//  Main Recompilation Loop
// =====================================================================================================

// Scanner-side decode: is `code` any branch/jump/eret/syscall-class op (a
// block-ender or continuation candidate)? Used to refuse continuation through
// a branch whose delay slot is itself a branch (architecturally-UB shape) —
// those fall back to ending the block, which is today's behavior.
static bool eeScanInsnIsBranchClass(u32 code)
{
	switch (code >> 26)
	{
		case 0: // SPECIAL
		{
			const u32 funct = code & 0x3f;
			return funct == 8 || funct == 9 || funct == 12 || funct == 13; // JR/JALR/SYSCALL/BREAK
		}
		case 1: // REGIMM
		{
			const u32 rt = (code >> 16) & 0x1f;
			return rt < 4 || (rt >= 16 && rt < 20);
		}
		case 2: case 3: // J, JAL
		case 4: case 5: case 6: case 7: // BEQ, BNE, BLEZ, BGTZ
		case 20: case 21: case 22: case 23: // likely forms
			return true;
		case 16: // COP0: BC0x or ERET
			return ((code >> 21) & 0x1f) == 8 ||
				   (((code >> 21) & 0x1f) == 16 && (code & 0x3f) == 24);
		case 17: case 18: // COP1/COP2: BCx
			return ((code >> 21) & 0x1f) == 8;
	}
	return false;
}

// Scanner-side continuation gate for a conditional branch at `i` targeting
// `target`. Forward-only (backward keeps the split/end logic), bounded, and
// refuses a branch-class delay slot.
static bool eeScanContinuable(EeInstructionScan& scan, u32 startpc, u32 i, u32 target)
{
	return target > i + 4 &&
		   s_numContSites < kMaxContSites &&
		   ((i + 8 - startpc) / 4) < kMaxSuperblockInsns &&
		   !eeScanInsnIsBranchClass(scan.Read(i + 4));
}

// A backward-split target that lands exactly on a continuation site's delay
// slot would leave that branch as the last insn of the range with its delay
// slot outside the analyzed block (instinfo overrun, split-pair emission).
// End the block before the branch instead; the split's purpose — making the
// target a block start — is preserved (a block starting at the delay-slot
// address compiles it as a plain instruction, which is the architectural
// meaning of branching into a delay slot).
//
// Degenerate case: the site is the block's FIRST instruction (guest loops
// that re-enter at the head branch's delay slot — e.g. UYA's dcache flush
// routine: `beqz exit; addiu t2,-1; ...; bgtz t2, <the addiu>`). Clamping
// there would yield s_nEndBlock == startpc — a zero-length block whose
// short tail is an unconditional self-linked B with no event check, i.e. a
// hard wedge. Skipping the split is always correct (the target simply gets
// its own block when branched to), so fall back to ending at the branch.
static u32 eeSuperblockClampSplit(u32 startpc, u32 branch_i, u32 target)
{
	for (int k = 0; k < s_numContSites; k++)
	{
		if (target == s_contSitePcs[k] + 4)
		{
			if (s_contSitePcs[k] > startpc)
				return s_contSitePcs[k];
			return branch_i + 8; // degenerate: no split, end at the branch
		}
	}
	return target;
}

#if defined(PS2_HOST_CODE_BRIDGE) || defined(IZPS2_SWITCH2_EE_WARM)
// Warm compilation must have verified every instruction in the block, including
// any delay slot, and must avoid the VM's boot and settings hooks.
static bool eeWarmSpanEligible(u32 address, u32 bytes)
{
	return !(address & 3) && bytes && !(bytes & 3) &&
		bytes <= _4kb - (address & (_4kb - 1)) &&
		address < Ps2MemSize::ExposedRam - _4kb &&
		address != VMManager::Internal::GetCurrentELFEntryPoint() &&
		address != EELOAD_START && address != HWADDR(g_eeloadMain) && address != HWADDR(g_eeloadExec);
}

static bool eeWarmDelaySlotVerified(const u8* direct, u32 bytes)
{
	u32 last;
	std::memcpy(&last, direct + bytes - 4, sizeof(last));
	return !eeScanInsnIsBranchClass(last) ||
		((last >> 26) == 16 && ((last >> 21) & 31) == 16 && (last & 63) == 24) || // ERET
		((last >> 26) == 0 && ((last & 63) == 12 || (last & 63) == 13)); // SYSCALL/BREAK
}

#ifdef IZPS2_SWITCH2_EE_WARM
#include <native_ee_warm_capture.inc>
#else
#include "native_ee_warm_capture.inc"
#endif
#endif

static void recRecompileBlock(u32 startpc, [[maybe_unused]] bool warm_batch,
	[[maybe_unused]] u32 warm_end = 0, [[maybe_unused]] bool ordered_warm = false)
{
	const u64 compile_begin = GetCPUTicks();
	u32 i;

	// Note: startpc=0 is valid (EE RAM address 0). The x86 rec asserts on this
	// but it can legitimately happen during BIOS init (e.g., JR $ra with ra=0).
	// We allow it since address 0 is properly mapped in recLUT.

	if (recPtr >= recPtrEnd || s_coldPtr >= s_coldPtrEnd)
	{
		Console.WriteLn("EE ARM64: cache-full reset trigger — hot used=%zu/%zu cold used=%zu/%zu",
			(size_t)(recPtr - SysMemory::GetEERec()), (size_t)(recPtrEnd - SysMemory::GetEERec()),
			(size_t)(s_coldPtr - s_coldBase), (size_t)(s_coldPtrEnd - s_coldBase));
		eeRecNeedsReset = true;
	}

	// Signal that the ELF entry point is now compiling, so VMManager flips
	// HasBootedELF() and applies the per-game GameDB fixes (both game fixes and
	// GS hardware fixes — e.g. Baldur's Gate: Dark Alliance's textureInsideRT,
	// which fixes the right-half-black menu render). Must fire before the
	// deferred reset below — the hook can change settings and flush the JIT.
	// Mirrors iR5900.cpp's recRecompile.
	if (HWADDR(startpc) == VMManager::Internal::GetCurrentELFEntryPoint())
		VMManager::Internal::EntryPointCompilingOnCPUThread();

	if (eeRecNeedsReset)
	{
		eeRecNeedsReset = false;
		recResetRaw();
	}

#ifdef IZPS2_RUN24_EE_TIMING
	const auto publication_begin = HostSys::GetJitCacheTiming().ticks;
	const auto generation_begin = iz_system_ticks();
#endif
#ifdef PS2_HOST_CODE_BRIDGE
	// Prefer fresh pages near startup, within a fixed part of the hot arena.
	// Keep the regular packing rule for the rest of the generation.
	const size_t page_tail = (0 - reinterpret_cast<uptr>(recPtr)) & (_4kb - 1);
	const auto hot_offset = recPtr - SysMemory::GetEERec();
	const size_t tail_limit = hot_offset < 3072 * _1kb ? _4kb - 1 : _4kb / 8;
	if (!warm_batch && page_tail >= 16 && page_tail <= tail_limit &&
		page_tail < static_cast<size_t>(recPtrEnd - recPtr))
		recPtr += page_tail;

    // ELF/settings/reset callbacks ran above, outside the transaction. No EE
    // RX execution resumes until the explicit commit below succeeds.
    if (!warm_batch)
        armBeginEEPublicationTransaction(SysMemory::GetEERec(),
            static_cast<size_t>(SysMemory::GetEERecEnd()-SysMemory::GetEERec()));
    s_eeWarmPublicationOpen = warm_batch;
#endif
	s_manualLoopSmc = {};
	s_loopManualSmc = false;
	// Non-identity TLB entry hooks must keep their original entry frequency.
	s_loopSemanticEntryHook = EmuConfig.Gamefixes.GoemonTlbHack;
	u8* const cold_before_block = s_coldPtr;
	armSetAsmPtr(recPtr, recPtrEnd - recPtr + _64kb, &s_eeConstantPool);
	armStartBlock();
#if PS2_EE_SOFTMEM_COLD_OOL && defined(PS2_HOST_CODE_BRIDGE)
	if (s_softmemColdVeneerCount || s_softmemColdHotOpen)
		ps2_mtvu_retain_owner("softmem cold stale block metadata", __LINE__);
	s_softmemColdHasBootedElf = VMManager::Internal::HasBootedELF();
	s_softmemColdHotOpen = true;
#elif PS2_EE_SOFTMEM_BLOCK_TAIL
	pxAssertRel(!s_softmemColdHotOpen && !s_softmemColdVeneerCount, "unfinalized EE MMIO tails");
	s_softmemKeepHandlerWithBlock = false;
	s_softmemColdHotOpen = true;
#endif
	emitCondVeneers();

	s_pCurBlock = GETBLOCK(startpc);
	pxAssert(s_pCurBlock->GetFnptr() == (uptr)JITCompile || s_pCurBlock->GetFnptr() == (uptr)UnmappedRecLUTPage);

	// armStartBlock() aligned armAsmPtr to 16 bytes, so the actual block
	// code starts at armGetCurrentCodePointer(), not at recPtr. Block
	// linking branches to BASEBLOCKEX::fnptr, so it must be the aligned
	// address — using recPtr instead lands the branch on padding bytes
	// and triggers SIGILL.
	const uptr block_fnptr = (uptr)armGetCurrentCodePointer();
	armAsm->bti(a64::EmitBTI_jc);

	// New() both creates and re-binds: a startpc whose BASEBLOCKEX survived a
	// straddled recClear is retargeted at the new code rather than left with
	// a stale fnptr. It also publishes the block as the owner of every link
	// site the emission below registers.
#if PS2_EE_INCOMING_PAGE_BATCH && defined(PS2_HOST_CODE_BRIDGE)
	s_pCurBlockEx = ps2EeNewIncomingPageBatch(recBlocks, HWADDR(startpc), block_fnptr);
#elif defined(IZPS2_SWITCH2_EE_INCOMING_BATCH)
	bool incoming_batch = false;
	s_pCurBlockEx = recBlocks.New(HWADDR(startpc), block_fnptr, [&] {
		// Match the warm compiler's ownership and ELF guards. The entry
		// callbacks and any cache reset already ran before armStartBlock.
		if (!warm_batch && !THREAD_VU1 && IopThread::GetMode() == IopThreadMode::Off &&
			VMManager::Internal::HasBootedELF() && !EmuConfig.Gamefixes.GoemonTlbHack &&
			(!EmuConfig.EnablePatches || !ps2_patch_has_active_dynamic_patches()))
			incoming_batch = iz_ps2_begin_ee_warm_ranges();
	}, sizeof(u32));
#else
	s_pCurBlockEx = recBlocks.New(HWADDR(startpc), block_fnptr);
#endif

	g_branch = 0;
	cop2VfCacheReset();
	cop2ClampConstsInvalidate(); // SL-13: q25/q26 state unknown at block entry

	// The first assignment and the final entry restore use the same static
	// dispatch mapping. Clear/invalidate only write the default JIT entry.
	const uptr table_offset = reinterpret_cast<uptr>(s_pCurBlock) -
		reinterpret_cast<uptr>(recLutReserve_RAM.data());
	u32 clean_lut_end = 0;
	if (table_offset < recLutEntries * sizeof(BASEBLOCK))
	{
		const size_t page = table_offset / 4096;
		const u64 bit = u64{1} << (page % 64);
		u64& dirty = s_eeLutDirtyPages[page / 64];
		if (!(dirty & bit))
		{
			// One dispatch entry per guest word. This span was all JITCompile
			// before this compile installed the entry at startpc.
			const size_t slots = kEeLutSlotsPerPage - (table_offset / sizeof(BASEBLOCK)) % kEeLutSlotsPerPage;
			clean_lut_end = std::min(static_cast<u32>(slots * 4), 4096u - (startpc & 4095u));
		}
		dirty |= bit;
	}
	else
	{
		// A nonstandard compile used the unmapped table; restore its entries
		// on the next reset even when the generated handler stays in place.
		s_eeUnmappedDefault = nullptr;
	}
	s_pCurBlock->SetFnptr(block_fnptr);
	s_nBlockCycles = 0;
	s_nBlockInterlocked = false;
	s_eeBlockHasFormChange = false; // per-block, so the block counter counts blocks

	// A recompile at a startpc reuses the existing BASEBLOCKEX — drop any
	// stale SL-1 back-edge registration from the previous compile.
	s_pCurBlockEx->backedge_site = 0;
	s_pCurBlockEx->backedge_stub = 0;

	pc = startpc;
	g_cpuHasConstReg = g_cpuFlushedConstReg = 1;
	g_cpuFlushedPC = false;
	g_cpuFlushedCode = false;

	_initArm64GPRregs();
	_initArm64NEONregs();

#ifdef PCSX2_RECOMPILER_TESTS
	// Optional block-entry diagnostic hook (test builds only). Emitted only when
	// g_emit_block_hook is set before recReset; production recompiles emit
	// nothing. Fires on EVERY block entry, including statically-linked ones,
	// because linked branches target block_fnptr — i.e. exactly here. At the
	// prologue all guest state is memory-resident (the allocator was just
	// re-initialized to memory), so the hook's FingerprintCpu() reads correct
	// cpuRegs. We pass startpc as an immediate because cpuRegs.pc is not updated
	// on a static-linked entry, and flush the cycle delta -> cpuRegs.cycle so
	// the hook sees the live cycle. The hook is read-only (no cycle/event
	// mutation) and RECCYCLE (x25) is callee-saved across the C call, so no
	// reload is needed.
	if (ee_divtrace::g_emit_block_hook)
	{
		armFlushCycleDelta();
		armAsm->Mov(RWARG1, startpc);
		armFlushEEGPRPins(); // lazy-dirty seam: the hook READS guest GPR memory
		s_loopSemanticEntryHook = true;
		armEmitCall((void*)ee_divtrace_jit_block_hook);
		// Read-only hook, but the C call clobbers the caller-saved pins and
		// the block body it precedes reads guest state through them.
		armReloadEEClobberedPins();
	}
#endif

	// EELOAD detection + ELF-load hooks (mirrors iR5900.cpp's recRecompile).
	// These compile-time-detected, run-time-emitted calls are how PCSX2 learns
	// which game ELF is booting: eeloadHook() -> ELFLoadingOnCPUThread() sets
	// s_elf_entry_point / CRC, which gates HasBootedELF() and therefore ALL
	// per-game patches, game fixes, GS hardware fixes, widescreen, symbol import
	// and achievements. The emitted calls run at block-execution time; at the
	// prologue all guest state is memory-resident, so no iFlushCall is needed,
	// and the pinned bases (x24/x25) are callee-saved across the C call.
	if (HWADDR(startpc) == EELOAD_START)
	{
		// The EELOAD _start function is the same across all BIOS versions.
		const u32 mainjump = memRead32(EELOAD_START + 0x9c);
		if (mainjump >> 26 == 3) // JAL
			g_eeloadMain = ((EELOAD_START + 0xa0) & 0xf0000000U) | (mainjump << 2 & 0x0fffffffU);
	}

	if (g_eeloadMain && HWADDR(startpc) == HWADDR(g_eeloadMain))
	{
		// eeloadHook can do arbitrary VM work (ELF load, patch/achievement
		// init) that may reschedule nextEventCycle — keep the cycle delta
		// coherent across it. Boot-only path, cost is irrelevant.
		armFlushCycleDelta();
		// lazy-dirty seam: eeloadHook READS $a0/$a1 (argc/argv) from GPR
		// memory, and the full reload below must see flushed memory or it
		// would clobber dirty pins with stale values.
		armFlushEEGPRPins();
		s_loopSemanticEntryHook = true;
		armEmitCall((void*)eeloadHook);
		armReloadCycleDelta();
		armReloadEEGPRPins(); // ELF load / arg injection writes guest GPRs
		if (VMManager::Internal::IsFastBootInProgress())
		{
			// Four known EELOAD versions, identified by the location of the 'jal' to
			// the EELOAD function that calls ExecPS2(). The function itself is at the
			// same address in all BIOSs after v1.00-v1.10.
			const u32 typeAexecjump = memRead32(EELOAD_START + 0x470); // v1.00, v1.01?, v1.10?
			const u32 typeBexecjump = memRead32(EELOAD_START + 0x5B0); // v1.20, v1.50, v1.60 (3000x models)
			const u32 typeCexecjump = memRead32(EELOAD_START + 0x618); // v1.60 (3900x models)
			const u32 typeDexecjump = memRead32(EELOAD_START + 0x600); // v1.70, v1.90, v2.00, v2.20, v2.30
			if ((typeBexecjump >> 26 == 3) || (typeCexecjump >> 26 == 3) || (typeDexecjump >> 26 == 3)) // JAL to 0x822B8
				g_eeloadExec = EELOAD_START + 0x2B8;
			else if (typeAexecjump >> 26 == 3) // JAL to 0x82170
				g_eeloadExec = EELOAD_START + 0x170;
			else // Unexamined BIOS models: 18000, 3500x, 3700x, 5500x, 7900x (and v1.01/v1.10).
				Console.WriteLn("recRecompile: Could not enable launch arguments for fast boot mode; unidentified BIOS version! Please report this to the PCSX2 developers.");
		}
	}

	if (g_eeloadExec && HWADDR(startpc) == HWADDR(g_eeloadExec))
	{
		// Same coherence rationale as the eeloadHook call above.
		armFlushCycleDelta();
		// lazy-dirty seam: the full reload below must not clobber dirty pins
		// with stale memory (eeloadHook2 itself only WRITES $a0/$a1).
		armFlushEEGPRPins();
		s_loopSemanticEntryHook = true;
		armEmitCall((void*)eeloadHook2);
		armReloadCycleDelta();
		armReloadEEGPRPins(); // eeloadHook2 injects launch arguments into GPRs
	}

	// Goemon TLB-cache preload/unload intercept (mirrors x86 iR5900.cpp:2241-2255,
	// dropped-host-hook item B5). PCSX2 precalculates all TLB mappings, but Goemon
	// dynamically remaps; these boot-region PC hooks keep the precalculated cache
	// in sync. The branch-target V2P half is already handled (SetBranchReg / recJ*
	// when GoemonTlbHack is set). pc == startpc here, and the addresses are useg so
	// HWADDR would be a no-op on them — use the raw pc to mirror x86 literally.
	if (EmuConfig.Gamefixes.GoemonTlbHack)
	{
		if (pc == 0x33ad48 || pc == 0x35060c)
		{
			// 0x33ad48 / 0x35060c are the return address of the function (0x356250)
			// that populates the TLB cache.
			armFlushEEClobberedPins(); // lazy-dirty seam: pairs with the reload below
			s_loopSemanticEntryHook = true;
			armEmitCall((void*)GoemonPreloadTlb);
			// TLB-cache population writes no guest GPRs; restore the
			// caller-saved pins the C call clobbered before the block body.
			armReloadEEClobberedPins();
		}
		else if (pc == 0x3563b8)
		{
			// Game unmaps some virtual addresses; a constant address hardcoded into a
			// compiled block would go stale, so force a full rec reset.
			eeRecNeedsReset = true;
			// 0x3563b8 is the start of the function that invalidates a TLB-cache entry;
			// a0 holds the key. $a0 is pinned — read the mirror (authoritative in
			// both pin modes), NOT canonical memory, which is stale under
			// lazy-dirty until the seam flush (and $a0's pin is callee-saved, so
			// no clobber flush ever covers it).
			armLoadEERegPtr(RWARG1, &cpuRegs.GPR.n.a0.UL[0]);
			armFlushEEClobberedPins(); // lazy-dirty seam: pairs with the reload below
			s_loopSemanticEntryHook = true;
			armEmitCall((void*)GoemonUnloadTlb);
			armReloadEEClobberedPins(); // see GoemonPreloadTlb above
		}
	}

	// Scan for block boundary
	EeInstructionScan scan(startpc);
	i = startpc;
	s_nEndBlock = 0xffffffff;
	s_branchTo = -1;
	s_branchLoopable = false;
	s_numContSites = 0;
#if defined(PCSX2_RECOMPILER_TESTS) && PS2_EE_TEST_BLOCK_METADATA
	s_curBlockContSites.clear();
#endif

	// Timeout loop detection (matches x86 recSkipTimeoutLoop pattern):
	//   addiu reg,reg,-N / nop*N / bne reg,zero,loop / nop
	s32 timeout_reg = -1;
	bool is_timeout_loop = true;
	bool timeout_has_bne = false;

	while (1)
	{
		if (warm_end && i >= warm_end)
		{
			s_nEndBlock = i;
			break;
		}
		// The cached direct fetch prevents a handler from mutating the LUT
		// during this span. The scanner retires it before any fallback.
		if (i != startpc && (!scan.bytes || i - startpc >= clean_lut_end) &&
			GETBLOCK(i)->GetFnptr() != (uptr)JITCompile)
		{
			s_nEndBlock = i;
			break;
		}

		// 4K page boundary
		if (i != startpc && (i & 0xffc) == 0)
		{
			s_nEndBlock = i;
			break;
		}

		cpuRegs.code = scan.Read(i);

		// Timeout loop pattern matching
		if (is_timeout_loop)
		{
			if ((cpuRegs.code >> 26) == 8 || (cpuRegs.code >> 26) == 9)
			{
				// addi/addiu — must be first non-nop, decrementing same reg
				if (timeout_reg >= 0 || _Rs_ != _Rt_ || _Imm_ >= 0)
					is_timeout_loop = false;
				else
					timeout_reg = _Rs_;
			}
			else if ((cpuRegs.code >> 26) == 5)
			{
				// bne — must branch back using the timeout reg vs zero
				if (timeout_reg != static_cast<s32>(_Rs_) || _Rt_ != 0 || scan.Read(i + 4) != 0)
					is_timeout_loop = false;
				else
					timeout_has_bne = true;
			}
			else if (cpuRegs.code != 0)
			{
				is_timeout_loop = false;
			}
		}

		switch (cpuRegs.code >> 26)
		{
			case 0: // SPECIAL
				if (_Funct_ == 8 || _Funct_ == 9) // JR, JALR
				{
					s_nEndBlock = i + 8;
					goto StartRecomp;
				}
				if (_Funct_ == 12 || _Funct_ == 13) // SYSCALL, BREAK
				{
					s_nEndBlock = i + 4; // no delay slot
					goto StartRecomp;
				}
				break;

			case 1: // REGIMM
				if (_Rt_ < 4 || (_Rt_ >= 16 && _Rt_ < 20))
				{
					// rt 16-19 are the AL link variants — call-shaped tails
					// (SetBranchImmCall), not back-edge candidates.
					// SL-03: forward BLTZ/BGEZ (rt 0/1) become continuation
					// sites — scan on at the fallthrough. Likely + AL forms
					// keep ending the block.
					if (_Rt_ < 2 && eeScanContinuable(scan, startpc, i, _Imm_ * 4 + i + 4))
					{
						s_contSitePcs[s_numContSites++] = i;
						i += 8; // skip the delay slot word in the scan
						continue;
					}
					s_branchLoopable = _Rt_ < 4;
					s_branchTo = _Imm_ * 4 + i + 4;
					// Backward branch into the current block: end the block at the
					// target so the loop head becomes its own linkable block.
					// Mirrors x86 iR5900.cpp:2362 and the COP1/COP2 case below.
					if (s_branchTo > startpc && s_branchTo < i)
						s_nEndBlock = eeSuperblockClampSplit(startpc, i, s_branchTo);
					else
					{
						s_nEndBlock = i + 8;
					}
					goto StartRecomp;
				}
				break;

			case 2: case 3: // J, JAL
				s_branchLoopable = (cpuRegs.code >> 26) == 2; // JAL = call tail
				s_branchTo = (_InstrucTarget_ << 2) | ((i + 4) & 0xf0000000);
				s_nEndBlock = i + 8;
				goto StartRecomp;

			case 4: case 5: case 6: case 7: // BEQ, BNE, BLEZ, BGTZ
				// SL-03: forward conditionals become continuation sites — scan
				// on at the fallthrough. BEQ rs==rt is the unconditional-`b`
				// idiom (always taken: everything after is unreachable on the
				// fallthrough) and keeps ending the block.
				if (!((cpuRegs.code >> 26) == 4 && _Rs_ == _Rt_) &&
					eeScanContinuable(scan, startpc, i, _Imm_ * 4 + i + 4))
				{
					s_contSitePcs[s_numContSites++] = i;
					i += 8; // skip the delay slot word in the scan
					continue;
				}
				[[fallthrough]];
			case 20: case 21: // BEQL, BNEL
			case 22: case 23: // BLEZL, BGTZL
				s_branchLoopable = true;
				s_branchTo = _Imm_ * 4 + i + 4;
				// Backward branch into the current block: split so the loop head
				// is its own linkable block. Mirrors x86 iR5900.cpp:2387 and
				// the COP1/COP2 case below.
				if (s_branchTo > startpc && s_branchTo < i)
					s_nEndBlock = eeSuperblockClampSplit(startpc, i, s_branchTo);
				else
				{
					s_nEndBlock = i + 8;
				}
				goto StartRecomp;

			case 16: // COP0
				if (_Rs_ == 16 && _Funct_ == 24) // ERET (no delay slot)
				{
					s_nEndBlock = i + 4;
					goto StartRecomp;
				}
				// Fall through: COP0's branch opcodes line up with COP1/COP2's.
				[[fallthrough]];

			case 17: // COP1
			case 18: // COP2
				if (_Rs_ == 8) // BC0/BC1/BC2 F/T/FL/TL
				{
					s_branchLoopable = true;
					s_branchTo = _Imm_ * 4 + i + 4;
					if (s_branchTo > startpc && s_branchTo < i)
						s_nEndBlock = eeSuperblockClampSplit(startpc, i, s_branchTo);
					else
					{
						s_nEndBlock = i + 8;
					}
					goto StartRecomp;
				}
				break;
		}

		i += 4;
	}

StartRecomp:

	// SL-03: a backward-split can truncate s_nEndBlock below already-recorded
	// continuation sites — drop any site whose branch+delay-slot pair no longer
	// fits inside [startpc, s_nEndBlock). (Sites are ascending; the compile
	// loop never reaches a dropped one, but the analysis passes below must not
	// treat it as an exit boundary either.)
	while (s_numContSites > 0 && s_contSitePcs[s_numContSites - 1] + 8 > s_nEndBlock)
		s_numContSites--;

	// A zero-length block would compile to an unconditional self-linked B
	// with no event check (hard wedge) — eeSuperblockClampSplit's degenerate
	// fallback guarantees this can't happen.
	pxAssert(s_nEndBlock > startpc);

	// Self-modifying code detection: generate inline memory checks for manual blocks.
	const bool needs_loop_snapshot = s_branchLoopable && s_branchTo == startpc && !s_loopSemanticEntryHook;
	a64::Label smc_discard;
	EeInlineSmcSnapshot inline_snapshot;
	const u32 smc_words = (s_nEndBlock - startpc) >> 2;
#ifdef IZPS2_LOCAL_SMC_RANGE_AUDIT
	const auto* const smc_begin = armGetCurrentCodePointer();
#endif
	const bool is_manual_block = memory_protect_recompiled_code(startpc, smc_words, needs_loop_snapshot, &smc_discard, inline_snapshot);
#ifdef IZPS2_LOCAL_SMC_RANGE_AUDIT
	if (is_manual_block)
	{
		std::fprintf(stderr, "LOCAL_SMC_RANGE lo=%llx hi=%llx pc=%08x bytes=%u\n",
			static_cast<unsigned long long>(reinterpret_cast<uptr>(smc_begin)),
			static_cast<unsigned long long>(reinterpret_cast<uptr>(armGetCurrentCodePointer())),
			startpc, smc_words * 4);
	}
#endif

	// SMC protection checks the physical map without changing the guest VTLB.
	// The scan retires its pointer before any fetch that could invoke a handler.
	const bool single_instruction_page = s_nEndBlock > startpc &&
		(s_nEndBlock - 1 - scan.page) < vtlb_private::VTLB_PAGE_SIZE;
	const u8* const scanned_block_bytes = scan.bytes && single_instruction_page ?
		scan.bytes + (startpc - scan.page) : nullptr;

	// Infinite (wait) loop detection — verbatim port of the x86 hazard
	// tracker (ix86-32/iR5900.cpp:2438-2515), replacing the old all-NOP-only
	// scan (AX-07). The idea: as long as a self-loop doesn't write a register
	// it has already read (registers initialised from constants or memory
	// loads excepted) and uses no instruction that alters machine state
	// beyond registers, every iteration does the same thing — so it can be
	// fast-forwarded to the next event. This admits the shape of real
	// hardware-poll idle loops (lw STATUS; andi/test; beq back), which the
	// NOP-only scan never matched. The last-pair skip (i == s_nEndBlock - 8)
	// excludes the backward branch + its delay slot.
	s_nBlockFF = false;
	if (s_branchTo == startpc)
	{
		s_nBlockFF = true;

		u32 reads = 0, loads = 1;

		for (i = startpc; i < s_nEndBlock; i += 4)
		{
			if (i == s_nEndBlock - 8)
				continue;
			if (scanned_block_bytes)
				std::memcpy(&cpuRegs.code, scanned_block_bytes + (i - startpc), sizeof(cpuRegs.code));
			else
				cpuRegs.code = readEeCompileInstruction(i);
			// nop
			if (cpuRegs.code == 0)
				continue;
			// cache, sync
			else if (_Opcode_ == 057 || (_Opcode_ == 0 && _Funct_ == 017))
				continue;
			// imm arithmetic
			else if ((_Opcode_ & 070) == 010 || (_Opcode_ & 076) == 030)
			{
				if (loads & 1 << _Rs_)
				{
					loads |= 1 << _Rt_;
					continue;
				}
				else
					reads |= 1 << _Rs_;
				if (reads & 1 << _Rt_)
				{
					s_nBlockFF = false;
					break;
				}
			}
			// common register arithmetic instructions
			else if (_Opcode_ == 0 && (_Funct_ & 060) == 040 && (_Funct_ & 076) != 050)
			{
				if (loads & 1 << _Rs_ && loads & 1 << _Rt_)
				{
					loads |= 1 << _Rd_;
					continue;
				}
				else
					reads |= 1 << _Rs_ | 1 << _Rt_;
				if (reads & 1 << _Rd_)
				{
					s_nBlockFF = false;
					break;
				}
			}
			// loads
			else if ((_Opcode_ & 070) == 040 || (_Opcode_ & 076) == 032 || _Opcode_ == 067)
			{
				if (loads & 1 << _Rs_)
				{
					loads |= 1 << _Rt_;
					continue;
				}
				else
					reads |= 1 << _Rs_;
				if (reads & 1 << _Rt_)
				{
					s_nBlockFF = false;
					break;
				}
			}
			// mfc*, cfc*
			else if ((_Opcode_ & 074) == 020 && _Rs_ < 4)
			{
				loads |= 1 << _Rt_;
			}
			else
			{
				s_nBlockFF = false;
				break;
			}
		}
	}
	else
	{
		// A timeout loop must branch back to its own start (a self-loop). If the
		// block's terminating branch targets anywhere else, it is NOT a timeout
		// loop and must be recompiled normally. Mirrors x86 iR5900.cpp:2510-2513.
		// Without this guard, the early-exit `bne reg,zero,<forward>` at the TOP
		// of a counted compute loop gets misdetected as a timeout loop and
		// recSkipTimeoutLoop fast-forwards the counter to 0 while skipping the
		// loop BODY's real work. timeout_has_bne alone is insufficient because it
		// matches the forward early-exit branch without checking the branch target.
		is_timeout_loop = false;
	}

#ifdef PCSX2_RECOMPILER_TESTS
	// Test-harness introspection: the wait-loop detection verdict for the
	// block most recently analysed. The FF fast-forward itself is not
	// observable from the harness (both engines are cycle-budget bounded),
	// so the detector (AX-07) is pinned at this seam.
	g_eeRecLastBlockFF = s_nBlockFF;
#endif

	// Instruction analysis (backward pass)
	{
		EEINST* pcur;

		if (s_nInstCacheSize < (s_nEndBlock - startpc) / 4 + 1)
		{
			free(s_pInstCache);
			s_nInstCacheSize = (s_nEndBlock - startpc) / 4 + 10;
			s_pInstCache = (EEINST*)malloc(sizeof(EEINST) * s_nInstCacheSize);
			pxAssert(s_pInstCache != NULL);
		}

		pcur = s_pInstCache + (s_nEndBlock - startpc) / 4;
		_recClearInst(pcur);
		pcur->info = 0;

		// If the wait-loop scan used the fallback reader, a handler may
		// have changed the mapping. Resolve it here as on the original path.
		const u8* block_bytes = scanned_block_bytes;
		if (!block_bytes && CHECK_EEREC && (startpc & 3) == 0 && single_instruction_page)
		{
			const auto mapping = vtlb_private::vtlbdata.vmap[startpc >> vtlb_private::VTLB_PAGE_BITS];
			if (!mapping.isHandler(startpc))
				block_bytes = reinterpret_cast<const u8*>(mapping.assumePtr(startpc));
		}

		bool has_cop2_instructions = false;
		const auto analyse = [&](u32 address) {
			if (block_bytes)
				std::memcpy(&cpuRegs.code, block_bytes + (address - startpc), sizeof(cpuRegs.code));
			else
				cpuRegs.code = readEeCompileInstruction(address);

			pcur[-1] = pcur[0];
			// NOP (sll zero,zero,0) and J leave both EEINST rows
			// untouched in recBackpropBSC.
			const u32 opcode = _Opcode_;
			if (cpuRegs.code && opcode != 2)
				recBackpropBSC(cpuRegs.code, pcur - 1, pcur);
			--pcur;
			has_cop2_instructions |= (opcode == 022 || opcode == 066 || opcode == 076);
		};
		const auto cut_liveness = [&] {
			memset(pcur->regs, EEINST_LIVE, sizeof(pcur->regs));
			memset(pcur->fpuregs, EEINST_LIVE, sizeof(pcur->fpuregs));
			memset(pcur->vfregs, EEINST_LIVE, sizeof(pcur->vfregs));
			memset(pcur->viregs, EEINST_LIVE, sizeof(pcur->viregs));
		};
		// Walk ordinary spans with a single loop bound. A continuation
		// exposes all registers at both its delay slot and its branch.
		i = s_nEndBlock;
		for (int site = s_numContSites - 1; site >= 0; --site)
		{
			const u32 slot_end = s_contSitePcs[site] + 8;
			while (i > slot_end)
			{
				i -= 4;
				analyse(i);
			}
			cut_liveness();
			i -= 4;
			analyse(i);
			cut_liveness();
			i -= 4;
			analyse(i);
		}
		while (i > startpc)
		{
			i -= 4;
			analyse(i);
		}

		// Run COP2 analysis passes — sets EEINST_COP2_SYNC_VU0/FINISH_VU0 flags
		// for conditional VU0 synchronization in transfer ops.
		// SL-03: run per segment, delimited at continuation sites. Both passes
		// defer commits forward (flag-hack elision, micro-finish placement); a
		// deferral must not cross a side exit that can escape before the
		// superseding instruction executes. Per-segment == today's per-block
		// semantics at each former boundary.
		if (has_cop2_instructions)
		{
			u32 seg_start = startpc;
			for (int k = 0; k <= s_numContSites; k++)
			{
				const u32 seg_end = (k < s_numContSites) ? (s_contSitePcs[k] + 8) : s_nEndBlock;
				if (seg_end <= seg_start)
					continue;
				EEINST* const seg_inst = s_pInstCache + 1 + (seg_start - startpc) / 4;
				R5900::COP2MicroFinishPass().Run(seg_start, seg_end, seg_inst);

				if (EmuConfig.Speedhacks.vuFlagHack)
					R5900::COP2FlagHackPass().Run(seg_start, seg_end, seg_inst);

				seg_start = seg_end;
			}
		}
	}

	// Try timeout loop speedhack — if detected, skip normal codegen
	// Timer-poll loops (mfc0 Count / subu / sltu / bne) are NOT skipped because
	// they need to wait for a specific elapsed time — the correct fix is native codegen.
	// Require timeout_reg >= 0 (actually found an addiu) to avoid matching all-nop blocks
	// Skip-MPEG speedhack fires first (short-circuits the timeout-loop check, as
	// in x86); both emit a complete self-contained block + set g_branch/pc.
	const bool doRecompilation = !skipMPEG_By_Pattern(startpc) &&
		!recSkipTimeoutLoop(timeout_reg, is_timeout_loop && timeout_reg >= 0 && timeout_has_bne);

	// ELF/settings hooks have already run. This thread owns both tables.
	s_eeMayHaveDynamicPatches = !EmuConfig.EnablePatches || ps2_patch_has_active_dynamic_patches();

#if PS2_EE_SOFTMEM_COLD_OOL && defined(PS2_HOST_CODE_BRIDGE)
	s_softmemKeepHandlerWithBlock = smc_words <= 128 &&
		recPtr - SysMemory::GetEERec() < 4096 * _1kb;
#elif PS2_EE_SOFTMEM_BLOCK_TAIL
	s_softmemKeepHandlerWithBlock = smc_words <= 48;
#endif

#if defined(PS2_HOST_CODE_BRIDGE) || defined(IZPS2_SWITCH2_EE_WARM)
	if (!warm_batch && (!EmuConfig.EnablePatches || !s_eeMayHaveDynamicPatches))
		capture_ee_block(startpc, smc_words * 4, scanned_block_bytes);
#endif

	// Code generation (forward pass)
	if (doRecompilation)
	{
		// Keep a reference to the slot: nested delay-slot emitters share its state.
		EEINST*& current_inst_info = g_pCurInstInfo;
		current_inst_info = s_pInstCache;
		// Sweep any LDL/LDR fusion residue from an aborted prior compile (the
		// per-pair gate otherwise guarantees same-block consume).
		g_eeUnalignedFused = false;
		// SL-03: sweep side-exit residue the same way (recEmitPendingSideExits
		// drains the list on every completed compile).
		s_numSideExits = 0;
		s_coldIslands.clear();

		// SL-1 candidacy: a resident self-loop is a block whose terminal branch
		// targets its own startpc. Wait-loop-FF blocks keep the fast-forward
		// tail. Forced-manual RAM may use residency only when its frozen
		// snapshot can be checked at every backedge; all other manual paths
		// retain their entry-per-iteration check. Pin GPRs in a preheader and bind the
		// loop-top label after it — see the SL-1 comment block above
		// SetBranchBackedge for the full contract.
		s_loopResident = false;
		s_loopBackedgeEmitted = false;
		if (s_branchLoopable && s_branchTo == startpc && !s_nBlockFF &&
			(!is_manual_block || manualLoopSmcEligible()))
		{
			const int ninsts = static_cast<int>((s_nEndBlock - startpc) / 4);
			int uses[32] = {};
			for (int k = 1; k <= ninsts; k++)
				for (int r = 1; r < 32; r++)
					if (s_pInstCache[k].regs[r] & EEINST_USED)
						uses[r]++;

			struct LoopCand
			{
				int reg;
				int uses;
			};
			LoopCand cands[32];
			int ncand = 0;
			for (int r = 1; r < 32; r++)
			{
				// Pin-table guest regs already live in dedicated mirrors and
				// must never get a scalar allocator home (GE-M2 I1).
				if (uses[r] > 0 && !armEEPinForGPR(r))
					cands[ncand++] = {r, uses[r]};
			}
			std::sort(cands, cands + ncand,
				[](const LoopCand& a, const LoopCand& b) { return a.uses > b.uses; });

			// Leave ≥2 pool regs unpinned for temps/FPRC churn; eviction of a
			// pin is legal anyway (reconcile restores), this just keeps the
			// common body allocation off the pins.
			// A zero-pin candidate (loop-live set entirely pin-table regs) still
			// activates: S0 = empty allocator is valid, and the back-edge still
			// drops the pc-store + link hop.
			constexpr int kMaxLoopPins = 5;
			const int npins = std::min(ncand, kMaxLoopPins);
			for (int k = 0; k < npins; k++)
			{
				// MODE_WRITE up front pessimizes the dirty bit: the loop-top
				// snapshot must claim dirty ⊇ any runtime dirty, or a body-
				// emitted eviction could drop a previous iteration's write.
				const int host = _allocArm64GPR(ARM64TYPE_GPR, cands[k].reg, MODE_READ | MODE_WRITE);
				arm64gprs[host].looppin = 1;
			}
			_clearNeededArm64GPRregs();
			std::memcpy(s_loopEntryState, arm64gprs, sizeof(arm64gprs));
			s_loopTopPc = startpc;
			armAsm->Bind(&s_loopTopLabel.emplace());
			s_loopResident = true;
			s_loopManualSmc = is_manual_block;
		}

		const bool direct_forward = single_instruction_page && scanned_block_bytes &&
			!(s_eeMayHaveDynamicPatches && EmuConfig.EnablePatches);
		// The scan and register analysis only read this direct page. The
		// forward pass still fetches each current instruction word.
		const u8* forward_page = direct_forward && !CHECK_SKIPMPEGHACK ? scan.bytes : nullptr;
		// The allocator tables live at stable addresses on the compiling thread, even
		// when a nested delay-slot emitter changes their contents.
		_arm64gprregs* const gpr_regs = arm64gprs;
		_arm64neonregs* const neon_regs = arm64neon;
		while (!g_branch && pc < s_nEndBlock)
			recompileEeInstruction(current_inst_info, false, false, nullptr,
				gpr_regs, neon_regs, direct_forward ? &forward_page : nullptr);

		s_loopResident = false;
	}

	pxAssert((pc - startpc) >> 2 <= 0xffff);
#if defined(PCSX2_RECOMPILER_TESTS) && PS2_EE_TEST_BLOCK_METADATA
	s_blockContSites[HWADDR(startpc)] = s_curBlockContSites;
#endif
	s_pCurBlockEx->size = (pc - startpc) >> 2;

	const u32 clear_start = HWADDR(startpc);
	const u32 clear_bytes = pc - startpc;
	const u32 physical_end = HWADDR(pc);
	// Query coverage before marking this block. A clear bit also rules
	// out older blocks whose delay slot crossed into this page.
	const bool may_overlap = clear_start > physical_end ||
		physical_end - clear_start != clear_bytes || eeClearMayHitCode(clear_start, physical_end);
	if (clear_bytes && clear_start < Ps2MemSize::TotalRam)
	{
		const u32 first = clear_start >> kEeCodePageShift;
		const u32 last = (clear_start + std::min(clear_bytes, Ps2MemSize::TotalRam - clear_start) - 1) >> kEeCodePageShift;
		for (u32 page = first; page <= last; ++page)
			s_eeCodePages[page / kEeCodePageBits] |= u64{1} << (page % kEeCodePageBits);
	}

	// High-water mark of any compiled block's guest extent, for the
	// stale-overlap walk's scan-back bound below. Reset with the block array.
	s_maxBlockBytes = std::max(s_maxBlockBytes, pc - startpc);
#if PS2_EE_OVERLAP_BUCKET_INDEX
    // New() and size assignment above precede every stale-overlap walk. Record
    // the current completed extent before any recClear can remove neighbours.
    const u32 physical_start = HWADDR(startpc);
    if (physical_start < Ps2MemSize::TotalRam)
    {
        u32& upper = s_overlapUpperEnd[physical_start >> kEeOverlapBucketShift];
        upper = std::max(upper, physical_start + (pc - startpc));
    }
#endif


	if (!(pc & 0x10000000))
		maxrecmem = std::max((pc & ~0xa0000000), maxrecmem);

	// Stale-overlap walk + snapshot (x86 iR5900.cpp:2636-2661, GE-18). Walk
	// OLDER blocks overlapping [startpc, pc) and memcmp each one's recRAMCopy
	// snapshot — taken when THAT block compiled — against live memory. A
	// mismatch means the old block went stale through a write no protection
	// path caught (raw host pokes, protection-bypassing writes); recClear the
	// range so it recompiles. recClear skips s_pCurBlockEx (in-progress), so
	// the re-Get below always finds it. The original port compared the NEW
	// block's not-yet-snapshotted (zeroed) region instead — every compile
	// mismatched and recompile-looped — which is why this was disabled; the
	// old-block-region compare is the correct x86 semantics
	// (OverlapWalkIgnoresUnmodifiedNeighbors pins the no-false-positive side).
	if (physical_end <= Ps2MemSize::ExposedRam)
	{
		BASEBLOCKEX* oldBlock;
		int i = may_overlap ? recBlocks.LastIndex(physical_end - 4) : -1;
		while ((oldBlock = recBlocks[i--]))
		{
            ++s_overlapMetadataVisits;
#if PS2_EE_OVERLAP_BUCKET_INDEX
            // Every live block starting in this bucket ends at or below its
            // monotonic bound. If that bound misses the requested start,
            // jump directly to the preceding bucket's last block. A stale
            // high bound can only refuse this optimization, never hide SMC.
            // Preserve the original global stop BEFORE any bucket jump.
            // Otherwise a bucket known to miss can walk the entire older
            // history despite the pre-existing maximum-block reach bound.
            if (oldBlock->startpc + s_maxBlockBytes <= HWADDR(startpc))
                break;
            const u32 bucket = oldBlock->startpc >> kEeOverlapBucketShift;
            if (oldBlock->startpc < Ps2MemSize::TotalRam &&
                s_overlapUpperEnd[bucket] <= HWADDR(startpc))
            {
                ++s_overlapBucketsSkipped;
                const u32 floor = bucket << kEeOverlapBucketShift;
                if (floor == 0)
                    break;
                const int previous = recBlocks.LastIndex(floor - 4);
                // LastIndex clamps to0 when no start lies below its argument.
                // Never revisit that first block when its whole bucket misses.
                if (previous > i)
                    break;
                i = previous;
                continue;
            }
#endif
			if (oldBlock == s_pCurBlockEx)
				continue;
			if (oldBlock->startpc >= HWADDR(pc))
				continue;
			// recBlocks is ordered by startpc, NOT by end address: a block
			// starting lower can be longer and still reach us, jumping clean
			// over a short one between it and [startpc, pc). So one
			// non-overlapping neighbour is no proof the blocks below it miss
			// too — skip it, and only stop once even the longest block ever
			// compiled could not span the gap. (Upstream x86 breaks here —
			// iR5900.cpp:2688 — and silently skips the stale block below.)
			if (oldBlock->startpc + s_maxBlockBytes <= HWADDR(startpc))
				break;
			if ((oldBlock->startpc + oldBlock->size * 4) <= HWADDR(startpc))
				continue;

			// recRAMCopy is a byte array covering guest main RAM 1:1 — index
			// it by guest address. Do NOT reintroduce the `/ 4` upstream x86
			// still carries (iR5900.cpp:2691/2701): it dates from when
			// recRAMCopy was u32* (dropped in 2010), and it packs the
			// snapshots 4:1 so overlapping blocks overwrite each other's and
			// the compare never matches — each then recClears the other
			// forever. Clamp keeps a block straddling the top of RAM in bounds.
			const u32 cmplen = std::min<u32>(oldBlock->size * 4,
				Ps2MemSize::ExposedRam - oldBlock->startpc);
			if (memcmp(&recRAMCopy[oldBlock->startpc], PSM(oldBlock->startpc), cmplen))
			{
				recClear(startpc, (pc - startpc) / 4);
				s_pCurBlockEx = recBlocks.Get(HWADDR(startpc));
				pxAssert(s_pCurBlockEx && s_pCurBlockEx->startpc == HWADDR(startpc));
				break;
			}
		}

		memcpy(&recRAMCopy[HWADDR(startpc)], PSM(startpc), pc - startpc);
	}

	if (g_branch == 2)
	{
		// Branch taken — flush and dispatch. recBranchCall already accumulated
		// any pre-call cycles into RECCYCLE and re-derived it after the C
		// call, so any further scaleblockcycles_clear() result is the
		// post-call remainder.
		iFlushCall(FLUSH_EVERYTHING);

		emitCycleUpdateAndEventCheck();

		armEmitJmp(DispatcherReg);
	}
	else
	{
		if (g_branch)
		{
			// g_branch == 1: block ended with a branch instruction.
			// pc may not equal s_nEndBlock for branch-likely instructions
			// where the not-taken path skips the delay slot.
		}
		else
		{
			// Non-branch block end (split / page boundary / cycle cap).
			// Mirrors x86 iR5900.cpp:2680-2698:
			//   long block  (>6 insns): SetBranchImm(pc)         — event check + static-linked B
			//   short block (≤6 insns): flush + pc + cycle + bare static-linked B
			// Short blocks skip the event check entirely; a few-insn block can't
			// have advanced cycles far enough to cross an event boundary, so the
			// load+cmp+B.ge is dead-weight code per block tail.
			if (pc != s_nEndBlock)
				Console.Error("EE ARM64: Block end mismatch! startpc=0x%08X pc=0x%08X s_nEndBlock=0x%08X", startpc, pc, s_nEndBlock);
			pxAssert(pc == s_nEndBlock);

			const int numinsts = (pc - startpc) / 4;
			if (numinsts > 6)
			{
				SetBranchImm(pc);
			}
			else
			{
				iFlushCall(FLUSH_EVERYTHING);

				emitEeKnownPc(pc);

				u32 cycles = scaleblockcycles_clear(EeChargeForm::AddImm12);
				if (cycles != 0)
					armAsm->Add(RECCYCLE, RECCYCLE, cycles);
				recEeNoteChargeSite(EeChargeSite::ShortBlockTail);

				if (!emitEeColdRamTail(pc))
					emitEeLinkedBranch(pc);
			}
		}
	}

	// SL-03/SL-10: the taken side exits' in-block footprint — one far-B island
	// each after the tail (mainline pc is dead — size and recRAMCopy
	// bookkeeping ran on it above). The bodies outline into the cold arena
	// below, after the hot block finalizes.
#if PS2_EE_SOFTMEM_COLD_OOL && defined(PS2_HOST_CODE_BRIDGE)
	recEmitCompactSideExits();
#endif
	recEmitSideExitIslands();

	// The mainline and the other islands all exit before this stub.
	// Build the discard arguments only when the entry check fails.
	if (is_manual_block && !inline_snapshot.bytes)
	{
		armAsm->Bind(&smc_discard);
		armAsm->Mov(a64::w0, HWADDR(startpc));
		armAsm->Mov(a64::w1, smc_words);
		armEmitJmp(DispatchBlockDiscard);
	}

	pxAssert(armGetCurrentCodePointer() < SysMemory::GetEERecEnd());

	// Size is from the aligned block_fnptr, not the pre-alignment recPtr —
	// keeps Perf::ee.RegisterPC consistent with the linker's view of the block.
	s_pCurBlockEx->x86size = static_cast<u32>((uptr)armGetCurrentCodePointer() - s_pCurBlockEx->fnptr);

	Perf::ee.RegisterPC((void*)s_pCurBlockEx->fnptr, s_pCurBlockEx->x86size, s_pCurBlockEx->startpc);

#if (PS2_EE_SOFTMEM_COLD_OOL && defined(PS2_HOST_CODE_BRIDGE)) || PS2_EE_SOFTMEM_BLOCK_TAIL
	s_softmemColdHotOpen = false;
#endif
#if defined(PS2_EE_DEFER_HOT_PUBLICATION)
	// Outstanding link sites can only be executed by this VM thread. Finish
	// both emitters and patch the exit branches before returning to EE code.
	const bool has_cold_code = s_numSideExits || !s_coldIslands.empty();
	const bool defer_publication = s_deferHotPublication && has_cold_code;
#ifdef IZPS2_SWITCH2_EE_FINALIZE_BATCH
	// All guest execution is parked until the cold emitter and terminal
	// patches finish. The startup warm path already owns its batch.
	const bool finalize_batch =
#ifdef IZPS2_SWITCH2_EE_INCOMING_BATCH
		incoming_batch ||
#endif
		(has_cold_code && !warm_batch && !THREAD_VU1 &&
			IopThread::GetMode() == IopThreadMode::Off &&
			(defer_publication ||
				(VMManager::Internal::HasBootedELF() && !EmuConfig.Gamefixes.GoemonTlbHack &&
					(!EmuConfig.EnablePatches || !ps2_patch_has_active_dynamic_patches()))) &&
			iz_ps2_begin_ee_warm_ranges());
#endif
#if defined(IZPS2_SWITCH2_EE_WARM_ALIGN) && !defined(PS2_HOST_CODE_BRIDGE)
	if (ordered_warm)
		recPtr = armEndOrderedEeWarmBlock(!defer_publication);
	else
#endif
		recPtr = defer_publication ? armEndBlockUnpublished() : armEndBlock();
#else
	recPtr = armEndBlock();
#endif
#if (PS2_EE_SOFTMEM_COLD_OOL && defined(PS2_HOST_CODE_BRIDGE)) || PS2_EE_SOFTMEM_BLOCK_TAIL
	recClearSoftmemColdVeneers(); // all labels resolved; cold targets already finalized
#endif

	// SL-10: outline the side-exit bodies into the cold arena and patch the
	// islands. Runs as its own emission session so the bodies land outside
	// the hot compile-order stream.
#if defined(PS2_EE_DEFER_HOT_PUBLICATION)
	const uptr hot_end = reinterpret_cast<uptr>(recPtr);
	recEmitColdSideExits(s_coldPtr == cold_before_block, defer_publication ? block_fnptr : 0,
		defer_publication ? hot_end : 0, ordered_warm);
	if (defer_publication)
		HostSys::FlushInstructionCache(reinterpret_cast<void*>(block_fnptr), static_cast<u32>(hot_end - block_fnptr));
#ifdef IZPS2_SWITCH2_EE_FINALIZE_BATCH
	if (finalize_batch)
		iz_ps2_end_ee_warm_ranges();
#endif
#else
	recEmitColdSideExits(s_coldPtr == cold_before_block);
#endif
	s_eeMayHaveDynamicPatches = true;

	pxAssert((g_cpuHasConstReg & g_cpuFlushedConstReg) == g_cpuHasConstReg);

	s_pCurBlock = NULL;
	s_pCurBlockEx = NULL;
#ifdef PS2_HOST_CODE_BRIDGE
    // Hot/cold emitters, pool and terminal island patches all finalized.
    // Failed/unknown publication retains ownership; this call cannot return
    // to generated EE code until every unique dirty page is committed.
    if (!warm_batch)
    {
        const u64 commit_begin = GetCPUTicks();
        armCommitEEPublicationTransaction();
        ps2_native_ee_compile_timing.Complete(compile_begin, commit_begin, GetCPUTicks());
    }
    s_eeWarmPublicationOpen = false;
#endif
#if PS2_EE_RESTORE_CURRENT_ENTRY
    // The stale-overlap walk can clear the LUT word at this very startpc
    // while deliberately retaining its in-progress BASEBLOCKEX. New() has
    // already bound that metadata to block_fnptr. Restore ONLY this freshly
    // completed entry after successful publication; untouched neighbours
    // retain their existing invalidation/boundary and guest-cycle policy.
    // No old metadata target is ever resurrected here.
    GETBLOCK(startpc)->SetFnptr(block_fnptr);
#endif
#ifdef IZPS2_RUN24_EE_TIMING
	const auto generation_end = iz_system_ticks();
	const auto publication_end = HostSys::GetJitCacheTiming().ticks;
	++s_eeCompileAudit.blocks;
	s_eeCompileAudit.ticks += generation_end - generation_begin;
	s_eeCompileAudit.publication_ticks += publication_end - publication_begin;
#endif
}

// =====================================================================================================
//  Thunk helpers for fastmem backpatching
// =====================================================================================================

#if defined(PS2_HOST_CODE_BRIDGE) || defined(IZPS2_SWITCH2_EE_WARM)
// Called on the EE owner at a quiescent point with verified game RAM.
// Publish before any guest instruction executes or before a normal cache reset.
static u32 recEeWarmBatchImpl(std::span<const ps2::ee_warm::Entry> entries, bool ordered)
{
	if (entries.empty() || entries.size() > ps2::ee_warm::BatchSize || eeCpuExecuting || !CHECK_EEREC ||
#ifdef PS2_HOST_CODE_BRIDGE
		!ps2_mtvu_caller_is_ee() || !ps2_mtvu_code_host_caller_idle() ||
#else
		THREAD_VU1 || IopThread::GetMode() != IopThreadMode::Off ||
#endif
		!VMManager::Internal::HasBootedELF() || EmuConfig.Gamefixes.GoemonTlbHack ||
		(EmuConfig.EnablePatches && ps2_patch_has_active_dynamic_patches()))
		return 0;

	const u32 saved_pc = cpuRegs.pc;
	const u32 saved_code = cpuRegs.code;
	u32 consumed = 0;
#ifdef PS2_HOST_CODE_BRIDGE
	bool open = false;
#elif defined(IZPS2_SWITCH2_EE_WARM_RANGES)
	const bool open = iz_ps2_begin_ee_warm_ranges();
#endif
	for (const auto& entry : entries)
	{
		const auto [address, bytes] = entry;
		const u32 page = address & ~u32{0xfff};
		const u32 next_page = page + _4kb;
		if (!eeWarmSpanEligible(address, bytes) ||
			eeRecNeedsReset || recPtr >= recPtrEnd || s_coldPtr >= s_coldPtrEnd)
		{
			if (ordered)
				break;
			continue;
		}

		// The scanner can fetch a terminal delay slot in the following page.
		// Both pages must be ordinary RAM, without a read handler or an alias.
		const auto mapping = vtlb_private::vtlbdata.vmap[page >> vtlb_private::VTLB_PAGE_BITS];
		const auto next_mapping = vtlb_private::vtlbdata.vmap[next_page >> vtlb_private::VTLB_PAGE_BITS];
		if (mapping.isHandler(page) || next_mapping.isHandler(next_page) ||
			mapping.assumePtr(page) != reinterpret_cast<uptr>(eeMem->Main + page) ||
			next_mapping.assumePtr(next_page) != reinterpret_cast<uptr>(eeMem->Main + next_page) ||
			GETBLOCK(address) != &recRAM[address / 4] ||
			!eeWarmDelaySlotVerified(eeMem->Main + address, bytes))
		{
			if (ordered)
				break;
			continue;
		}
		if (GETBLOCK(address)->GetFnptr() != reinterpret_cast<uptr>(JITCompile))
		{
			++consumed;
			continue;
		}

#ifdef PS2_HOST_CODE_BRIDGE
		if (!open)
		{
			armBeginEEPublicationTransaction(SysMemory::GetEERec(),
				static_cast<size_t>(SysMemory::GetEERecEnd() - SysMemory::GetEERec()));
			open = true;
		}
#endif
		cpuRegs.pc = address;
		recRecompileBlock(address, true, address + bytes, ordered);
		++consumed;
	}
#ifdef PS2_HOST_CODE_BRIDGE
	if (open)
		armCommitEEPublicationTransaction();
#elif defined(IZPS2_SWITCH2_EE_WARM_RANGES)
	if (open)
		iz_ps2_end_ee_warm_ranges();
#endif
	cpuRegs.pc = saved_pc;
	cpuRegs.code = saved_code;
	return consumed;
}

__attribute__((visibility("hidden"))) u32 recEeWarmBatch(std::span<const ps2::ee_warm::Entry> entries)
{
	return recEeWarmBatchImpl(entries, false);
}

#if defined(IZPS2_SWITCH2_EE_WARM)
__attribute__((visibility("hidden"))) u32 recEeWarmOrderedBatch(std::span<const ps2::ee_warm::Entry> entries)
{
	return recEeWarmBatchImpl(entries, true);
}
#endif
#endif

static void recRecompile(u32 startpc)
{
#ifdef IZPS2_SWITCH2_STARTUP_TRACE
	const unsigned trace = s_ee_startup_compiles;
	if (trace)
	{
		--s_ee_startup_compiles;
		Console.WriteLn("IZPS2_EE_STARTUP phase=compile_begin pc=%08x remaining=%u ee_cycle=%llu",
			startpc, trace - 1, static_cast<unsigned long long>(cpuRegs.cycle));
	}
#endif
    u32 trace_end = 0;
#ifdef IZPS2_SWITCH2_STARTUP_TRACE
    if (s_ee_trace_window_words && startpc >= s_ee_trace_window_start && startpc < s_ee_trace_window_end)
        trace_end = std::min(s_ee_trace_window_end, startpc + 4 * s_ee_trace_window_words);
#endif
    recRecompileBlock(startpc, false, trace_end);
#ifdef IZPS2_SWITCH2_STARTUP_TRACE
    if (trace_end) {
        const uptr target = GETBLOCK(startpc)->GetFnptr();
        const uptr cursor = reinterpret_cast<uptr>(recPtr);
        auto* rx = reinterpret_cast<u8*>(target);
        const bool valid = target >= reinterpret_cast<uptr>(SysMemory::GetEERec()) &&
            target < cursor && cursor <= reinterpret_cast<uptr>(SysMemory::GetEERecEnd());
        const size_t size = valid ? cursor - target : 0;
        const bool match = valid && std::memcmp(rx, armGetWritableCodePtr(rx), size) == 0;
        Console.WriteLn("IZPS2_EE_TRACE_WINDOW pc=%08x scan_end=%08x bytes=%zu rx_match=%d",
            startpc, trace_end, size, match ? 1 : 0);
    }
	if (trace)
	{
		const uptr address = GETBLOCK(startpc)->GetFnptr();
		const auto target = reinterpret_cast<const void*>(address);
		const uptr begin = reinterpret_cast<uptr>(SysMemory::GetEERec());
		const uptr end = reinterpret_cast<uptr>(SysMemory::GetEERecEnd());
		const bool readable = address >= begin && address < end && sizeof(u32) <= end - address;
		u32 first = 0;
		if (readable)
			std::memcpy(&first, target, sizeof(first));
		Console.WriteLn("IZPS2_EE_STARTUP phase=compile_complete pc=%08x target=%p remaining=%u rx_valid=%d rx_first=%08x",
			startpc, target, trace - 1, readable ? 1 : 0, first);
	}
#endif
}

u8* recBeginThunk()
{
	// Check for recompiler cache overflow
	if (recPtr >= recPtrEnd)
		eeRecNeedsReset = true;

	// Set up assembler to emit thunk code at the current recompiler pointer.
	// No constant pool needed for thunks (they're small and self-contained).
	armSetAsmPtr(recPtr, recPtrEnd - recPtr + _64kb, nullptr);
	u8* aligned = armStartBlock();
	armAsm->bti(a64::EmitBTI_jc);

	// Return the aligned address where code actually starts, not recPtr.
	// armStartBlock() aligns to 16 bytes — branching to recPtr would hit padding.
	return aligned;
}

u8* recEndThunk()
{
	recPtr = armEndBlock();
	pxAssert(recPtr < SysMemory::GetEERecEnd());
	return recPtr;
}

// =====================================================================================================
//  R5900cpu struct — public interface
// =====================================================================================================

R5900cpu recCpu = {
	recReserve,
	recShutdown,
	recResetEE,
	recStep,
	recExecute,
	recSafeExitExecution,
	recCancelInstruction,
	recClear,
};
