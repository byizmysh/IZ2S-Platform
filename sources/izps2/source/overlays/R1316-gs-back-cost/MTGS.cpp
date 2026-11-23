#ifdef __SWITCH2__
#include "gs-pool-wait.h"
#include "gs-back-cost.h"
#endif
#include "gs-back-close.h"
#include "gs-back-native.h"
#include "gif-retirement-wait.h"
#include "GS/GSCompileStats.h"
#include <algorithm>
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "EECycleRateSampler.h"
#include "GS.h"
#include "Gif_Unit.h"
#include "MTGS.h"
#include "ps2_mtgs_vsync_queue.h"
#include "mtgs-submit-trace.h"
#include "mtgs-command-trace.h"
#include "gs-field-profile.h"
#include "nfs-gs-peaks.h"

#include <cstdio>
#include <fmt/format.h>
#include <iterator>
#include "MTVU.h"
#include "Host.h"
#include "IconsFontAwesome.h"
#include "PerformanceMetrics.h"
#include "VMManager.h"

#include "common/FPControl.h"
#include "common/HostSys.h"
#include "common/ScopedGuard.h"
#include "common/StringUtil.h"
#include "common/Timer.h"
#include "common/WrappedMemCopy.h"

#include <list>
#include <mutex>
#include <thread>
#include <vector>

// Uncomment this to enable profiling of the GS RingBufferCopy function.
//#define PCSX2_GSRING_SAMPLING_STATS

#if 0 //PCSX2_DEBUG
#define MTGS_LOG Console.WriteLn
#else
#define MTGS_LOG(...) \
	do                \
	{                 \
	} while (0)
#endif

extern uint g_FrameCount;

namespace MTGS
{
	void ConfigureVsyncSubmitTrace(bool enabled);
	void ReportVsyncSubmitTrace();
	void ConfigureGifRetirementWait(bool enabled);
#ifdef IZPS2_SWITCH2_WIDE_MTGS
	// Scale only the host command ring. The GIF packet queue has its own layout.
	static constexpr uint CommandRingSize = IZPS2_SWITCH2_WIDE_MTGS * RingBufferSize;
#else
	static constexpr uint CommandRingSize = RingBufferSize;
#endif
	static_assert((CommandRingSize & (CommandRingSize - 1)) == 0);
	static constexpr uint CommandRingMask = CommandRingSize - 1;

	struct BufferedData
	{
		u128 m_Ring[CommandRingSize];
		u8 Regs[Ps2MemSize::GSregs];

		u128& operator[](uint idx)
		{
			pxAssert(idx < CommandRingSize);
			return m_Ring[idx];
		}
	};

	static void ThreadEntryPoint();
	static void MainLoop();

	static void GenericStall(uint size);

	static void PrepDataPacket(Command cmd, u32 size);
	static void PrepDataPacket(GIF_PATH pathidx, u32 size);
	static void SendDataPacket(MtgsSubmitTrace::Sample* sample = nullptr);

	static void SendSimplePacket(Command type, int data0, int data1, int data2);
	static void SendSimpleGSPacket(Command type, u32 offset, u32 size, GIF_PATH path);
	static void SendPointerPacket(Command type, u32 data0, void* data1);
	static void _FinishSimplePacket();
	static u8* GetDataPacketPtr();

	static void SetEvent();

	alignas(__cachelinesize) BufferedData RingBuffer;

	// note: when m_ReadPos == m_WritePos, the fifo is empty
	// Threading info: m_ReadPos is updated by the MTGS thread. m_WritePos is updated by the EE thread
	alignas(__cachelinesize) static std::atomic<unsigned int> s_ReadPos; // cur pos gs is reading from
	alignas(__cachelinesize) static std::atomic<unsigned int> s_WritePos; // cur pos ee thread is writing to

	// These vars maintain instance data for sending Data Packets.
	// Only one data packet can be constructed and uploaded at a time.
	static u32 s_packet_startpos; // size of the packet (data only, ie. not including the 16 byte command!)
	static u32 s_packet_size; // size of the packet (data only, ie. not including the 16 byte command!)
	static u32 s_packet_writepos; // index of the data location in the ringbuffer.

	static std::atomic<bool> s_SignalRingEnable;
	static std::atomic<int> s_SignalRingPosition;

	static MtgsVsyncQueue s_VsyncQueue;
	// This trace is owned by the EE producer. The GS worker does not read it.
	static MtgsSubmitTrace s_VsyncSubmitTrace;
	static MtgsCommandTrace s_GsCommandTrace;
	static constexpr unsigned GsTraceVsyncLimit = 48;

	static std::mutex s_mtx_RingBufferBusy2; // Gets released on semaXGkick waiting...
	static Threading::WorkSema s_sem_event;
	static Threading::UserspaceSemaphore s_sem_OnRingReset;
	static Threading::UserspaceSemaphore s_sem_Vsync;

	// Used to delay the sending of events.  Performance is better if the ringbuffer
	// has more than one command in it when the thread is kicked.
	static int s_CopyDataTally;

#ifdef RINGBUF_DEBUG_STACK
	static std::mutex s_lock_Stack;
	static std::list<uint> ringposStack;
#endif

	static Threading::Thread s_thread;
	static std::atomic_bool s_open_flag{false};
	static std::atomic_bool s_shutdown_flag{false};
	static std::atomic_bool s_run_idle_flag{false};
	static Threading::UserspaceSemaphore s_open_or_close_done;
	static GifRetirementWait s_gif_retirement;
	static std::atomic_bool s_gif_progress_requested{false};
	static std::atomic_bool s_gif_progress_active{false};
} // namespace MTGS

// =====================================================================================================
//  MTGS Threaded Class Implementation
// =====================================================================================================

const Threading::ThreadHandle& MTGS::GetThreadHandle()
{
	return s_thread;
}

bool MTGS::IsOpen()
{
	return s_open_flag.load(std::memory_order_acquire);
}

void MTGS::StartThread()
{
	if (s_thread.Joinable())
		return;

	pxAssertRel(!s_open_flag.load(), "GS thread should not be opened when starting");
	s_sem_event.Reset();
	s_shutdown_flag.store(false, std::memory_order_release);
	s_thread.Start(&MTGS::ThreadEntryPoint);
}

void MTGS::ShutdownThread()
{
	if (!s_thread.Joinable())
		return;

	// just go straight to shutdown, don't wait-for-open again
	s_shutdown_flag.store(true, std::memory_order_release);
	if (IsOpen())
		WaitForClose();

	// make sure the thread actually exits
	s_sem_event.NotifyOfWork();
	s_thread.Join();
}


