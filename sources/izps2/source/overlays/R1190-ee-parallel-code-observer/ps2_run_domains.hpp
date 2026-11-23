#pragma once

#include <cstdint>

struct Ps2SampleTotals
{
    std::uint64_t calls = 0;
    std::uint64_t selected = 0;
    std::uint64_t valid = 0;
    std::uint64_t cpu = 0;
    std::uint64_t wall = 0;
};

class Ps2CpuSampler
{
public:
    struct Stamp { std::uint64_t cpu = 0, wall = 0; };
    using Clock = std::uint64_t (*)();

    Stamp Begin(Clock cpu, Clock wall, unsigned stride)
    {
        // One sample in each bucket. Offset changes between buckets so
        // periodically repeated guest events do not always hit the same slot.
        const auto index = m_totals.calls++;
        const auto bucket = index / stride;
        if ((index % stride) != ((bucket ^ (bucket >> 5)) % stride))
            return {};
        ++m_totals.selected;
        return {cpu(), wall()};
    }

    void End(Stamp first, Clock cpu, Clock wall)
    {
        if (!first.wall)
            return;
        const auto end_wall = wall();
        const auto end_cpu = cpu();
        if (!first.cpu || end_cpu < first.cpu || end_wall < first.wall)
            return;
        ++m_totals.valid;
        m_totals.cpu += end_cpu - first.cpu;
        m_totals.wall += end_wall - first.wall;
    }

    Ps2SampleTotals Get() const { return m_totals; }
    void Reset() { m_totals = {}; }

private:
    Ps2SampleTotals m_totals;
};

struct Ps2WorkerCpuTicks
{
    std::uint64_t ticks = 0;
    std::uint32_t handle = 0;
    std::uint32_t result = 0;
};

struct Ps2DomainTiming
{
    Ps2SampleTotals ee_event, ee_compile;
    Ps2WorkerCpuTicks iop, vu1;
    std::uint64_t iop_lag_ns = 0;
    std::uint64_t iop_hold_ns = 0;
    std::uint64_t iop_sif_calls = 0;
    std::uint64_t sd_calls = 0, sd_ticks = 0, sd_bytes = 0;
};

void iz_ps2_set_ee_domain_timing(bool enabled);
void iz_ps2_get_ee_domain_timing(Ps2SampleTotals& event, Ps2SampleTotals& compile);
Ps2WorkerCpuTicks iz_ps2_iop_cpu_ticks();
Ps2WorkerCpuTicks iz_ps2_vu1_cpu_ticks();

extern "C" void iz_ps2_set_sd_read_timing(bool enabled);
extern "C" void iz_ps2_get_sd_read_timing(std::uint64_t* calls, std::uint64_t* ticks, std::uint64_t* bytes);
