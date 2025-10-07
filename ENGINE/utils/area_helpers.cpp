#include "area_helpers.hpp"

#include <cmath>
#include <limits>

#include "asset/asset_info.hpp"

namespace area_helpers {
namespace {
inline float effective_scale(const AssetInfo& info) {
    return (info.scale_factor > 0.0f && std::isfinite(info.scale_factor))
               ? info.scale_factor
               : 1.0f;
}

inline int scaled_dimension(int dimension, float scale) {
    if (dimension <= 0) {
        return 0;
    }
    const double value = static_cast<double>(dimension) * static_cast<double>(scale);
    const long long rounded = std::llround(value);
    if (rounded < 0) {
        return 0;
    }
    if (rounded > static_cast<long long>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(rounded);
}

inline void copy_area_metadata(const Area& source, Area& target) {
    target.set_name(source.get_name());
    target.set_type(source.get_type());
}
} // namespace

Area make_world_area(const AssetInfo& info,
                     const Area&       local_area,
                     SDL_Point         world_pos,
                     bool              flipped) {
    const auto& local_points = local_area.get_points();
    if (local_points.empty()) {
        return Area(local_area.get_name());
    }

    const float scale_factor = effective_scale(info);
    const int pivot_x = static_cast<int>(std::lround(static_cast<double>(scaled_dimension(info.original_canvas_width, scale_factor)) * 0.5));
    const int pivot_y = scaled_dimension(info.original_canvas_height, scale_factor);

    std::vector<SDL_Point> world_points;
    world_points.reserve(local_points.size());

    for (const auto& pt : local_points) {
        int local_dx = pt.x - pivot_x;
        if (flipped) {
            local_dx = -local_dx;
        }
        const int world_x = world_pos.x + local_dx;
        const int world_y = world_pos.y + (pt.y - pivot_y);
        world_points.push_back(SDL_Point{ world_x, world_y });
    }

    Area world_area(local_area.get_name(), world_points);
    copy_area_metadata(local_area, world_area);
    return world_area;
}

Area make_world_area(const AssetInfo& info,
                     const std::string& area_name,
                     SDL_Point          world_pos,
                     bool               flipped) {
    if (area_name.empty()) {
        return Area(area_name);
    }
    Area* local = const_cast<AssetInfo&>(info).find_area(area_name);
    if (!local) {
        return Area(area_name);
    }
    return make_world_area(info, *local, world_pos, flipped);
}

} // namespace area_helpers

