#include "grid_camera.hpp"

#include "camera_grid.hpp"
#include "asset/Asset.hpp"
#include "world/grid.hpp"

#include <algorithm>
#include <limits>
#include <cmath>

grid_camera::grid_camera(int screen_width, int screen_height)
    : screen_width_(screen_width)
    , screen_height_(screen_height)
{
}

void grid_camera::set_screen_dimensions(int width, int height) {
    screen_width_ = width;
    screen_height_ = height;
}

void grid_camera::rebuild_grid(camera_grid& camera, world::Grid& world_grid, float dt_seconds) {
    (void)dt_seconds; // Not directly used, but may be used for future grid animations

    clear_grid_state();

    std::vector<Asset*> assets = world_grid.all_assets();
    warped_points_.reserve(assets.size());
    visible_assets_.reserve(assets.size());
    visible_points_.reserve(assets.size());

    const float inv_scale   = 1.0f / std::max(0.000001f, camera.get_scale());
    const float screen_w    = static_cast<float>(screen_width_);
    const float screen_h    = static_cast<float>(screen_height_);
    const float horizon_y   = static_cast<float>(camera.horizon_screen_y_for_scale());
    const auto& settings    = camera.realism_settings();

    const float margin_px   = std::max(0.0f, settings.extra_cull_margin);
    const float bottom_pad  = std::max(settings.grid_depth_offset_px, margin_px);
    const float cull_top    = std::max(0.0f, horizon_y - margin_px);
    const SDL_FRect cull_rect{
        -margin_px,
        cull_top,
        screen_w + margin_px * 2.0f,
        (screen_h + bottom_pad) - cull_top
    };
    const float min_visible_px =
        screen_h * std::clamp(settings.min_visible_screen_ratio, 0.0f, 0.5f);

    auto rects_intersect = [](const SDL_FRect& a, const SDL_FRect& b) -> bool {
        const float ax1 = a.x + a.w;
        const float ay1 = a.y + a.h;
        const float bx1 = b.x + b.w;
        const float by1 = b.y + b.h;
        return !(ax1 < b.x || bx1 < a.x || ay1 < b.y || by1 < a.y);
    };

    for (Asset* a : assets) {
        if (!a) continue;
        world::GridPoint* gp = world_grid.point_for_asset(a);
        if (!gp) continue;

        const SDL_Point world_pos{ gp->world.x, gp->world.y };

        SDL_FPoint screen_pos = world_grid.floor_warped_screen_position(camera, world_pos);
        if (!std::isfinite(screen_pos.x) || !std::isfinite(screen_pos.y)) {
            screen_pos = camera.map_to_screen(world_pos);
        }

        const float parallax_dx = world_grid.parallax_offset(world_pos);
        const auto effects = camera.compute_render_effects(
            world_pos,
            0.0f,
            settings.base_height_px,
            camera_grid::RenderSmoothingKey(a));

        float base_scale = a->smoothed_scale();
        if (!std::isfinite(base_scale) || base_scale <= 0.0f) {
            base_scale = 1.0f;
        }

        const int fw = (a && a->info) ? std::max(1, a->info->original_canvas_width) : 1;
        const int fh = (a && a->info) ? std::max(1, a->info->original_canvas_height) : 1;
        const float base_sw = static_cast<float>(fw) * base_scale * inv_scale;
        const float base_sh = static_cast<float>(fh) * base_scale * inv_scale;

        float approx_w = base_sw * effects.distance_scale;
        float approx_h = base_sh * effects.distance_scale * effects.vertical_scale;
        const float min_size = std::max(1.0f, min_visible_px);
        approx_w = std::isfinite(approx_w) && approx_w > 0.0f ? std::max(approx_w, min_size) : min_size;
        approx_h = std::isfinite(approx_h) && approx_h > 0.0f ? std::max(approx_h, min_size) : min_size;

        SDL_FRect bounds{
            screen_pos.x - approx_w * 0.5f,
            screen_pos.y - approx_h,
            approx_w,
            approx_h
        };
        const bool on_screen = rects_intersect(bounds, cull_rect);

        gp->screen             = screen_pos;
        gp->parallax_dx        = parallax_dx;
        gp->vertical_scale     = effects.vertical_scale;
        gp->distance_scale     = effects.distance_scale;
        gp->distance_to_camera = 0.0f;
        gp->tilt_radians       = camera.current_pitch_degrees() * static_cast<float>(M_PI) / 180.0f;
        gp->on_screen          = on_screen;

        id_to_index_[gp->id] = warped_points_.size();
        warped_points_.push_back(gp);
        if (on_screen) {
            visible_assets_.push_back(a);
            visible_points_.push_back(gp);
        }
        if (gp->chunk) active_chunks_.push_back(gp->chunk);
    }

    // Deduplicate active chunks
    if (!active_chunks_.empty()) {
        std::sort(active_chunks_.begin(), active_chunks_.end());
        active_chunks_.erase(std::unique(active_chunks_.begin(), active_chunks_.end()), active_chunks_.end());
    }

    rebuild_grid_bounds();
    bounds_.left   = cull_rect.x;
    bounds_.top    = cull_rect.y;
    bounds_.right  = cull_rect.x + cull_rect.w;
    bounds_.bottom = cull_rect.y + cull_rect.h;
}

