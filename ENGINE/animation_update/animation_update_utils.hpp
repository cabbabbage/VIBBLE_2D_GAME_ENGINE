#pragma once

#include <algorithm>
#include <cmath>

#include <SDL.h>

#include "asset/Asset.hpp"
#include "utils/area.hpp"

namespace animation_update::detail {

inline constexpr const char kDefaultAnimation[] = "default";
inline constexpr int        kOverlapDistanceSq  = 40 * 40;

inline int distance_sq(SDL_Point a, SDL_Point b) {
    const int dx = a.x - b.x;
    const int dy = a.y - b.y;
    return dx * dx + dy * dy;
}

inline bool segment_hits_area(SDL_Point from, SDL_Point to, const Area& area) {
    const int steps = std::max(std::abs(to.x - from.x), std::abs(to.y - from.y));
    if (steps == 0) {
        return area.contains_point(from);
    }

    const double step_x = (to.x - from.x) / static_cast<double>(steps);
    const double step_y = (to.y - from.y) / static_cast<double>(steps);

    for (int i = 0; i <= steps; ++i) {
        SDL_Point sample{ static_cast<int>(std::round(from.x + step_x * i)),
                          static_cast<int>(std::round(from.y + step_y * i)) };
        if (area.contains_point(sample)) {
            return true;
        }
    }
    return false;
}

inline SDL_Point bottom_middle_for(const Asset& asset, SDL_Point pos) {
    Area area = asset.get_area("collision_area");
    const auto& pts = area.get_points();
    if (pts.empty()) {
        return pos;
    }

    SDL_Point bottom = pts.front();
    for (const SDL_Point& pt : pts) {
        if (pt.y > bottom.y) {
            bottom = pt;
        }
    }

    const int offset_x = bottom.x - asset.pos.x;
    const int offset_y = bottom.y - asset.pos.y;
    return SDL_Point{ pos.x + offset_x, pos.y + offset_y };
}

}  // namespace animation_update::detail