namespace GSFieldProfile
{
static Capture s_capture;

uint64_t WallTicks() { return GetCPUTicks(); }
uint64_t CpuTicks() { return Threading::GetThreadCpuTime(); }
static constexpr std::array WaitNames{"vsync_slot", "ring_drain", "ring_space", "gif_buffer", "gif_retirement"};
static constexpr std::array CommandNames{"gif1", "gif2", "gif3", "vsync", "freeze", "reset",
    "soft_reset", "gif", "mtvu_gif", "fifo_sync", "fifo_async", "async_call"};
static_assert(CommandNames.size() == static_cast<unsigned>(MTGS::Command::AsyncCall) + 1);
enum WaitSite : uint32_t { VsyncSlot, RingDrain, RingSpace, GifBuffer, GifRetirement };
static NfsGsPeaks<WaitNames.size(), 12> s_ee_waits;
static NfsGsPeaks<CommandNames.size(), 12> s_gs_commands;
static bool s_nfs_ee_waits;
static bool s_nfs_gs_commands;
static uint32_t s_last_field;

struct PipelineField
{
    uint32_t field;
    uint64_t foreground_ns;
    uint64_t spirv_ns;
    uint64_t module_ns;
    uint64_t pipeline_ns;
    uint64_t misses;
};

static std::array<PipelineField, 24> s_pipeline_peaks;
static PipelineField s_previous_pipeline;

static PipelineField ReadPipelineField(uint32_t field)
{
    using namespace GSCompileStats;
    return {field, Get(GSThreadStallNs), Get(SpirvCompileNs), Get(ModuleCreateNs),
        Get(PipelineCreateNs), Get(TFXPipelineMisses)};
}

static void NotePipelineField(uint32_t field)
{
    const auto now = ReadPipelineField(field);
    const auto previous = s_previous_pipeline;
    s_previous_pipeline = now;
    if (now.foreground_ns - previous.foreground_ns < 2'000'000)
        return;

    auto least = std::min_element(s_pipeline_peaks.begin(), s_pipeline_peaks.end(),
        [](const auto& a, const auto& b) { return a.foreground_ns < b.foreground_ns; });
    if (now.foreground_ns - previous.foreground_ns > least->foreground_ns)
    {
        *least = {field, now.foreground_ns - previous.foreground_ns,
            now.spirv_ns - previous.spirv_ns, now.module_ns - previous.module_ns,
            now.pipeline_ns - previous.pipeline_ns, now.misses - previous.misses};
    }
}


void StartNfsEeWaits()
{
    s_ee_waits.Start(GetTickFrequency() / 200);
    s_nfs_ee_waits = true;
}

void StopNfsEeWaits() { s_nfs_ee_waits = false; }

static void BeginLiveEeWait(WaitSite kind, uint64_t begin)
{
    if (begin && s_nfs_ee_waits && NfsGsLive::Enabled.load(std::memory_order_relaxed))
        NfsGsLive::EeWait.Begin(kind, begin);
}

static void NoteEeWait(WaitSite kind, uint64_t begin)
{
    // Nonzero BeginWait timestamps are issued to the EE owner only.
    if (begin && s_nfs_ee_waits)
    {
        if (NfsGsLive::Enabled.load(std::memory_order_relaxed))
            NfsGsLive::EeWait.End();
        s_ee_waits.Add({begin, GetCPUTicks(), 0, g_FrameCount + 1, kind});
    }
}

void Start(bool nfs)
{
    s_nfs_gs_commands = nfs;
    if (nfs)
    {
        s_gs_commands.Start(GetTickFrequency() / 125);
        s_last_field = 0;
        s_pipeline_peaks = {};
        s_previous_pipeline = ReadPipelineField(0);
    }
    s_capture.Start(nfs);
}

template <size_t Kinds, size_t Peaks>
static void AppendPeaks(std::string& out, const char* lane, const std::array<const char*, Kinds>& names,
    const NfsGsPeaks<Kinds, Peaks>& trace)
{
    for (size_t kind = 0; kind < Kinds; ++kind)
    {
        const auto& total = trace.Counts()[kind];
        fmt::format_to(std::back_inserter(out), "IZPS2_NFS_GS_BUCKET lane={} kind={} calls={} ticks={}\n",
            lane, names[kind], total.calls, total.ticks);
        for (const auto& row : trace.Rows()[kind])
        {
            if (row.begin != row.end)
                fmt::format_to(std::back_inserter(out),
                    "IZPS2_NFS_GS_PEAK lane={} kind={} field={} begin_tick={} end_tick={} mtvu_wait_ticks={}\n",
                    lane, names[kind], row.field, row.begin, row.end, row.mtvu_wait);
        }
    }
}

std::string FormatReport()
{
    std::string out;
    out.reserve(s_capture.Size() * 512 + 4096);
    fmt::format_to(std::back_inserter(out), "IZPS2_GS_FIELD_PROFILE samples={} wall_hz={} cpu_hz={} discontinuities={}\n",
        s_capture.Size(), GetTickFrequency(), Threading::GetThreadTicksPerSecond(), s_capture.Discontinuities());
    if (s_nfs_gs_commands)
    {
        AppendPeaks(out, "ee", WaitNames, s_ee_waits);
        AppendPeaks(out, "gs", CommandNames, s_gs_commands);
        for (const auto& row : s_pipeline_peaks)
        {
            if (row.foreground_ns)
                fmt::format_to(std::back_inserter(out),
                    "IZPS2_NFS_TFX_FIELD field={} foreground_ns={} spirv_ns={} module_ns={} pipeline_ns={} misses={}\n",
                    row.field, row.foreground_ns, row.spirv_ns, row.module_ns, row.pipeline_ns, row.misses);
        }
    }
    for (size_t i = 0; i < s_capture.Size(); ++i)
    {
        const auto& row = s_capture[i];
        char line[1152];
        int bytes = std::snprintf(line, sizeof(line), "IZPS2_GS_FIELD field=%u begin_tick=%llu wall=%llu cpu=%llu cpu_valid=%u", row.field,
            static_cast<unsigned long long>(row.begin), static_cast<unsigned long long>(row.wall), static_cast<unsigned long long>(row.cpu), unsigned(row.cpu_valid));
        for (size_t slot = 0; slot < Names.size() && bytes > 0 && static_cast<size_t>(bytes) < sizeof(line); ++slot)
        {
            const int written = std::snprintf(line + bytes, sizeof(line) - bytes, " %s=%llu/%u", Names[slot],
                static_cast<unsigned long long>(row.ticks[slot]), row.calls[slot]);
            bytes = written > 0 ? bytes + written : -1;
        }
        if (bytes > 0 && static_cast<size_t>(bytes) < sizeof(line))
        {
            out.append(line, static_cast<size_t>(bytes));
            out.push_back('\n');
        }
    }
    return out;
}
}

void MTGS::ThreadEntryPoint()
{
	Threading::SetNameOfCurrentThread("GS");
	PerformanceMetrics::AdpfRegisterCallingThread(); // ADPF: hint the GS thread's core (Android)

	// GS can hit SMC write traps when executing InitAndReadFIFO
	// As racey as it sounds, it should be safe, since InitAndReadFIFO is requested and immediately waited for,
	// so the EE thread shouldn't be touching any of its codegen structures while it happens.
	PageFaultHandler::InstallSecondaryThread();

	// Explicitly set rounding mode to default (nearest, FTZ off).
	// Otherwise it appears to get inherited from the EE thread on Linux.
	FPControlRegister::SetCurrent(FPControlRegister::GetDefault());

	for (;;)
	{
		// wait until we're actually asked to initialize (and config has been loaded, etc)
		while (!s_open_flag.load(std::memory_order_acquire))
		{
			if (s_shutdown_flag.load(std::memory_order_acquire))
			{
				s_sem_event.Kill();
				return;
			}

			s_sem_event.WaitForWork();
		}

		// try initializing.. this could fail
		std::memcpy(RingBuffer.Regs, PS2MEM_GS, sizeof(PS2MEM_GS));
		const bool opened = GSopen(EmuConfig.GS, EmuConfig.GS.Renderer, RingBuffer.Regs,
			VMManager::GetEffectiveVSyncMode(), VMManager::ShouldAllowPresentThrottle());
		s_open_flag.store(opened, std::memory_order_release);

		// notify emu thread that we finished opening (or failed)
		s_open_or_close_done.Post();

		// are we open?
		if (!opened)
		{
			// wait until we're asked to try again...
			continue;
		}

#ifdef IZPS2_SWITCH2_WIDE_MTGS
		Console.WriteLn("IZPS2_MTGS commandRingBytes=%zu", sizeof(RingBuffer.m_Ring));
#endif
		// we're ready to go
		MainLoop();
#if defined(__SWITCH2__) || defined(IZPS2_TEST_HOSTED_GS_BACK)
		GSBackClose::Mtgs.store(GSBackClose::MtgsPhase::StoppingCapture, std::memory_order_release);
#endif
		GSFieldProfile::s_capture.Stop();

		// when we come back here, it's because we closed (or shutdown)
		// that means the emu thread should be blocked, waiting for us to be done
		pxAssertRel(!s_open_flag.load(std::memory_order_relaxed), "Open flag is clear on close");
		GSclose();

		// MainLoop killed the work semaphore. Reset it before acknowledging
		// closure so a subsequent open or shutdown can publish a wake safely.
		s_sem_event.Reset();
#if defined(__SWITCH2__) || defined(IZPS2_TEST_HOSTED_GS_BACK)
		GSBackClose::Mtgs.store(GSBackClose::MtgsPhase::Closed, std::memory_order_release);
#endif
		s_open_or_close_done.Post();
	}
}

