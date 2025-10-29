#pragma once

#include <string_view>
#include <array>

namespace DMIcons {
struct LineSegment {
    float x0;
    float y0;
    float x1;
    float y1;
};

inline constexpr std::array<LineSegment, 4> CollapsibleHandleSegments() noexcept {
    return {
        LineSegment{-0.5f, 0.0f, 0.5f, 0.0f},
        LineSegment{0.0f, -0.5f, 0.0f, 0.5f},
        LineSegment{-0.35f, -0.35f, 0.35f, 0.35f},
        LineSegment{-0.35f, 0.35f, 0.35f, -0.35f},
};
}

inline constexpr std::string_view CollapseExpanded() noexcept { return u8"\u25B2"; }
inline constexpr std::string_view CollapseCollapsed() noexcept { return u8"\u25BC"; }
inline constexpr std::string_view Close() noexcept { return "X"; }
inline constexpr std::string_view Info() noexcept { return u8"\u24D8"; }
}

