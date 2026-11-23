#pragma once

#include <cstdint>

extern "C" std::uint32_t iz_ps2_warm_fastload_request();
extern "C" std::uint32_t iz_ps2_warm_fastload_cancel();
extern "C" std::uint32_t iz_ps2_warm_fastload_normal();

// This scope owns only the request it successfully made. Its caller stops
// startup if neither public method can restore the ordinary applet profile.
class Ps2EeWarmFastLoad
{
public:
    explicit Ps2EeWarmFastLoad(bool requested)
    {
        if (requested)
        {
            request_result = iz_ps2_warm_fastload_request();
            m_active = request_result == 0;
        }
    }

    Ps2EeWarmFastLoad(const Ps2EeWarmFastLoad&) = delete;
    Ps2EeWarmFastLoad& operator=(const Ps2EeWarmFastLoad&) = delete;

    ~Ps2EeWarmFastLoad() { Restore(); }

    bool Restore()
    {
        if (!m_active)
            return true;
        cancel_result = iz_ps2_warm_fastload_cancel();
        if (cancel_result == 0)
            m_active = false;
        else
        {
            fallback_used = true;
            normal_result = iz_ps2_warm_fastload_normal();
            m_active = normal_result != 0;
        }
        return !m_active;
    }

    std::uint32_t request_result = 0;
    std::uint32_t cancel_result = 0;
    std::uint32_t normal_result = 0;
    bool fallback_used = false;

private:
    bool m_active = false;
};