void MTGS::ResetGS(bool hardware_reset)
{
	// MTGS Reset process:
	//  * clear the ringbuffer.
	//  * Signal a reset.
	//  * clear the path and byRegs structs (used by GIFtagDummy)

	if (hardware_reset)
	{
		s_ReadPos = s_WritePos.load();
		if (s_VsyncQueue.Clear())
			s_sem_Vsync.Post();
	}

	MTGS_LOG("MTGS: Sending Reset...");
	SendSimplePacket(Command::Reset, static_cast<int>(hardware_reset), 0, 0);

	if (hardware_reset)
		SetEvent();
}

int MTGS::GetCurrentVsyncQueueSize()
{
	return s_VsyncQueue.Pending();
}

struct RingCmdPacket_Vsync
{
	u8 regset1[0x0f0];
	u32 csr;
	u32 imr;
	GSRegSIGBLID siglblid;

	// must be 16 byte aligned
	u32 registers_written;
	u32 profile_field;
	u32 pad[2];
};
static_assert(sizeof(RingCmdPacket_Vsync) % 16 == 0);
static_assert(offsetof(RingCmdPacket_Vsync, profile_field) == 0xf0 + 5 * sizeof(u32));

void MTGS::PostVsyncStart(bool registers_written)
{
	// Optimization note: Typically regset1 isn't needed.  The regs in that area are typically
	// changed infrequently, usually during video mode changes.  However, on modern systems the
	// 256-byte copy is only a few dozen cycles -- executed 60 times a second -- so probably
	// not worth the effort or overhead of trying to selectively avoid it.

	uint packsize = sizeof(RingCmdPacket_Vsync) / 16;
	MtgsSubmitTrace::Sample* sample = nullptr;
	if (s_VsyncSubmitTrace.Active())
	{
		const auto cpu = Threading::GetThreadCpuTime();
		sample = &s_VsyncSubmitTrace.Begin(g_FrameCount, GetCPUTicks(), cpu);
	}
	PrepDataPacket(Command::VSync, packsize);
	if (sample) sample->Mark(MtgsSubmitPhase::ReserveRing, GetCPUTicks());
	MemCopy_WrappedDest((u128*)PS2MEM_GS, RingBuffer.m_Ring, s_packet_writepos, CommandRingSize, 0xf);

	u32* remainder = (u32*)GetDataPacketPtr();
	remainder[0] = GSCSRr;
	remainder[1] = GSIMR._u32;
	(GSRegSIGBLID&)remainder[2] = GSSIGLBLID;
	remainder[4] = static_cast<u32>(registers_written);
	// Counters.cpp observes this field on the EE after incrementing g_FrameCount.
	remainder[5] = g_FrameCount + 1;
	s_packet_writepos = (s_packet_writepos + 2) & CommandRingMask;
	if (sample) sample->Mark(MtgsSubmitPhase::CopyRegisters, GetCPUTicks());

	// Account for the command before SendDataPacket publishes the write index.
	s_VsyncQueue.Reserve();
	if (sample) sample->Mark(MtgsSubmitPhase::ReserveVsync, GetCPUTicks());
	SendDataPacket(sample);
	if (sample) sample->Mark(MtgsSubmitPhase::Publish, GetCPUTicks());

	// Keep the queue limit for input latency. If GS completed work while we
	// were publishing, the registration checks the now-current depth.
	const bool wait = s_VsyncQueue.ArmIfOverLimit(EmuConfig.GS.VsyncQueueSize);
	if (sample) sample->Mark(MtgsSubmitPhase::ArmWait, GetCPUTicks());
	if (s_CopyDataTally != 0 || wait)
		SetEvent();
	if (sample) sample->Mark(MtgsSubmitPhase::Wake, GetCPUTicks());

	if (wait)
	{
		const u64 wait_begin = EECycleRateSampler::BeginWait();
		GSFieldProfile::BeginLiveEeWait(GSFieldProfile::VsyncSlot, wait_begin);
		s_sem_Vsync.Wait();
		GSFieldProfile::NoteEeWait(GSFieldProfile::VsyncSlot, wait_begin);
		EECycleRateSampler::EndGSWait(wait_begin);
	}
	if (sample)
	{
		sample->Mark(MtgsSubmitPhase::Wait, GetCPUTicks());
		s_VsyncSubmitTrace.Finish(Threading::GetThreadCpuTime(), wait);
		if (s_VsyncSubmitTrace.Observed() == GsTraceVsyncLimit)
			s_GsCommandTrace.Stop();
	}
}

void MTGS::ConfigureVsyncSubmitTrace(bool enabled)
{
	const auto hz = GetTickFrequency();
	s_VsyncSubmitTrace.Reset(enabled, hz / 100);
	if (enabled)
		s_GsCommandTrace.Start(hz / 200);
	else
		s_GsCommandTrace.Stop();
}

void MTGS::ReportVsyncSubmitTrace()
{
	const auto cpu_hz = Threading::GetThreadTicksPerSecond();
	const auto system_hz = GetTickFrequency();
	const auto ms = [system_hz](u64 ticks) { return 1000.0 * ticks / system_hz; };
	s_GsCommandTrace.Stop();
	Console.WriteLn("IZPS2_MTGS_SUBMIT_TRACE observed=%u qualifying=%u retained=%u",
		s_VsyncSubmitTrace.Observed(), s_VsyncSubmitTrace.Qualifying(), s_VsyncSubmitTrace.Retained());
	for (unsigned i = 0; i < s_VsyncSubmitTrace.Retained(); ++i)
	{
		const auto& row = s_VsyncSubmitTrace.Get(i);
		Console.WriteLn("IZPS2_MTGS_SUBMIT start=%u wall_ms=%.3f cpu_ms=%.3f waited=%u "
			"reserve_ring_ms=%.3f copy_regs_ms=%.3f reserve_vsync_ms=%.3f publish_ms=%.3f "
			"publish_wake_ms=%.3f arm_ms=%.3f wake_ms=%.3f ack_ms=%.3f "
			"begin_tick=%llu end_tick=%llu ack_begin_tick=%llu",
			row.frame, ms(row.last - row.start), cpu_hz ? 1000.0 * row.cpu / cpu_hz : 0.0, unsigned(row.waited),
			ms(row.spans[0]), ms(row.spans[1]), ms(row.spans[2]), ms(row.spans[3]),
			ms(row.spans[4]), ms(row.spans[5]), ms(row.spans[6]), ms(row.spans[7]),
			static_cast<unsigned long long>(row.start), static_cast<unsigned long long>(row.last),
			static_cast<unsigned long long>(row.last - row.spans[7]));
	}
	const unsigned commands = s_GsCommandTrace.Size();
	Console.WriteLn("IZPS2_MTGS_COMMAND_TRACE hz=%llu retained=%u limit=%u vsync_limit=%u",
		static_cast<unsigned long long>(system_hz), commands, MtgsCommandTrace::Capacity, GsTraceVsyncLimit);
	for (unsigned i = 0; i < commands; ++i)
	{
		const auto& row = s_GsCommandTrace.Get(i);
		Console.WriteLn("IZPS2_MTGS_COMMAND kind=%u ring_pos=%u queued_qw=%u wall_ms=%.3f begin_tick=%llu end_tick=%llu "
			"mtvu_wait_ms=%.3f shader_source_ms=%.3f spirv_compiles=%llu spirv_ms=%.3f shader_module_ms=%.3f "
			"pipeline_creates=%llu pipeline_ms=%.3f tfx_misses=%llu tfx_stall_ms=%.3f precompile_wait_ms=%.3f",
			row.command, row.ring_position, row.pending_quadwords, ms(row.end - row.begin),
			static_cast<unsigned long long>(row.begin), static_cast<unsigned long long>(row.end),
			ms(row.mtvu_wait), row.compile[MtgsCommandTrace::ShaderSource] / 1000000.0, static_cast<unsigned long long>(row.compile[MtgsCommandTrace::SpirvCount]),
			row.compile[MtgsCommandTrace::SpirvCompile] / 1000000.0, row.compile[MtgsCommandTrace::ShaderModule] / 1000000.0, static_cast<unsigned long long>(row.compile[MtgsCommandTrace::PipelineCount]),
			row.compile[MtgsCommandTrace::PipelineCreate] / 1000000.0, static_cast<unsigned long long>(row.compile[MtgsCommandTrace::TfxMisses]),
			row.compile[MtgsCommandTrace::TfxStall] / 1000000.0, row.compile[MtgsCommandTrace::PrecompileWait] / 1000000.0);
	}
}

