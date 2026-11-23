// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include "gs-field-profile.h"

namespace GsBackCost
{
inline thread_local bool RecordingBackRecord = false;

enum class Detail
{
    DrawConfiguration,
    VulkanState,
};

class DetailScope
{
public:
    explicit DetailScope(Detail detail)
        : m_capture(RecordingBackRecord ? GSFieldProfile::ThreadCapture : nullptr)
    {
        // These slots measure MTGS-only waits in the front capture. The
        // back capture can use them for detail while retaining the phase ABI.
        if (m_capture)
            m_previous = m_capture->Enter(detail == Detail::DrawConfiguration ?
                GSFieldProfile::Phase::MtvUWait : GSFieldProfile::Phase::Vsync);
    }

    ~DetailScope()
    {
        if (m_capture)
            m_capture->Leave(m_previous);
    }

    DetailScope(const DetailScope&) = delete;
    DetailScope& operator=(const DetailScope&) = delete;

private:
    GSFieldProfile::Capture* m_capture;
    GSFieldProfile::Phase m_previous{};
};
}
