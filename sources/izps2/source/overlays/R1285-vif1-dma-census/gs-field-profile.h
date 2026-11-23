// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include "nfs-gs-live.h"

namespace GSFieldProfile
{
enum class Phase : unsigned
{
    OutsideCommand,
    Gif,
    MtvU,
    MtvUWait,
    Vsync,
    OtherCommand,
    TextureSource,
    TextureTarget,
    TextureInvalidate,
    TextureUpdate,
    TextureReadback,
    VulkanDraw,
    VulkanPipeline,
    VulkanSubmit,
    VulkanFenceWait,
    VulkanReadback,
    VulkanPresent,
    Count,
};

inline constexpr std::array Names{
    "outside_command", "gif", "mtvu", "mtvu_wait", "vsync", "other_command",
    "texture_source", "texture_target", "texture_invalidate", "texture_update",
    "texture_readback", "vk_draw", "vk_pipeline", "vk_submit", "vk_fence_wait",
    "vk_readback", "vk_present"
};
static_assert(Names.size() == static_cast<unsigned>(Phase::Count));

uint64_t WallTicks();
uint64_t CpuTicks();

class Capture;
inline std::atomic<bool> Sampling{false};
inline thread_local Capture* ThreadCapture = nullptr;

class Capture
{
public:
    struct Row
    {
        uint32_t field = 0;
        uint64_t begin = 0;
        uint64_t wall = 0;
        uint64_t cpu = 0;
        bool cpu_valid = false;
        std::array<uint64_t, Names.size()> ticks{};
        std::array<uint32_t, Names.size()> calls{};
    };

    void Start(bool longer_run = false)
    {
        Stop();
        m_size = 0;
        m_fields_per_group = longer_run ? 64 : 32;
        m_discontinuities = 0;
        m_running = true;
    }

    void Stop()
    {
        if (Sampling.exchange(false, std::memory_order_relaxed))
            ThreadCapture = nullptr;
        m_running = false;
    }

    void AtVsync(uint32_t field)
    {
        if (!m_running)
            return;

        if (Sampling.load(std::memory_order_relaxed))
        {
            const uint64_t now = WallTicks();
            const uint64_t cpu = CpuTicks();
            if (field == m_current.field && now >= m_start_wall)
            {
                m_current.wall = now - m_start_wall;
                m_current.cpu_valid = m_start_cpu != 0 && cpu >= m_start_cpu;
                m_current.cpu = m_current.cpu_valid ? cpu - m_start_cpu : 0;
                Advance(Phase::OutsideCommand, now);
                m_rows[m_size++] = m_current;
            }
            else
            {
                ++m_discontinuities;
            }
        }

        // Sample an adjacent pair so interlaced field parity cannot bias the profile.
        // Rotate the pair within each group using the group number.
        const uint32_t next = field + 1;
        const uint32_t pair = static_cast<uint32_t>((uint64_t((next / m_fields_per_group) * 2654435761u) * (m_fields_per_group / 2)) >> 32);
        if (m_size == m_rows.size() || (next % m_fields_per_group) / 2 != pair)
        {
            ThreadCapture = nullptr;
            Sampling.store(false, std::memory_order_relaxed);
            return;
        }

        m_current = {};
        m_current.field = next;
        m_phase = Phase::OutsideCommand;
        m_start_cpu = CpuTicks();
        m_current.begin = m_start_wall = m_last = WallTicks();
        ThreadCapture = this;
        Sampling.store(true, std::memory_order_relaxed);
    }

    size_t Size() const { return m_size; }
    unsigned Discontinuities() const { return m_discontinuities; }
    const Row& operator[](size_t index) const { return m_rows[index]; }

    Phase Enter(Phase phase)
    {
        const Phase previous = m_phase;
        Advance(phase, WallTicks());
        ++m_current.calls[static_cast<unsigned>(phase)];
        return previous;
    }

    void Leave(Phase previous) { Advance(previous, WallTicks()); }

private:
    void Advance(Phase phase, uint64_t now)
    {
        m_current.ticks[static_cast<unsigned>(m_phase)] += now - m_last;
        m_phase = phase;
        m_last = now;
    }

    std::array<Row, 256> m_rows{};
    uint32_t m_fields_per_group = 32;
    Row m_current{};
    size_t m_size = 0;
    uint64_t m_start_wall = 0;
    uint64_t m_start_cpu = 0;
    uint64_t m_last = 0;
    unsigned m_discontinuities = 0;
    Phase m_phase = Phase::OutsideCommand;
    bool m_running = false;
};

class Scope
{
public:
    explicit Scope(Phase phase)
        : m_live_phase(static_cast<unsigned>(phase)),
          m_capture(Sampling.load(std::memory_order_relaxed) ? ThreadCapture : nullptr)
    {
        if (m_capture)
            m_previous = m_capture->Enter(phase);
    }

    ~Scope()
    {
        if (m_capture)
            m_capture->Leave(m_previous);
    }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    NfsGsLive::ScopedPhase m_live_phase;
    Capture* m_capture;
    Phase m_previous = Phase::OutsideCommand;
};

// Called via MTGS on the GS thread. Report runs after WaitForClose completes.
void Start(bool nfs);
std::string FormatReport();
void StartNfsEeWaits();
void StopNfsEeWaits();
}
