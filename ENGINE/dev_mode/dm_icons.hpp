#pragma once

#include <string_view>

namespace DMIcons {
inline constexpr std::string_view CollapseExpanded() noexcept { return u8"\u25B2"; }
inline constexpr std::string_view CollapseCollapsed() noexcept { return u8"\u25BC"; }
inline constexpr std::string_view Close() noexcept { return "X"; }
}

