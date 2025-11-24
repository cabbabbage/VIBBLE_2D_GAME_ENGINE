#include "world/screen_grid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

#include "asset/Asset.hpp"
#include "render/camera.hpp"
#include "world/chunk.hpp"
#include "world/grid.hpp"

namespace world {

void ScreenGrid::clear() {
    warped_points_.clear();
    visible_assets_.clear();
    active_chunks_.clear();
    id_to_index_.clear();
}

void ScreenGrid::rebuild(Grid& world_grid, const camera& cam, float dt_seconds) {
    clear();
    rebuild_bounds(cam);

    SDL_Rect world_rect = compute_world_rect(cam);
    constexpr int kChunkMarginPx = 0;
    world_grid.update_active_chunks(world_rect, kChunkMarginPx);
    world_grid.update_parallax(cam, dt_seconds);

    collect_candidates(world_grid);
    warp_points(world_grid, cam);
    rebuild_active_chunks(world_grid);
}

void ScreenGrid::rebuild_bounds(const camera& cam) {
    const double raw_horizon = cam.horizon_screen_y_for_scale();
    const float horizon = std::isfinite(raw_horizon) ? static_cast<float>(raw_horizon) : 0.0f;
    const float screen_w = static_cast<float>(cam.screen_width());
    const float screen_h = static_cast<float>(cam.screen_height());

    bounds_.left   = -400.0f;
    bounds_.right  = screen_w + 400.0f;
    bounds_.top    = horizon;
    bounds_.bottom = screen_h + 500.0f;
}

SDL_Rect ScreenGrid::compute_world_rect(const camera& cam) const {
    const SDL_Point screen_corners[4] = {
        SDL_Point{static_cast<int>(std::floor(bounds_.left)),  static_cast<int>(std::floor(bounds_.top))},
        SDL_Point{static_cast<int>(std::ceil(bounds_.right)),  static_cast<int>(std::floor(bounds_.top))},
        SDL_Point{static_cast<int>(std::ceil(bounds_.right)),  static_cast<int>(std::ceil(bounds_.bottom))},
        SDL_Point{static_cast<int>(std::floor(bounds_.left)),  static_cast<int>(std::ceil(bounds_.bottom))}
    };

    int min_x = std::numeric_limits<int>::max();
    int min_y = std::numeric_limits<int>::max();
    int max_x = std::numeric_limits<int>::min();
    int max_y = std::numeric_limits<int>::min();

    for (const SDL_Point& corner : screen_corners) {
        SDL_FPoint world = cam.screen_to_map(corner);
        min_x = std::min(min_x, static_cast<int>(std::floor(world.x)));
        min_y = std::min(min_y, static_cast<int>(std::floor(world.y)));
        max_x = std::max(max_x, static_cast<int>(std::ceil(world.x)));
        max_y = std::max(max_y, static_cast<int>(std::ceil(world.y)));
    }

    SDL_Rect world_rect{
        min_x,
        min_y,
        std::max(0, max_x - min_x),
        std::max(0, max_y - min_y)
    };
    return world_rect;
}

void ScreenGrid::collect_candidates(const Grid& world_grid) {
    const auto& source_points = world_grid.points();
    if (source_points.empty()) {
        return;
    }

    std::unordered_set<const Chunk*> active_lookup;
    const auto& active_chunks = world_grid.active_chunks();
    for (const Chunk* c : active_chunks) {
        if (c) active_lookup.insert(c);
    }
    const bool use_active_filter = !active_lookup.empty();

    warped_points_.reserve(source_points.size());
    for (const auto& entry : source_points) {
        const GridPoint& src = entry.second;
        if (use_active_filter) {
            if (!src.chunk || active_lookup.find(src.chunk) == active_lookup.end()) {
                continue;
            }
        }
        warped_points_.push_back(src);
        id_to_index_[src.id] = warped_points_.size() - 1;
    }
}

void ScreenGrid::warp_points(const Grid& world_grid, const camera& cam) {
    std::unordered_set<Asset*> seen_assets;
    std::unordered_set<Chunk*> seen_chunks;
    const auto& floor = cam.current_floor_depth_params();

    for (GridPoint& point : warped_points_) {
        SDL_FPoint base = cam.map_to_screen(point.world);
        const float warped_y = cam.warp_floor_screen_y(static_cast<float>(point.world.y), base.y);
        if (std::isfinite(warped_y)) {
            base.y = warped_y;
        }
        const float parallax_dx = world_grid.parallax_offset(point.world);
        base.x += parallax_dx;

        point.parallax_dx    = parallax_dx;
        point.screen         = base;
        camera::RenderEffects effects = cam.compute_render_effects(point.world, 1.0f, 1.0f);
        point.vertical_scale = effects.vertical_scale;
        point.distance_scale = effects.distance_scale;

        if (floor.enabled && std::isfinite(floor.camera_height)) {
            const float ground_depth = static_cast<float>(point.world.y - floor.base_world_y);
            point.distance_to_camera = std::hypot(ground_depth, static_cast<float>(floor.camera_height));
            point.tilt_radians       = static_cast<float>(floor.pitch_radians);
        } else {
            point.distance_to_camera = 0.0f;
            point.tilt_radians       = 0.0f;
        }

        point.on_screen = (base.x >= bounds_.left && base.x <= bounds_.right &&
                           base.y >= bounds_.top && base.y <= bounds_.bottom);

        if (!point.on_screen) {
            continue;
        }

        if (point.chunk && seen_chunks.insert(point.chunk).second) {
            active_chunks_.push_back(point.chunk);
        }

        for (Asset* occupant : point.occupants) {
            if (occupant && seen_assets.insert(occupant).second) {
                visible_assets_.push_back(occupant);
            }
        }
    }
}

void ScreenGrid::rebuild_active_chunks(const Grid& world_grid) {
    if (!active_chunks_.empty()) {
        return;
    }
    const auto& active = world_grid.active_chunks();
    for (Chunk* c : active) {
        if (c) {
            active_chunks_.push_back(c);
        }
    }
}

GridPoint* ScreenGrid::point_for_asset(const Asset* asset) {
    if (!asset) {
        return nullptr;
    }
    const GridId id = asset->grid_id();
    auto it = id_to_index_.find(id);
    if (it == id_to_index_.end()) {
        return nullptr;
    }
    if (it->second >= warped_points_.size()) {
        return nullptr;
    }
    return &warped_points_[it->second];
}

}  // namespace world