void MTGS::InitAndReadFIFO(u8* mem, u32 qwc)
{
	if (EmuConfig.GS.HWDownloadMode == GSHardwareDownloadMode::Asynchronous && GSIsHardwareRenderer())
	{
		// Hand back the CPU-side shadow of GS memory immediately; the ordered GS-thread command
		// below schedules the real GPU download, which refreshes that shadow a few frames later.
		// The EE thread never waits on the GS thread here.
		GSReadLocalMemoryUnsync(mem, qwc, vif1.BITBLTBUF._u64, vif1.TRXPOS._u64, vif1.TRXREG._u64);
		SendSimplePacket(Command::AsyncReadFIFO, static_cast<int>(qwc), 0, 0);
		SetEvent();
		return;
	}

	// NOTE: GSHardwareDownloadMode is no longer ordered (Asynchronous is appended after
	// Disabled), so this has to enumerate the modes explicitly. NoReadbacks is deliberately
	// NOT in this set — it falls through to the synchronizing path below.
	if ((EmuConfig.GS.HWDownloadMode == GSHardwareDownloadMode::Unsynchronized ||
			EmuConfig.GS.HWDownloadMode == GSHardwareDownloadMode::Disabled) &&
		GSIsHardwareRenderer())
	{
		if (EmuConfig.GS.HWDownloadMode == GSHardwareDownloadMode::Unsynchronized)
			GSReadLocalMemoryUnsync(mem, qwc, vif1.BITBLTBUF._u64, vif1.TRXPOS._u64, vif1.TRXREG._u64);
		else
			std::memset(mem, 0, qwc * 16);

		return;
	}

	SendPointerPacket(Command::InitAndReadFIFO, qwc, mem);
	WaitGS(false, false, false);
}

union PacketTagType
{
	struct
	{
		u32 command;
		u32 data[3];
	};
	struct
	{
		u32 _command;
		u32 _data[1];
		uptr pointer;
	};
};

void MTGS::ConfigureGifRetirementWait(bool enabled)
{
	s_gif_progress_requested.store(enabled, std::memory_order_release);
}

