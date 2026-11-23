#pragma once
#include <cstdint>

// Private EE-owner diagnostic: inclusive Off-slice time, not pure IOP JIT time.
// No live external reader; the hidden getter is called only after VM shutdown.
struct Ps2IopOffSampleTiming {
    std::uint64_t calls, call_overflows, windows, offset, selected, valid;
    std::uint64_t total_ticks, maximum_ticks, tick_hz, clock_regressions;
    std::uint64_t unqualified, frequency_mismatches, sum_overflows;
    std::uint64_t nested_calls, nested_selected_skipped, capped_calls;
    std::uint64_t first_selected_call, last_selected_call, last_end;
    std::uint64_t have_last_end, active_sample;
};
extern "C" __attribute__((visibility("hidden")))
void ps2_iop_off_sample_timing_snapshot(Ps2IopOffSampleTiming* out);
#ifdef IZPS2_SWITCH2_EE_EVENT_AUDIT
void ps2_set_ee_event_timing(bool enabled);
void ps2_ee_event_timing_snapshot(Ps2IopOffSampleTiming* out);
#endif

#ifdef PS2_IOP_OFF_TIMING_KERNEL
#define PS2_IOP_INLINE inline __attribute__((always_inline))
#define PS2_IOP_COLD __attribute__((noinline, cold))

// Local writer scheduling state, deliberately outside the published counter ABI.
struct Ps2IopOffSamplingFront {
    std::uint64_t next_rare=0;
};

template<bool Enabled, class Clock, Ps2IopOffSampleTiming& Bank, Ps2IopOffSamplingFront& Front>
struct Ps2IopOffTimingScope {
    PS2_IOP_INLINE explicit Ps2IopOffTimingScope(bool = true) {}
};

template<class Clock> struct Ps2IopOffTimingCold {
    static constexpr std::uint64_t mask = 8191;
    static constexpr std::uint64_t cap = 16384;
    static constexpr std::uint64_t limit = std::uint64_t(-1);

    static inline void Schedule(Ps2IopOffSampleTiming& d, Ps2IopOffSamplingFront& front) {
        // Nested and capped paths must account for every call exactly as before.
        if (d.active_sample || d.selected >= cap) {
            front.next_rare = 0;
            return;
        }
        // Schedule whichever comes first: this window's sample or its boundary.
        const auto next = d.calls;
        const auto slot = next & mask;
        if (!slot) {
            front.next_rare = next; // next call starts a scoring window
            return;
        }
        const auto epoch = next & ~mask;
        if (slot <= d.offset)
            front.next_rare = epoch + d.offset;
        else
            front.next_rare = epoch <= limit - (mask + 1) ? epoch + (mask + 1) : limit;
    }

    static PS2_IOP_COLD bool Start(Ps2IopOffSampleTiming& d, Ps2IopOffSamplingFront& front,
        std::uint64_t ordinal, std::uint64_t& start_tick, std::uint64_t& start_hz) {
        if (ordinal == limit) {
            ++d.call_overflows;
            return false;
        }
        d.calls = ordinal + 1;
        if(d.selected>=cap){++d.capped_calls;return false;}
        if(d.active_sample)++d.nested_calls;
        if((ordinal&mask)==0){
            if(ordinal)d.offset=(d.offset+1031)&mask;
            ++d.windows;
        }
        if((ordinal&mask)!=d.offset) {
            Schedule(d,front);
            return false;
        }
        if(d.active_sample){++d.nested_selected_skipped;return false;}
        ++d.selected;
        if(!d.first_selected_call)d.first_selected_call=ordinal+1;
        d.last_selected_call=ordinal+1;
        d.active_sample=1;
        front.next_rare=0;
        start_hz=Clock::frequency();
        start_tick=Clock::ticks();
        return true;
    }

    static PS2_IOP_COLD void Finish(Ps2IopOffSampleTiming& d, Ps2IopOffSamplingFront& front,
        std::uint64_t start_tick, std::uint64_t start_hz) {
        const std::uint64_t end_tick=Clock::ticks();
        const std::uint64_t end_hz=Clock::frequency();
        d.active_sample=0;
        Schedule(d,front);
        const bool regression=end_tick<start_tick ||
            (d.have_last_end && start_tick<d.last_end);
        d.last_end=end_tick;d.have_last_end=1;
        if(regression)++d.clock_regressions;
        const bool frequency_ok=start_hz && start_hz<1000000000ull && end_hz==start_hz;
        if(start_hz!=end_hz || (d.tick_hz && start_hz!=d.tick_hz))++d.frequency_mismatches;
        if(!frequency_ok || (d.tick_hz && start_hz!=d.tick_hz)){++d.unqualified;return;}
        if(!d.tick_hz)d.tick_hz=start_hz;
        if(regression)return;
        const std::uint64_t elapsed=end_tick-start_tick;
        ++d.valid;
        if(elapsed>d.maximum_ticks)d.maximum_ticks=elapsed;
        if(elapsed>std::uint64_t(-1)-d.total_ticks){d.total_ticks=std::uint64_t(-1);++d.sum_overflows;}
        else d.total_ticks+=elapsed;
    }

};

template<class Clock, Ps2IopOffSampleTiming& Bank, Ps2IopOffSamplingFront& Front>
struct Ps2IopOffTimingScope<true, Clock, Bank, Front> {
    std::uint64_t start_tick, start_hz;
    bool sampled = false;

    PS2_IOP_INLINE explicit Ps2IopOffTimingScope(bool enabled = true) {
        if (!enabled)
            return;
        const auto ordinal = Bank.calls;
        if (ordinal < Front.next_rare)
            Bank.calls = ordinal + 1; // strict comparison excludes saturation
        else
            sampled = Ps2IopOffTimingCold<Clock>::Start(Bank,Front,ordinal,start_tick,start_hz);
    }

    // Only a selected sample needs a live timing scope across the body.
    // Keep the ordinary event path free of sample storage and cleanup.
    template<class Body>
    static PS2_IOP_INLINE void Run(Body body) {
        const auto ordinal = Bank.calls;
        if (ordinal >= Front.next_rare) {
            using Cold = Ps2IopOffTimingCold<Clock>;
            if (ordinal == Cold::limit || Bank.selected < Cold::cap) {
                RunRare(body);
                return;
            }
            // Full sample banks cannot read a clock again. Count the call
            // and enter the same slice body as an ordinary unsampled call.
            ++Bank.capped_calls;
        }
        Bank.calls = ordinal + 1;
        body();
    }

    template<class Body>
    static PS2_IOP_COLD void RunRare(Body body) {
        const Ps2IopOffTimingScope timing;
        body();
    }

    PS2_IOP_INLINE ~Ps2IopOffTimingScope() {
        if (sampled)
            Ps2IopOffTimingCold<Clock>::Finish(Bank,Front,start_tick,start_hz);
    }
};
#undef PS2_IOP_COLD
#undef PS2_IOP_INLINE
#endif
