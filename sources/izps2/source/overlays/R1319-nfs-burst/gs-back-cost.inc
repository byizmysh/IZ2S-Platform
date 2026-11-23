// SPDX-License-Identifier: GPL-3.0+
#include "gs-back-cost.h"
#include "gs-back-detail.h"
#include "gs-field-profile.h"
#include <fmt/format.h>

namespace GsBackCost
{
namespace
{
using namespace GSFieldProfile;
Capture s_capture;
std::atomic<bool> s_active{false};
bool s_enabled = false; // MTGS only
GsBackNative::CpuSample s_cpu_start;
struct CpuRow { uint64_t ticks = 0; bool valid = false; };
std::array<CpuRow, 256> s_cpu;

// The already-linked GS scopes reference this exact TLS and sampling flag.
// This saves their MTGS values while the independent back capture advances.
struct PreserveFront
{
    const bool sampling = Sampling.load(std::memory_order_relaxed);
    Capture* const capture = ThreadCapture;
    ~PreserveFront()
    {
        ThreadCapture = capture;
        Sampling.store(sampling, std::memory_order_relaxed);
    }
};

void Start(bool enabled)
{
    s_active.store(false, std::memory_order_relaxed);
    s_enabled = enabled;
    s_cpu_start = {};
    s_cpu = {};
    const PreserveFront preserve;
    if (enabled)
        s_capture.Start(true);
    else
        s_capture.Stop();
}

class Record
{
public:
    explicit Record(GSBackQueue::RecordType type)
    {
        if (!s_active.load(std::memory_order_acquire))
            return;
        m_capture = ThreadCapture;
        ThreadCapture = &s_capture;
        RecordingBackRecord = true;
        using GSBackQueue::RecordType;
        // Reuse the existing phase ABI. Report remaps these outer phases
        // to the packet classes that the back worker actually executes.
        const auto phase = type == RecordType::Draw ? Phase::OtherCommand :
            type == RecordType::Transfer ? Phase::Gif : Phase::MtvU;
        m_previous = s_capture.Enter(phase);
        m_enabled = true;
    }
    ~Record()
    {
        if (m_enabled)
        {
            s_capture.Leave(m_previous);
            RecordingBackRecord = false;
            ThreadCapture = m_capture;
        }
    }
    Record(const Record&) = delete;
    Record& operator=(const Record&) = delete;
private:
    Capture* m_capture = nullptr;
    bool m_enabled = false;
    Phase m_previous = Phase::OutsideCommand;
};

std::string Report()
{
    if (!s_enabled)
        return {};
    constexpr std::array names{
        "outside_record", "transfer", "other_record", "draw_configuration", "vk_state", "draw_setup_rest",
        "texture_source", "texture_target", "texture_invalidate", "texture_update",
        "texture_readback", "vk_draw", "vk_pipeline", "vk_submit", "vk_fence_wait",
        "vk_readback", "vk_present"};
    static_assert(names.size() == GSFieldProfile::Names.size());
    std::string report = fmt::format("IZPS2_GS_BACK_PROFILE samples={} wall_hz={} cpu_hz={} discontinuities={}\n",
        s_capture.Size(), GetTickFrequency(), Threading::GetThreadTicksPerSecond(), s_capture.Discontinuities());
    for (size_t i = 0; i < s_capture.Size(); ++i)
    {
        const auto& row = s_capture[i];
        report += fmt::format("IZPS2_GS_BACK_FIELD field={} begin_tick={} wall={} cpu={} cpu_valid={}",
            row.field, row.begin, row.wall, s_cpu[i].ticks, unsigned(s_cpu[i].valid));
        for (size_t phase = 0; phase < names.size(); ++phase)
            report += fmt::format(" {}={}/{}", names[phase], row.ticks[phase], row.calls[phase]);
        report += '\n';
    }
    return report;
}
}

void AtVsync(uint32_t field)
{
    if (!s_enabled || !field)
        return;
    // The front consumed the VSync's ordered drain. No other thread can add
    // back records until this callback returns to the producer loop.
    const PreserveFront preserve;
    const bool was_active = s_active.load(std::memory_order_relaxed);
    Sampling.store(was_active, std::memory_order_relaxed);
    ThreadCapture = was_active ? &s_capture : nullptr;
    const size_t old_size = s_capture.Size();
    s_capture.AtVsync(field);
    const bool next_active = Sampling.load(std::memory_order_relaxed);
    if (was_active || next_active)
    {
        const auto now_cpu = GsBackNative::SampleCpu();
        if (s_capture.Size() != old_size && s_cpu_start.valid && now_cpu.valid &&
            s_cpu_start.generation == now_cpu.generation && now_cpu.ticks >= s_cpu_start.ticks)
            s_cpu[old_size] = {now_cpu.ticks - s_cpu_start.ticks, true};
        s_cpu_start = next_active ? now_cpu : GsBackNative::CpuSample{};
    }
    s_active.store(next_active, std::memory_order_release);
}
}
