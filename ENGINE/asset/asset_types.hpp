#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace asset_types {

inline constexpr std::string_view player   = "player";
inline constexpr std::string_view boundary = "boundary";
inline constexpr std::string_view enemy    = "enemy";
inline constexpr std::string_view texture  = "texture";
inline constexpr std::string_view npc      = "npc";
inline constexpr std::string_view object   = "object";

inline constexpr std::array<std::string_view, 6> all{ player, boundary, enemy, texture, npc, object };

inline bool is_valid(std::string_view value) {
    return std::find(all.begin(), all.end(), value) != all.end();
}

inline std::string canonicalize(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (is_valid(value)) {
        return value;
    }
    return std::string{object};
}

inline std::vector<std::string> all_as_strings() {
    std::vector<std::string> result;
    result.reserve(all.size());
    for (std::string_view v : all) {
        result.emplace_back(v);
    }
    return result;
}

}

