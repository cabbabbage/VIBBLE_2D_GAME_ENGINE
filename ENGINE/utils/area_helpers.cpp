#include "area_helpers.hpp"

#include <algorithm>
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

inline int unscale_dimension(int dimension, float scale) {
    if (!(scale > 0.0f) || !std::isfinite(scale)) {
        return dimension;
    }
    if (dimension <= 0) {
        return 0;
    }
    const double value = static_cast<double>(dimension) / static_cast<double>(scale);
    const long long rounded = std::llround(value);
    if (rounded < 0) {
        return 0;
    }
    if (rounded > static_cast<long long>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(rounded);
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
    target.set_resolution(source.resolution());
}

inline const AssetInfo::NamedArea::RenderFrame*
find_render_frame(const AssetInfo& info, const Area& local_area) {
    for (const auto& named : info.areas) {
        if (!named.area) {
            continue;
        }
        if (named.area->get_name() == local_area.get_name() && named.render_frame &&
            named.render_frame->is_valid()) {
            return &(*named.render_frame);
        }
    }
    return nullptr;
}
}

Area make_world_area(const AssetInfo& info,
                     const Area&       local_area,
                     SDL_Point         world_pos,
                     bool              flipped) {
    const auto& local_points = local_area.get_points();
    if (local_points.empty()) {
        return Area(local_area.get_name(), local_area.resolution());
    }

    const float scale_factor = effective_scale(info);

    const AssetInfo::NamedArea::RenderFrame* frame = find_render_frame(info, local_area);

    int base_width = info.original_canvas_width;
    int base_height = info.original_canvas_height;
    double pivot_ratio_x = 0.5;
    double pivot_ratio_y = 1.0;

    if (frame) {
        if (base_width <= 0) {
            base_width = unscale_dimension(frame->width, frame->pixel_scale);
        }
        if (base_height <= 0) {
            base_height = unscale_dimension(frame->height, frame->pixel_scale);
        }
        if (frame->width > 0) {
            pivot_ratio_x = static_cast<double>(frame->pivot_x) / static_cast<double>(std::max(frame->width, 1));
        }
        if (frame->height > 0) {
            pivot_ratio_y = static_cast<double>(frame->pivot_y) / static_cast<double>(std::max(frame->height, 1));
        }
    }

    int scaled_width = scaled_dimension(base_width, scale_factor);
    int scaled_height = scaled_dimension(base_height, scale_factor);

    if (frame) {
        if (scaled_width <= 0) {
            scaled_width = frame->width;
        }
        if (scaled_height <= 0) {
            scaled_height = frame->height;
        }
    }

    const int pivot_x = (scaled_width > 0) ? static_cast<int>(std::llround(pivot_ratio_x * scaled_width)) : 0;
    const int pivot_y = (scaled_height > 0) ? static_cast<int>(std::llround(pivot_ratio_y * scaled_height)) : 0;

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

    Area world_area(local_area.get_name(), world_points, local_area.resolution());
    copy_area_metadata(local_area, world_area);
    return world_area;
}

Area make_world_area(const AssetInfo& info,
                     const std::string& area_name,
                     SDL_Point          world_pos,
                     bool               flipped) {
    if (area_name.empty()) {
        return Area(area_name, 0);
    }
    Area* local = const_cast<AssetInfo&>(info).find_area(area_name);
    if (!local) {
        return Area(area_name, 0);
    }
    return make_world_area(info, *local, world_pos, flipped);
}

}

