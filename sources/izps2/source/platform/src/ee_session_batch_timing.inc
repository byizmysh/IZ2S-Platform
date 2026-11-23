// Included by the EE session owner. All values are written on that thread and
// read by the launcher only after the worker has joined.
struct Ps2EeSessionTiming {
    uint64_t valid_pairs, maximum_ticks, slow_pairs, begin_tick;
    uint64_t begin_pc, end_pc, gs_wait_ticks, vu_wait_ticks, waits_qualified;
};
static_assert(sizeof(Ps2EeSessionTiming) == 72);
extern "C" {
[[gnu::used, gnu::visibility("hidden")]] Ps2EeSessionTiming ps2_ee_session_timing{};
}

struct Ps2EeSessionStart {
    uint64_t counter_epoch, gs_wait_ticks, vu_wait_ticks;
    uint32_t pc;
    bool waits_valid;
};
static Ps2EeSessionStart ps2_ee_session_start{};

static void ps2_ee_session_begin(uint64_t id) {
    const bool valid = ps2_ee_wait_previous_valid &&
        __atomic_load_n(&ps2_hbxl_ee_wait_tally.word[10], __ATOMIC_RELAXED) == id &&
        __atomic_load_n(&ps2_hbxl_ee_wait_tally.word[11], __ATOMIC_RELAXED) == 0;
    ps2_ee_session_start = {ps2_ee_wait_counter_epoch, ps2_ee_wait_previous.gs_wait_ticks,
        ps2_ee_wait_previous.vu1_wait_ticks, cpuRegs.pc, valid};
}

static void ps2_ee_session_complete(uint64_t id, uint64_t begin_tick, uint64_t elapsed, uint64_t hz) {
    auto& result = ps2_ee_session_timing;
    ++result.valid_pairs;
    if (elapsed >= (hz + 24) / 25)
        ++result.slow_pairs;
    if (elapsed <= result.maximum_ticks)
        return;

    const auto& start = ps2_ee_session_start;
    const auto& end = ps2_ee_wait_previous;
    const bool valid = start.waits_valid && ps2_ee_wait_previous_valid &&
        __atomic_load_n(&ps2_hbxl_ee_wait_tally.word[10], __ATOMIC_RELAXED) == id &&
        __atomic_load_n(&ps2_hbxl_ee_wait_tally.word[11], __ATOMIC_RELAXED) == 1 &&
        ps2_ee_wait_counter_epoch == start.counter_epoch && end.gs_wait_ticks >= start.gs_wait_ticks &&
        end.vu1_wait_ticks >= start.vu_wait_ticks;
    result.maximum_ticks = elapsed;
    result.begin_tick = begin_tick;
    result.begin_pc = start.pc;
    result.end_pc = cpuRegs.pc;
    result.gs_wait_ticks = valid ? end.gs_wait_ticks - start.gs_wait_ticks : 0;
    result.vu_wait_ticks = valid ? end.vu1_wait_ticks - start.vu_wait_ticks : 0;
    result.waits_qualified = valid;
}
