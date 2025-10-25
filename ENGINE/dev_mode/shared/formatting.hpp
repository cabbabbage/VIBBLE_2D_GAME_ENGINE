#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <string_view>
#include <system_error>

namespace dev_mode {

constexpr std::size_t kSliderFormatBufferSize = 32;

inline std::string_view FormatSliderValue(double value, int precision, std::array<char, kSliderFormatBufferSize>& buffer) {
    precision = std::max(0, precision);

    if (!std::isfinite(value)) {
        return std::string_view{};
    }

    auto* begin = buffer.data();
    auto* end = buffer.data() + buffer.size();
    auto result = std::to_chars(begin, end, value, std::chars_format::fixed, precision);
    if (result.ec != std::errc{}) {
        return std::string_view{};
    }
    return std::string_view(begin, static_cast<std::size_t>(result.ptr - begin));
}

inline std::string_view FormatSliderValue(float value, int precision, std::array<char, kSliderFormatBufferSize>& buffer) {
    return FormatSliderValue(static_cast<double>(value), precision, buffer);
}

}

