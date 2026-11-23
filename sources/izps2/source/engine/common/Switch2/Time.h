// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include <cerrno>
#include <chrono>
#include <ctime>

// UTC conversion for newlib; does not mutate global TZ or use local time.
inline time_t iz_timegm(std::tm* value)
{
    if (!value) { errno = EINVAL; return static_cast<time_t>(-1); }
    using namespace std::chrono;
    const long long months_since_year_zero =
        (static_cast<long long>(value->tm_year) + 1900) * 12 + value->tm_mon;
    const long long y = (months_since_year_zero >= 0 ? months_since_year_zero : months_since_year_zero - 11) / 12;
    if (y < -32767 || y > 32767) { errno = EOVERFLOW; return static_cast<time_t>(-1); }
    const unsigned mon = static_cast<unsigned>(months_since_year_zero - y * 12 + 1);
    const auto stamp = sys_days{year{static_cast<int>(y)}/month{mon}/1} +
        days{static_cast<long long>(value->tm_mday) - 1} + hours{value->tm_hour} +
        minutes{value->tm_min} + seconds{value->tm_sec};
    const auto result = static_cast<time_t>(duration_cast<seconds>(stamp.time_since_epoch()).count());
    std::tm normalized{};
    if (!gmtime_r(&result, &normalized)) { errno = EOVERFLOW; return static_cast<time_t>(-1); }
    *value = normalized;
    return result;
}