void MTGS::MainLoop()
{
	// Threading info: run in MTGS thread
	// m_ReadPos is only update by the MTGS thread so it is safe to load it with a relaxed atomic

#ifdef RINGBUF_DEBUG_STACK
	PacketTagType prevCmd;
#endif

	std::unique_lock mtvu_lock(s_mtx_RingBufferBusy2);
#ifdef __SWITCH2__
	u32 transition_mtvu_packets = 0;
	u64 transition_mtvu_bytes = 0;
#endif
	const bool gif_progress = s_gif_progress_requested.load(std::memory_order_acquire);
	s_gif_progress_active.store(gif_progress, std::memory_order_release);

	while (true)
	{
		if (gif_progress)
			s_gif_retirement.Checkpoint();
		if (s_run_idle_flag.load(std::memory_order_acquire) && VMManager::GetState() != VMState::Running && GSHasDisplayWindow())
		{
			if (!s_sem_event.CheckForWork())
			{
				GSPresentCurrentFrame();
				GSThrottlePresentation();
			}
		}
		else
		{
			mtvu_lock.unlock();
			s_sem_event.WaitForWork();
			mtvu_lock.lock();
		}

		if (!s_open_flag.load(std::memory_order_acquire))
			break;

		// note: m_ReadPos is intentionally not volatile, because it should only
		// ever be modified by this thread.
		while (s_ReadPos.load(std::memory_order_relaxed) != s_WritePos.load(std::memory_order_acquire))
		{
			const unsigned int local_ReadPos = s_ReadPos.load(std::memory_order_relaxed);

			pxAssert(local_ReadPos < CommandRingSize);

			const PacketTagType& tag = (PacketTagType&)RingBuffer[local_ReadPos];
			const bool legacy_trace = s_GsCommandTrace.Active();
			const bool nfs_trace = GSFieldProfile::s_nfs_gs_commands;
			const bool live_trace = nfs_trace && NfsGsLive::Enabled.load(std::memory_order_relaxed);
			const u64 command_begin = (legacy_trace || nfs_trace) ? GetCPUTicks() : 0;
			MtgsCommandTrace::CompileSnapshot compile_begin;
			u64 mtvu_wait = 0;
			if (legacy_trace)
				compile_begin = MtgsCommandTrace::ReadCompileCounters();
			u32 ringposinc = 1;
			u32 profile_field = 0;

#ifdef RINGBUF_DEBUG_STACK
			// pop a ringpos off the stack.  It should match this one!

			s_lock_Stack.Lock();
			uptr stackpos = ringposStack.back();
			if (stackpos != local_ReadPos)
			{
				Console.Error("MTGS Ringbuffer Critical Failure ---> %x to %x (prevCmd: %x)\n", stackpos, local_ReadPos, prevCmd.command);
			}
			pxAssert(stackpos == local_ReadPos);
			prevCmd = tag;
			ringposStack.pop_back();
			s_lock_Stack.Release();
#endif

			if (live_trace)
			{
				NfsGsLive::TracePhases = true;
				NfsGsLive::GsCommand.Begin(tag.command, command_begin);
			}
			const ScopedGuard clear_live_command([live_trace] {
				if (live_trace)
				{
					NfsGsLive::GsCommand.End();
					NfsGsLive::TracePhases = false;
				}
			});
			switch (static_cast<Command>(tag.command))
			{
#if COPY_GS_PACKET_TO_MTGS == 1
				case Command::GIFPath1:
				{
					GSFieldProfile::Scope profile(GSFieldProfile::Phase::Gif);
					uint datapos = (local_ReadPos + 1) & CommandRingMask;
					const int qsize = tag.data[0];
					const u128* data = &RingBuffer[datapos];

					MTGS_LOG("(MTGS Packet Read) ringtype=P1, qwc=%u", qsize);

					uint endpos = datapos + qsize;
					if (endpos >= CommandRingSize)
					{
						uint firstcopylen = CommandRingSize - datapos;
						GSgifTransfer((u8*)data, firstcopylen);
						datapos = endpos & CommandRingMask;
						GSgifTransfer((u8*)RingBuffer.m_Ring, datapos);
					}
					else
					{
						GSgifTransfer((u8*)data, qsize);
					}

					ringposinc += qsize;
				}
				break;

				case Command::GIFPath2:
				{
					GSFieldProfile::Scope profile(GSFieldProfile::Phase::Gif);
					uint datapos = (local_ReadPos + 1) & CommandRingMask;
					const int qsize = tag.data[0];
					const u128* data = &RingBuffer[datapos];

					MTGS_LOG("(MTGS Packet Read) ringtype=P2, qwc=%u", qsize);

					uint endpos = datapos + qsize;
					if (endpos >= CommandRingSize)
					{
						uint firstcopylen = CommandRingSize - datapos;
						GSgifTransfer2((u32*)data, firstcopylen);
						datapos = endpos & CommandRingMask;
						GSgifTransfer2((u32*)RingBuffer.m_Ring, datapos);
					}
					else
					{
						GSgifTransfer2((u32*)data, qsize);
					}

					ringposinc += qsize;
				}
				break;

				case Command::GIFPath3:
				{
					GSFieldProfile::Scope profile(GSFieldProfile::Phase::Gif);
					uint datapos = (local_ReadPos + 1) & CommandRingMask;
					const int qsize = tag.data[0];
					const u128* data = &RingBuffer[datapos];

					MTGS_LOG("(MTGS Packet Read) ringtype=P3, qwc=%u", qsize);

					uint endpos = datapos + qsize;
					if (endpos >= CommandRingSize)
					{
						uint firstcopylen = CommandRingSize - datapos;
						GSgifTransfer3((u32*)data, firstcopylen);
						datapos = endpos & CommandRingMask;
						GSgifTransfer3((u32*)RingBuffer.m_Ring, datapos);
					}
					else
					{
						GSgifTransfer3((u32*)data, qsize);
					}

					ringposinc += qsize;
				}
				break;
#endif
				case Command::GSPacket:
				{
					GSFieldProfile::Scope profile(GSFieldProfile::Phase::Gif);
					Gif_Path& path = gifUnit.gifPath[tag.data[2]];
					u32 offset = tag.data[0];
					u32 size = tag.data[1];
					if (offset != ~0u)
						GSgifTransfer((u8*)&path.buffer[offset], size / 16);
					path.readAmount.fetch_sub(size, std::memory_order_acq_rel);
					if (gif_progress && size)
						s_gif_retirement.RetirePacket();
					break;
				}

				case Command::MTVUGSPacket:
				{
					GSFieldProfile::Scope profile(GSFieldProfile::Phase::MtvU);
					MTVU_LOG("MTGS - Waiting on semaXGkick!");
					if (!vu1Thread.semaXGkick.TryWait())
					{
						GSFieldProfile::Scope wait_profile(GSFieldProfile::Phase::MtvUWait);
						const u64 wait_begin = command_begin ? GetCPUTicks() : 0;
						mtvu_lock.unlock();
						// Wait for MTVU to complete vu1 program
						vu1Thread.semaXGkick.WaitWithSpin();
						mtvu_lock.lock();
						if (wait_begin)
							mtvu_wait = GetCPUTicks() - wait_begin;
					}
					Gif_Path& path = gifUnit.gifPath[GIF_PATH_1];
					GS_Packet gsPack = path.GetGSPacketMTVU(); // Get vu1 program's xgkick packet(s)
#ifdef __SWITCH2__
					if (nfs_trace)
					{
						++transition_mtvu_packets;
						transition_mtvu_bytes += gsPack.size;
					}
#endif
					if (gsPack.size)
						GSgifTransfer((u8*)&path.buffer[gsPack.offset], gsPack.size / 16);
					path.readAmount.fetch_sub(gsPack.size + gsPack.readAmount, std::memory_order_acq_rel);
					path.PopGSPacketMTVU(); // Should be done last, for proper Gif_MTGS_Wait()
					break;
				}

				default:
				{
					switch (static_cast<Command>(tag.command))
					{
						case Command::VSync:
						{
							GSFieldProfile::Scope profile(GSFieldProfile::Phase::Vsync);
							const int qsize = tag.data[0];
							ringposinc += qsize;

							MTGS_LOG("(MTGS Packet Read) ringtype=Vsync, field=%u, skip=%s", !!(((u32&)RingBuffer.Regs[0x1000]) & 0x2000) ? 0 : 1, tag.data[1] ? "true" : "false");

							// Mail in the important GS registers.
							// This seemingly obtuse system is needed in order to handle cases where the vsync data wraps
							// around the edge of the ringbuffer.  If not for that I'd just use a struct. >_<

							uint datapos = (local_ReadPos + 1) & CommandRingMask;
							MemCopy_WrappedSrc(RingBuffer.m_Ring, datapos, CommandRingSize, (u128*)RingBuffer.Regs, 0xf);

							u32* remainder = (u32*)&RingBuffer[datapos];
							profile_field = remainder[5];
							((u32&)RingBuffer.Regs[0x1000]) = remainder[0];
							((u32&)RingBuffer.Regs[0x1010]) = remainder[1];
							((GSRegSIGBLID&)RingBuffer.Regs[0x1080]) = (GSRegSIGBLID&)remainder[2];

							// CSR & 0x2000; is the pageflip id.
							GSvsync((((u32&)RingBuffer.Regs[0x1000]) & 0x2000) ? 0 : 1, remainder[4] != 0);

							if (s_VsyncQueue.Complete())
								s_sem_Vsync.Post();

							// Do not StateCheckInThread() here
							// Otherwise we could pause while there's still data in the queue
							// Which could make the MTVU thread wait forever for it to empty
						}
						break;

						case Command::AsyncCall:
							{
								AsyncCallType* const func = (AsyncCallType*)tag.pointer;
								(*func)();
								delete func;
							}
							break;

						case Command::Freeze:
						{
							GSFieldProfile::Scope profile(GSFieldProfile::Phase::OtherCommand);
							MTGS::FreezeData* data = (MTGS::FreezeData*)tag.pointer;
							int mode = tag.data[0];
							data->retval = GSfreeze((FreezeAction)mode, (freezeData*)data->fdata);
						}
						break;

						case Command::Reset:
							MTGS_LOG("(MTGS Packet Read) ringtype=Reset");
							GSreset(tag.data[0] != 0);
							break;

						case Command::SoftReset:
						{
							GSFieldProfile::Scope profile(GSFieldProfile::Phase::OtherCommand);
							int mask = tag.data[0];
							MTGS_LOG("(MTGS Packet Read) ringtype=SoftReset");
							GSgifSoftReset(mask);
						}
						break;

						case Command::InitAndReadFIFO:
						{
							GSFieldProfile::Scope profile(GSFieldProfile::Phase::OtherCommand);
							MTGS_LOG("(MTGS Packet Read) ringtype=Fifo2, size=%d", tag.data[0]);
							GSInitAndReadFIFO((u8*)tag.pointer, tag.data[0]);
							break;
						}

						case Command::AsyncReadFIFO:
						{
							GSFieldProfile::Scope profile(GSFieldProfile::Phase::OtherCommand);
							// The EE thread already took its answer from the shadow; this only
							// exists to issue the GPU download that refreshes it. The result goes
							// into a scratch buffer (some GS formats write one quadword past the
							// requested transfer size, hence the +1).
							MTGS_LOG("(MTGS Packet Read) ringtype=AsyncFifo, size=%d", tag.data[0]);
							static thread_local std::vector<u8> async_fifo_buffer;
							async_fifo_buffer.resize((static_cast<size_t>(tag.data[0]) + 1) * 16);
							GSInitAndReadFIFO(async_fifo_buffer.data(), tag.data[0]);
						}
						break;

#ifdef PCSX2_DEVBUILD
						default:
							Console.Error("GSThreadProc, bad packet (%x) at m_ReadPos: %x, m_WritePos: %x", tag.command, local_ReadPos, s_WritePos.load());
							pxFail("Bad packet encountered in the MTGS Ringbuffer.");
							s_ReadPos.store(s_WritePos.load(std::memory_order_acquire), std::memory_order_release);
							continue;
#else
							// Optimized performance in non-Dev builds.
							jNO_DEFAULT;
#endif
					}
				}
			}

			const bool vsync = tag.command == static_cast<u32>(Command::VSync);
			if (nfs_trace)
			{
				const u32 field = vsync ? profile_field : GSFieldProfile::s_last_field + 1;
				GSFieldProfile::s_gs_commands.Add({command_begin, GetCPUTicks(), mtvu_wait, field, tag.command});
				if (vsync)
				{
					GSFieldProfile::s_last_field = profile_field;
					GSFieldProfile::NotePipelineField(profile_field);
				}
			}
			if (vsync)
			{
				GSFieldProfile::s_capture.AtVsync(profile_field);
#ifdef __SWITCH2__
				GsBackCost::AtVsync(profile_field);
				GsPoolWait::EndField(profile_field);
				if (nfs_trace)
				{
					GsPoolWait::NoteTransition(profile_field, transition_mtvu_packets, transition_mtvu_bytes,
						reinterpret_cast<const u8*>(RingBuffer.Regs));
					transition_mtvu_packets = 0;
					transition_mtvu_bytes = 0;
				}
#endif
			}

			if (legacy_trace)
			{
				const unsigned queued = (s_WritePos.load(std::memory_order_relaxed) - local_ReadPos) & CommandRingMask;
				s_GsCommandTrace.Record({command_begin, GetCPUTicks(), tag.command, local_ReadPos, queued, mtvu_wait, {}},
					compile_begin);
			}

			uint newringpos = (s_ReadPos.load(std::memory_order_relaxed) + ringposinc) & CommandRingMask;

			if (IsDevBuild && EmuConfig.GS.SynchronousMTGS) [[unlikely]]
			{
				pxAssert(s_WritePos == newringpos);
			}

			s_ReadPos.store(newringpos, std::memory_order_release);

			if (s_SignalRingEnable.load(std::memory_order_acquire))
			{
				// The EEcore has requested a signal after some amount of processed data.
				if (s_SignalRingPosition.fetch_sub(ringposinc) <= 0)
				{
					// Make sure to post the signal after the m_ReadPos has been updated...
					s_SignalRingEnable.store(false, std::memory_order_release);
					s_sem_OnRingReset.Post();
					continue;
				}
			}
		}

		// TODO: With the new race-free WorkSema do we still need these?

		// Safety valve in case standard signals fail for some reason -- this ensures the EEcore
		// won't sleep the eternity, even if SignalRingPosition didn't reach 0 for some reason.
		// Important: Need to unlock the MTGS busy signal PRIOR, so that EEcore SetEvent() calls
		// parallel to this handler aren't accidentally blocked.
		if (s_SignalRingEnable.exchange(false))
		{
			//Console.Warning( "(MTGS Thread) Dangling RingSignal on empty buffer!  signalpos=0x%06x", m_SignalRingPosition.exchange(0) ) );
			s_SignalRingPosition.store(0, std::memory_order_release);
			s_sem_OnRingReset.Post();
		}


		//Console.Warning( "(MTGS Thread) Nothing to do!  ringpos=0x%06x", m_ReadPos );
	}

	if (gif_progress)
	{
		s_gif_progress_active.store(false, std::memory_order_release);
		s_gif_retirement.Checkpoint();
	}

	// Unblock any threads in WaitGS in case MTGS gets cancelled while still processing work
	s_ReadPos.store(s_WritePos.load(std::memory_order_acquire), std::memory_order_relaxed);
	if (s_VsyncQueue.Clear())
		s_sem_Vsync.Post();
	s_sem_event.Kill();
}

