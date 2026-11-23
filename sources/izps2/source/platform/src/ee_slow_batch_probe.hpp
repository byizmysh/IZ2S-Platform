#ifndef PS2_EE_SLOW_BATCH_PROBE_HPP
#define PS2_EE_SLOW_BATCH_PROBE_HPP
#include <cstdint>

// CPU-owner-only diagnostic state. The caller supplies the ticks it already
// captured; this probe never reads a clock, invokes SDK, waits, or controls VM.
namespace Ps2SlowBatch {
struct Sample {
    std::uint64_t id{}, tick{}, hz{}, cycles{}, frames{};
    std::uint64_t publishCalls{}, publishTicks{}, mapCalls{}, mapTicks{};
    std::uint64_t unmapCalls{}, unmapTicks{}, bytes{}, epoch{};
    std::uint32_t pc{};
    bool countersStable{}, clockQualified{};
};
struct Record {
    Sample begin{}, end{};
    std::uint64_t ticks{};
    bool counterDeltasQualified{};
};
struct State {
    Sample pending{};
    Record maximum{};
    std::uint64_t starts{}, completed{}, slow{}, mismatch{}, clockRejected{}, regressions{};
    std::uint32_t printed{};
    bool active{};
};
static inline void begin(State& s,const Sample& a) {
    ++s.starts;
    if(s.active)++s.mismatch;
    s.pending=a;s.active=true;
}
static inline bool end(State& s,const Sample& b,Record& out) {
    if(!s.active||s.pending.id!=b.id){++s.mismatch;s.active=false;return false;}
    s.active=false;const Sample a=s.pending;
    if(!a.clockQualified||!b.clockQualified||!a.hz||a.hz!=b.hz){++s.clockRejected;return false;}
    if(b.tick<a.tick){++s.regressions;return false;}
    ++s.completed;out={a,b,b.tick-a.tick,false};
    out.counterDeltasQualified=a.countersStable&&b.countersStable&&b.epoch>=a.epoch&&
        b.publishCalls>=a.publishCalls&&b.publishTicks>=a.publishTicks&&
        b.mapCalls>=a.mapCalls&&b.mapTicks>=a.mapTicks&&b.unmapCalls>=a.unmapCalls&&
        b.unmapTicks>=a.unmapTicks&&b.bytes>=a.bytes;
    if(out.ticks>s.maximum.ticks)s.maximum=out;
    // >=40 ms, rounded upwards so very low model frequencies remain valid.
    const std::uint64_t threshold=a.hz/25+(a.hz%25!=0);
    if(out.ticks<threshold)return false;
    ++s.slow;
    if(s.printed>=32)return false;
    ++s.printed;return true;
}
}
#endif
