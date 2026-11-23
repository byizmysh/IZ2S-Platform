#pragma once

#include "ps2_timing_windows.hpp"
#include "ps2_vu1_cpu_timing.hpp"
#include "ps2_publication_timing.hpp"
#include "ps2_run_domains.hpp"

#ifdef IZPS2_SWITCH2_VK_STREAM_PRESSURE
#include "ps2_vk_stream_pressure.hpp"
#endif

struct Ps2FrameWindowCounters
{
#ifdef IZPS2_SWITCH2_SESSION_TIMING
    struct Timing
    {
        uint64_t wall_ticks;
        uint64_t ee_cpu_ticks;
        uint64_t ee_blocks;
        uint64_t ee_compile_ticks;
        uint64_t jit_calls;
        uint64_t jit_bytes;
        uint64_t jit_ticks;
        Ps2Vu1Timing vu1;
        Ps2Vu1WallTiming vu1_wall;
        Ps2PublicationTiming publication;
#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
        uint64_t poll_skips;
        uint64_t poll_guest_cycles;
#endif
#ifdef IZPS2_SWITCH2_QUEUE_WAIT_AUDIT
        uint64_t gs_wait_ticks;
        uint64_t vu1_wait_ticks;
#endif
#ifdef IZPS2_SWITCH2_LIMITER_AUDIT
        uint64_t limiter_ticks;
        uint64_t limiter_calls;
        float target_speed;
        bool host_vsync_pacing;
#endif
#ifdef IZPS2_SWITCH2_IOP_SESSION_TIMING
        uint64_t ee_compile_publication_ticks;
        uint64_t iop_calls;
        uint64_t iop_selected;
        uint64_t iop_valid;
        uint64_t iop_ticks;
        uint64_t iop_tick_hz;
#endif
#ifdef IZPS2_SWITCH2_EE_EVENT_AUDIT
        uint64_t ee_event_calls;
        uint64_t ee_event_valid;
        uint64_t ee_event_ticks;
        uint64_t ee_event_tick_hz;
#endif
        Ps2DomainTiming domains;
    };
#endif

    struct Counts
    {
        uint64_t presents;
        uint64_t register_updates;
        uint64_t blits;
        float nominal_hz;
#ifdef IZPS2_SWITCH2_GTA_FRAME_WITNESS
        Ps2GtaFrameWitness gta_witness;
#endif
#ifdef IZPS2_SWITCH2_VK_STREAM_PRESSURE
        Ps2VkStreamPressure::Totals vk_streams;
#endif
#ifdef IZPS2_SWITCH2_SESSION_TIMING
        Timing timing;
#endif
    };

};

class Ps2FrameWindows : public Ps2TimingWindows<Ps2FrameWindowCounters::Counts>
{
public:
    using Counts = Ps2FrameWindowCounters::Counts;
#ifdef IZPS2_SWITCH2_SESSION_TIMING
    using Timing = Ps2FrameWindowCounters::Timing;
#endif
};