// Waits for the GS to empty out the entire ring buffer contents.
// If syncRegs, then writes pcsx2's gs regs to MTGS's internal copy
// If weakWait, then this function is allowed to exit after MTGS finished a path1 packet
// If isMTVU, then this implies this function is being called from the MTVU thread...
void MTGS::WaitGS(bool syncRegs, bool weakWait, bool isMTVU)
{
	pxAssertMsg(IsOpen(), "MTGS Warning!  WaitGS issued on a closed thread.");
	if (!IsOpen()) [[unlikely]]
		return;

	Gif_Path& path = gifUnit.gifPath[GIF_PATH_1];

	// Both m_ReadPos and m_WritePos can be relaxed as we only want to test if the queue is empty but
	// we don't want to access the content of the queue

	SetEvent();

	// Both branches block (the MTVU weak wait spins on the ring-busy mutex, the ordinary one
	// sleeps until the ring drains), so both are timed. BeginWait() returns nothing on the MTVU
	// thread, which is where the weak wait comes from.
	const u64 wait_begin = EECycleRateSampler::BeginWait();
	const auto read_amounts = [] {
		return std::array{
			THREAD_VU1 ? 0 : gifUnit.gifPath[GIF_PATH_1].readAmount.load(std::memory_order_acquire),
			gifUnit.gifPath[GIF_PATH_2].readAmount.load(std::memory_order_acquire),
			gifUnit.gifPath[GIF_PATH_3].readAmount.load(std::memory_order_acquire)};
	};
	const bool allow_retirement = weakWait && !isMTVU && !syncRegs && !COPY_GS_PACKET_TO_MTGS &&
		s_gif_progress_active.load(std::memory_order_acquire) && VMManager::Internal::IsOnCPUThread();
	const auto before = allow_retirement ? read_amounts() : std::array<int, 3>{};
	const bool wait_for_retirement = allow_retirement &&
		std::any_of(before.begin(), before.end(), [](int bytes) { return bytes > 0; });
	const auto wait_site = wait_for_retirement ? GSFieldProfile::GifRetirement :
		(weakWait ? GSFieldProfile::GifBuffer : GSFieldProfile::RingDrain);
	GSFieldProfile::BeginLiveEeWait(wait_site, wait_begin);

	if (weakWait && isMTVU)
	{
		// On weakWait we will stop waiting on the MTGS thread if the
		// MTGS thread has processed a vu1 xgkick packet, or is pending on
		// its final vu1 xgkick packet (!curP1Packs)...
		// Note: m_WritePos doesn't seem to have proper atomic write
		// code, so reading it from the MTVU thread might be dangerous;
		// hence it has been avoided...
		u32 startP1Packs = path.GetPendingGSPackets();
		if (startP1Packs)
		{
			while (true)
			{
				// m_mtx_RingBufferBusy2.Wait();
				s_mtx_RingBufferBusy2.lock();
				s_mtx_RingBufferBusy2.unlock();
				if (path.GetPendingGSPackets() != startP1Packs)
					break;
			}
		}
	}
	else if (wait_for_retirement)
	{
		// The caller rechecks its buffer boundary and will wait again if the
		// retired packet belonged to a different GIF path or freed too little.
		const auto ready = [&] {
			if (!s_gif_progress_active.load(std::memory_order_acquire) ||
				s_ReadPos.load(std::memory_order_acquire) == s_WritePos.load(std::memory_order_acquire))
				return true;
			const auto after = read_amounts();
			for (size_t i = 0; i < before.size(); ++i)
			{
				if (after[i] < before[i])
					return true;
			}
			return false;
		};
		while (!ready())
			s_gif_retirement.Wait(ready);
	}
	else
	{
		if (!s_sem_event.WaitForEmpty())
			pxFailRel("MTGS Thread Died");
	}

	GSFieldProfile::NoteEeWait(wait_site, wait_begin);
	EECycleRateSampler::EndGSWait(wait_begin);

	pxAssert(!(weakWait && syncRegs) && "No synchronization for this!");

	if (syncRegs)
	{
		// Completely synchronize GS and MTGS register states.
		memcpy(RingBuffer.Regs, PS2MEM_GS, sizeof(RingBuffer.Regs));
	}
}

// Sets the gsEvent flag and releases a timeslice.
// For use in loops that wait on the GS thread to do certain things.
void MTGS::SetEvent()
{
	s_sem_event.NotifyOfWork();
	s_CopyDataTally = 0;
}

u8* MTGS::GetDataPacketPtr()
{
	return (u8*)&RingBuffer[s_packet_writepos & CommandRingMask];
}

// Closes the data packet send command, and initiates the gs thread (if needed).
void MTGS::SendDataPacket(MtgsSubmitTrace::Sample* sample)
{
	// make sure a previous copy block has been started somewhere.
	pxAssert(s_packet_size != 0);

	uint actualSize = ((s_packet_writepos - s_packet_startpos) & CommandRingMask) - 1;
	pxAssert(actualSize <= s_packet_size);
	pxAssert(s_packet_writepos < CommandRingSize);

	PacketTagType& tag = (PacketTagType&)RingBuffer[s_packet_startpos];
	tag.data[0] = actualSize;

	s_WritePos.store(s_packet_writepos, std::memory_order_release);

	if (IsDevBuild && EmuConfig.GS.SynchronousMTGS) [[unlikely]]
	{
		WaitGS();
	}
	else
	{
		s_CopyDataTally += s_packet_size;
		if (s_CopyDataTally > 0x2000)
		{
			if (sample) sample->Mark(MtgsSubmitPhase::Publish, GetCPUTicks());
			SetEvent();
			if (sample) sample->Mark(MtgsSubmitPhase::PublishWake, GetCPUTicks());
		}
	}

	s_packet_size = 0;

	//m_PacketLocker.Release();
}