void grid_camera::clear_grid_state() {
    warped_points_.clear();
    visible_assets_.clear();
    visible_points_.clear();
    active_chunks_.clear();
    id_to_index_.clear();
    cached_world_rect_ = SDL_Rect{0, 0, 0, 0};
    bounds_ = GridBounds{};
}

void grid_camera::rebuild_grid_bounds() {
    if (warped_points_.empty()) {
        cached_world_rect_ = SDL_Rect{0, 0, 0, 0};
        bounds_ = GridBounds{};
        return;
    }

    int minx = std::numeric_limits<int>::max();
    int miny = std::numeric_limits<int>::max();
    int maxx = std::numeric_limits<int>::min();
    int maxy = std::numeric_limits<int>::min();

    for (const world::GridPoint* gp : warped_points_) {
        if (!gp) continue;
        minx = std::min(minx, gp->world.x);
        miny = std::min(miny, gp->world.y);
        maxx = std::max(maxx, gp->world.x);
        maxy = std::max(maxy, gp->world.y);
    }

    if (minx > maxx || miny > maxy) {
        cached_world_rect_ = SDL_Rect{0, 0, 0, 0};
        bounds_ = GridBounds{};
        return;
    }

    cached_world_rect_.x = minx;
    cached_world_rect_.y = miny;
    cached_world_rect_.w = std::max(0, maxx - minx);
    cached_world_rect_.h = std::max(0, maxy - miny);

    // Populate a conservative screen-space bounds; callers may overwrite later.
    bounds_.left = 0.0f;
    bounds_.top = 0.0f;
    bounds_.right = static_cast<float>(screen_width_);
    bounds_.bottom = static_cast<float>(screen_height_);
}

world::GridPoint* grid_camera::grid_point_for_asset(const Asset* asset) {
    if (!asset) return nullptr;
    const std::uint64_t id = asset->grid_id();
    auto it = id_to_index_.find(id);
    if (it == id_to_index_.end()) return nullptr;
    std::size_t idx = it->second;
    if (idx >= warped_points_.size()) return nullptr;
    return warped_points_[idx];
}

const world::GridPoint* grid_camera::grid_point_for_asset(const Asset* asset) const {
    if (!asset) return nullptr;
    const std::uint64_t id = asset->grid_id();
    auto it = id_to_index_.find(id);
    if (it == id_to_index_.end()) return nullptr;
    std::size_t idx = it->second;
    if (idx >= warped_points_.size()) return nullptr;
    return warped_points_[idx];
}
