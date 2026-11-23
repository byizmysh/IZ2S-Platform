#pragma once
#include <string>
#include <string_view>
#include <vector>
namespace Switch2Paths {
inline size_t RootLength(std::string_view path) {
    if (path.starts_with("sdmc:/")) return 6;
    if (path.starts_with("romfs:/")) return 7;
    return 0;
}
inline std::string Canonicalize(std::string_view path) {
    const size_t root = RootLength(path);
    if (!root) return std::string(path);
    std::vector<std::string_view> parts;
    for (size_t start = root; start < path.size();) {
        size_t end = path.find('/', start);
        if (end == std::string_view::npos) end = path.size();
        const auto part = path.substr(start, end - start);
        if (part == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!part.empty() && part != ".") parts.push_back(part);
        start = end + 1;
    }
    std::string result(path.substr(0, root));
    for (const auto& part : parts) {
        if (result.back() != '/') result.push_back('/');
        result.append(part);
    }
    return result;
}
}