void MTGS::GenericStall(uint size)
{
	// Note on volatiles: m_WritePos is not modified by the GS thread, so there's no need
	// to use volatile reads here.  We do cache it though, since we know it never changes,
	// except for calls to RingbufferRestert() -- handled below.
	const uint writepos = s_WritePos.load(std::memory_order_relaxed);

	// Sanity checks! (within the confines of our ringbuffer please!)
	pxAssert(size < CommandRingSize);
	pxAssert(writepos < CommandRingSize);

	// generic gs wait/stall.
	// if the writepos is past the readpos then we're safe.
	// But if not then we need to make sure the readpos is outside the scope of
	// the block about to be written (writepos + size)

	uint readpos = s_ReadPos.load(std::memory_order_acquire);
	uint freeroom;

	if (writepos < readpos)
		freeroom = readpos - writepos;
	else
		freeroom = CommandRingSize - (writepos - readpos);

	if (freeroom <= size)
	{
		// Both branches below block the EE thread (one sleeps, one spins) and are timed; the fast
		// return above is not.
		const u64 wait_begin = EECycleRateSampler::BeginWait();

		GSFieldProfile::BeginLiveEeWait(GSFieldProfile::RingSpace, wait_begin);

		// writepos will overlap readpos if we commit the data, so we need to wait until
		// readpos is out past the end of the future write pos, or until it wraps around
		// (in which case writepos will be >= readpos).

		// Ideally though we want to wait longer, because if we just toss in this packet
		// the next packet will likely stall up too.  So lets set a condition for the MTGS
		// thread to wake up the EE once there's a sizable chunk of the ringbuffer emptied.

		uint somedone = (CommandRingSize - freeroom) / 4;
		if (somedone < size + 1)
			somedone = size + 1;

		// FMV Optimization: FMVs typically send *very* little data to the GS, in some cases
		// every other frame is nothing more than a page swap.  Sleeping the EEcore is a
		// waste of time, and we get better results using a spinwait.

		if (somedone > 0x80)
		{
			pxAssertMsg(s_SignalRingEnable == 0, "MTGS Thread Synchronization Error");
			s_SignalRingPosition.store(somedone, std::memory_order_release);

			//Console.WriteLn( Color_Blue, "(EEcore Sleep) PrepDataPacker \tringpos=0x%06x, writepos=0x%06x, signalpos=0x%06x", readpos, writepos, m_SignalRingPosition );

			while (true)
			{
				s_SignalRingEnable.store(true, std::memory_order_release);
				SetEvent();
				s_sem_OnRingReset.Wait();
				readpos = s_ReadPos.load(std::memory_order_acquire);
				//Console.WriteLn( Color_Blue, "(EEcore Awake) Report!\tringpos=0x%06x", readpos );

				if (writepos < readpos)
					freeroom = readpos - writepos;
				else
					freeroom = CommandRingSize - (writepos - readpos);

				if (freeroom > size)
					break;
			}

			pxAssertMsg(s_SignalRingPosition <= 0, "MTGS Thread Synchronization Error");
		}
		else
		{
			//Console.WriteLn( Color_StrongGray, "(EEcore Spin) PrepDataPacket!" );
			SetEvent();
			while (true)
			{
				Threading::SpinWait();
				readpos = s_ReadPos.load(std::memory_order_acquire);

				if (writepos < readpos)
					freeroom = readpos - writepos;
				else
					freeroom = CommandRingSize - (writepos - readpos);

				if (freeroom > size)
					break;
			}
		}

		GSFieldProfile::NoteEeWait(GSFieldProfile::RingSpace, wait_begin);
		EECycleRateSampler::EndGSWait(wait_begin);
	}
}

void MTGS::PrepDataPacket(Command cmd, u32 size)
{
	s_packet_size = size;
	++size; // takes into account our RingCommand QWC.
	GenericStall(size);

	// Command qword: Low word is the command, and the high word is the packet
	// length in SIMDs (128 bits).
	const unsigned int local_WritePos = s_WritePos.load(std::memory_order_relaxed);

	PacketTagType& tag = (PacketTagType&)RingBuffer[local_WritePos];
	tag.command = static_cast<u32>(cmd);
	tag.data[0] = s_packet_size;
	s_packet_startpos = local_WritePos;
	s_packet_writepos = (local_WritePos + 1) & CommandRingMask;
}

// Returns the amount of giftag data processed (in simd128 values).
// Return value is used by VU1's XGKICK instruction to wrap the data
// around VU memory instead of having buffer overflow...
// Parameters:
//  size - size of the packet data, in smd128's
void MTGS::PrepDataPacket(GIF_PATH pathidx, u32 size)
{
	//m_PacketLocker.Acquire();

	PrepDataPacket(static_cast<Command>(pathidx), size);
}

__fi void MTGS::_FinishSimplePacket()
{
	uint future_writepos = (s_WritePos.load(std::memory_order_relaxed) + 1) & CommandRingMask;
	pxAssert(future_writepos != s_ReadPos.load(std::memory_order_acquire));
	s_WritePos.store(future_writepos, std::memory_order_release);

	if (IsDevBuild && EmuConfig.GS.SynchronousMTGS) [[unlikely]]
		WaitGS();
	else
		++s_CopyDataTally;
}

void MTGS::SendSimplePacket(Command type, int data0, int data1, int data2)
{
	//ScopedLock locker( m_PacketLocker );

	GenericStall(1);
	PacketTagType& tag = (PacketTagType&)RingBuffer[s_WritePos.load(std::memory_order_relaxed)];

	tag.command = static_cast<u32>(type);
	tag.data[0] = data0;
	tag.data[1] = data1;
	tag.data[2] = data2;

	_FinishSimplePacket();
}

void MTGS::SendSimpleGSPacket(Command type, u32 offset, u32 size, GIF_PATH path)
{
	SendSimplePacket(type, (int)offset, (int)size, (int)path);

	if (!IsDevBuild || !EmuConfig.GS.SynchronousMTGS) [[likely]]
	{
		s_CopyDataTally += size / 16;
		if (s_CopyDataTally > 0x2000)
			SetEvent();
	}
}

void MTGS::SendPointerPacket(Command type, u32 data0, void* data1)
{
	//ScopedLock locker( m_PacketLocker );

	GenericStall(1);
	PacketTagType& tag = (PacketTagType&)RingBuffer[s_WritePos.load(std::memory_order_relaxed)];

	tag.command = static_cast<u32>(type);
	tag.data[0] = data0;
	tag.pointer = (uptr)data1;

	_FinishSimplePacket();
}

bool MTGS::WaitForOpen()
{
	if (IsOpen())
		return true;

	StartThread();

	// request open, and kick the thread.
	s_open_flag.store(true, std::memory_order_release);
	s_sem_event.NotifyOfWork();

	// wait for it to finish its stuff
	s_open_or_close_done.Wait();

	// did we succeed?
	const bool result = s_open_flag.load(std::memory_order_acquire);
	if (!result)
		Console.Error("GS failed to open.");

	return result;
}

#if defined(__SWITCH2__)
struct IzNativeApp;
extern "C" void izps2_boot_stage(IzNativeApp*, const char*, uint32_t);
#endif

void MTGS::WaitForClose()
{
	if (!IsOpen())
		return;

#if defined(__SWITCH2__)
	GSBackClose::Vulkan.store(GSBackClose::VulkanPhase::None, std::memory_order_relaxed);
	const bool back_active = GSBackClose::Active.load(std::memory_order_acquire);
	GSBackClose::Mtgs.store(GSBackClose::MtgsPhase::Requested, std::memory_order_release);
#endif
	// ask the thread to stop processing work, by clearing the open flag
	s_open_flag.store(false, std::memory_order_release);

	// and kick the thread if it's sleeping
	s_sem_event.NotifyOfWork();

#if defined(__SWITCH2__)
	// The EE owner alone writes this log. A stuck GS thread only has to
	// publish the stage before making the potentially blocking call.
	unsigned last_mtgs = ~0u;
	unsigned last_worker = ~0u;
	unsigned last_vk = ~0u;
	unsigned last_reap = ~0u;
	const auto report = [&] {
		const auto mtgs = unsigned(GSBackClose::Mtgs.load(std::memory_order_acquire));
		const auto worker = back_active ? unsigned(GSBackClose::Worker.load(std::memory_order_acquire)) : 0;
		const auto vk = unsigned(GSBackClose::Vulkan.load(std::memory_order_acquire));
		const auto reap = back_active ? unsigned(GsBackNative::Progress.load(std::memory_order_acquire)) : 0;
		if (last_mtgs == mtgs && last_worker == worker && last_vk == vk && last_reap == reap)
			return;
		last_mtgs = mtgs;
		last_worker = worker;
		last_vk = vk;
		last_reap = reap;
		const unsigned pending = (s_WritePos.load(std::memory_order_acquire) -
			s_ReadPos.load(std::memory_order_acquire)) & CommandRingMask;
		char line[128];
		const int size = std::snprintf(line, sizeof(line),
			"26 GS close mtgs=%u back_active=%u back=%u vk=%u pending_qw=%u native_reap=%u\n",
			mtgs, unsigned(back_active), worker, vk, pending, reap);
		if (size > 0 && static_cast<size_t>(size) < sizeof(line))
			izps2_boot_stage(nullptr, line, static_cast<uint32_t>(size));
	};
	while (!s_open_or_close_done.TryWait())
	{
		report();
		Threading::Sleep(20);
	}
	report();
#else
	s_open_or_close_done.Wait();
#endif
}

