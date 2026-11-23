#pragma once

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace GameScenes {

inline std::string Directory(std::string_view serial, uint32_t crc) {
    std::string directory = "scenes/";
    if (serial.empty()) serial = "ELF";
    for (char c : serial) {
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        const bool allowed = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
        directory += allowed ? c : '_';
    }
    char suffix[10];
    std::snprintf(suffix, sizeof(suffix), "-%08X", static_cast<unsigned>(crc));
    return directory + suffix;
}

inline std::string Filename(uint32_t id) {
    char name[sizeof("scene-4294967295.p2s")];
    std::snprintf(name, sizeof(name), "scene-%08u.p2s", static_cast<unsigned>(id));
    return name;
}

inline uint32_t ParseFilename(std::string_view name) {
    if (!name.starts_with("scene-") || !name.ends_with(".p2s")) return 0;
    const auto digits = name.substr(6, name.size() - 10);
    if (digits.size() < 8 || digits.size() > 10) return 0;
    if (digits.size() > 8 && digits.front() == '0') return 0;
    uint32_t id = 0;
    const auto end = digits.data() + digits.size();
    const auto result = std::from_chars(digits.data(), end, id);
    return result.ec == std::errc{} && result.ptr == end ? id : 0;
}

} // namespace GameScenes
