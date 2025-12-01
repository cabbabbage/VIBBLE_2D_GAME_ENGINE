#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace animation_editor::strings {

inline bool is_space(unsigned char ch) {
    return std::isspace(ch) != 0;
}

inline std::string trim_copy(std::string_view value) {
    std::size_t start = 0;
    std::size_t end   = value.size();

    while (start < end && is_space(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    while (end > start && is_space(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return std::string(value.substr(start, end - start));
}

inline std::string to_lower_copy(std::string_view value) {
    std::string result = std::string(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c){ return std::tolower(c); });
    return result;
}

inline bool has_numeric_stem(const std::filesystem::path& path) {
    std::string stem = path.stem().string();
    if (stem.empty()) return false;
    return std::all_of(stem.begin(), stem.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; });
}

inline bool is_reserved_animation_name(std::string_view value) {
    std::string lowered = to_lower_copy(value);
    return lowered == "kill" || lowered == "lock" || lowered == "reverse";
}

}