void MTGS::Freeze(FreezeAction mode, MTGS::FreezeData& data)
{
	pxAssertRel(IsOpen(), "GS thread is open");

	// synchronize regs before loading
	if (mode == FreezeAction::Load)
		WaitGS(true);

	SendPointerPacket(Command::Freeze, (int)mode, &data);
	WaitGS(false);
}

void MTGS::RunOnGSThread(AsyncCallType func)
{
	// The ring is single-producer: s_WritePos is owned by the CPU/EE thread, and the send path
	// below is a relaxed load / slot write / release store with no CAS. A second producer makes
	// both writers claim the same slot and both advance the position, so one packet is dropped —
	// if the loser was a data-packet header the GS thread then parses payload qwords as command
	// tags and dereferences a garbage pointer as an AsyncCallType. It also desyncs the
	// pending-packet count, which lost-wakeup-deadlocks a WaitGS'ing EE against a sleeping GS
	// thread (see the same reasoning spelled out in PINE.cpp's BuildStatsJson).
	//
	// So: marshal first. Host::RunOnGSThread() is the primitive for that — it chains through
	// Host::RunOnCPUThread(), whose queue the CPU thread drains every vsync via
	// PollInputOnCPUThread(). Dev-only because the remaining Android/iOS offenders should surface
	// as a debuggable assert during development, not an abort in a shipped build.
	//
	// The data-packet path (PrepDataPacket/SendDataPacket/SendSimpleGSPacket) is deliberately not
	// asserted: it is reached only from Gif_Unit, which is EE-thread code by construction, and it
	// is hot enough that even a dev-build check per GIF packet is not worth it.
	pxAssertMsg(VMManager::Internal::IsOnCPUThread(),
		"MTGS::RunOnGSThread() off the CPU thread — use Host::RunOnGSThread() instead");

	SendPointerPacket(Command::AsyncCall, 0, new AsyncCallType(std::move(func)));

	// wake the gs thread in case it's sleeping
	SetEvent();
}

void MTGS::GameChanged()
{
	pxAssertRel(IsOpen(), "MTGS is running");
	// Read here on the CPU thread; the GS thread must not wait on the VM info lock.
	RunOnGSThread([serial = VMManager::GetDiscSerial(), crc = VMManager::GetDiscCRC()]() { GSGameChanged(serial, crc); });
}

void MTGS::ApplySettings()
{
	pxAssertRel(IsOpen(), "MTGS is running");

	RunOnGSThread([opts = EmuConfig.GS]() {
		GSUpdateConfig(opts);
	});

	// We need to synchronize the thread when changing any settings when the download mode
	// reads local memory from the EE thread, because otherwise we might potentially read in
	// the middle of the GS renderer being reopened.
	if (IsHardwareDownloadEEThreadRead(EmuConfig.GS.HWDownloadMode))
		WaitGS(false, false, false);
}

void MTGS::ResizeDisplayWindow(u32 width, u32 height, float scale)
{
	pxAssertRel(IsOpen(), "MTGS is running");
	RunOnGSThread([width, height, scale]() {
		GSResizeDisplayWindow(width, height, scale);

		// If we're paused, re-present the current frame at the new window size.
		if (VMManager::GetState() == VMState::Paused)
			GSPresentCurrentFrame();
	});
}

void MTGS::UpdateDisplayWindow()
{
	pxAssertRel(IsOpen(), "MTGS is running");
	RunOnGSThread([]() {
		GSUpdateDisplayWindow();

		// If we're paused, re-present the current frame at the new window size.
		if (VMManager::GetState() == VMState::Paused)
		{
			// Hackity hack, on some systems, presenting a single frame isn't enough to actually get it
			// displayed. Two seems to be good enough. Maybe something to do with direct scanout.
			GSPresentCurrentFrame();
			GSPresentCurrentFrame();
		}
	});
}

void MTGS::SetVSyncMode(GSVSyncMode mode, bool allow_present_throttle)
{
	pxAssertRel(IsOpen(), "MTGS is running");

	RunOnGSThread([mode, allow_present_throttle]() { GSSetVSyncMode(mode, allow_present_throttle); });
}

void MTGS::UpdateVSyncMode()
{
	SetVSyncMode(VMManager::GetEffectiveVSyncMode(), VMManager::ShouldAllowPresentThrottle());
}

void MTGS::SetSoftwareRendering(bool software, GSInterlaceMode interlace, bool display_message /* = true */)
{
	pxAssertRel(IsOpen(), "MTGS is running");

	if (display_message)
	{
		Host::AddIconOSDMessage("SwitchRenderer", ICON_FA_WAND_MAGIC_SPARKLES, software ?
			TRANSLATE_STR("GS", "Switching to Software Renderer...") : TRANSLATE_STR("GS", "Switching to Hardware Renderer..."),
			Host::OSD_QUICK_DURATION);
	}

	RunOnGSThread([software, interlace]() {
		GSSetSoftwareRendering(software, interlace);
	});

	// See note in ApplySettings() for reasoning here.
	if (IsHardwareDownloadEEThreadRead(EmuConfig.GS.HWDownloadMode))
		WaitGS(false, false, false);
}

void MTGS::ToggleSoftwareRendering()
{
	// reading from the GS thread.. but should be okay here
	SetSoftwareRendering(GSIsHardwareRenderer(), EmuConfig.GS.InterlaceMode);
}

bool MTGS::SaveMemorySnapshot(u32 window_width, u32 window_height, bool apply_aspect, bool crop_borders,
	u32* width, u32* height, std::vector<u32>* pixels)
{
	bool result = false;
	RunOnGSThread([window_width, window_height, apply_aspect, crop_borders, width, height, pixels, &result]() {
		result = GSSaveSnapshotToMemory(window_width, window_height, apply_aspect, crop_borders, width, height, pixels);
	});
	WaitGS(false, false, false);
	return result;
}

void MTGS::PresentCurrentFrame()
{
	if (s_run_idle_flag.load(std::memory_order_relaxed))
	{
		// If we're running idle, we're going to re-present anyway.
		return;
	}

	RunOnGSThread([]() {
		GSPresentCurrentFrame();
	});
}

void MTGS::SetRunIdle(bool enabled)
{
	// NOTE: Should only be called on the GS thread.
	s_run_idle_flag.store(enabled, std::memory_order_release);
}

// Used in MTVU mode... MTVU will later complete a real packet
void Gif_AddGSPacketMTVU(GS_Packet& gsPack, GIF_PATH path)
{
	MTGS::SendSimpleGSPacket(MTGS::Command::MTVUGSPacket, 0, 0, path);
}

void Gif_AddCompletedGSPacket(GS_Packet& gsPack, GIF_PATH path)
{
	//DevCon.WriteLn("Adding Completed Gif Packet [size=%x]", gsPack.size);
	if (COPY_GS_PACKET_TO_MTGS)
	{
		MTGS::PrepDataPacket(path, gsPack.size / 16);
		MemCopy_WrappedDest((u128*)&gifUnit.gifPath[path].buffer[gsPack.offset], MTGS::RingBuffer.m_Ring,
							MTGS::s_packet_writepos, MTGS::CommandRingSize, gsPack.size / 16);
		MTGS::SendDataPacket();
	}
	else
	{
		pxAssertMsg(!gsPack.readAmount, "Gif Unit - gsPack.readAmount only valid for MTVU path 1!");
		gifUnit.gifPath[path].readAmount.fetch_add(gsPack.size);
		MTGS::SendSimpleGSPacket(MTGS::Command::GSPacket, gsPack.offset, gsPack.size, path);
	}
}

void Gif_AddBlankGSPacket(u32 size, GIF_PATH path)
{
	//DevCon.WriteLn("Adding Blank Gif Packet [size=%x]", size);
	gifUnit.gifPath[path].readAmount.fetch_add(size);
	MTGS::SendSimpleGSPacket(MTGS::Command::GSPacket, ~0u, size, path);
}

void Gif_MTGS_Wait(bool isMTVU)
{
	MTGS::WaitGS(false, true, isMTVU);
}
