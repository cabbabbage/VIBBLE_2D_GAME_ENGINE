#pragma once

#include <cctype>
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

}

