#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace tag_utils {

inline std::string normalize(std::string_view input) {
    auto begin = std::find_if_not(input.begin(), input.end(), [](char ch) {
        return std::isspace(static_cast<unsigned char>(ch)) != 0;
    });
    auto end = std::find_if_not(input.rbegin(), input.rend(), [](char ch) {
        return std::isspace(static_cast<unsigned char>(ch)) != 0;
    }).base();
    if (begin >= end) {
        return std::string{};
    }
    std::string out(begin, end);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

}

