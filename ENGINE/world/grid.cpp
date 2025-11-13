#include "world/grid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <tuple>
#include <vector>

#include "asset/Asset.hpp"
#include "render/camera.hpp"
#include "util/grid.hpp"
#include "utils/area.hpp"
#include "utils/log.hpp"

namespace world {

int floor_div(int value, int step) {
    if (step == 0) {
        return 0;
    }
    const int quotient = value / step;
    const int remainder = value % step;
    if (remainder == 0) {
        return quotient;
    }
    if ((remainder < 0) != (step < 0)) {
        return quotient - 1;
    }
    return quotient;
}

Grid::Grid(SDL_Point origin, int r_chunk)
    : origin_(origin)
    , r_chunk_(std::clamp(r_chunk, 0, vibble::grid::kMaxResolution))
    , parallax_resolution_(r_chunk_) {
    invalidate_active_cache();
}

void Grid::set_chunk_resolution(int r) {
    const int clamped = std::clamp(r, 0, vibble::grid::kMaxResolution);
    if (clamped != r) {
        vibble::log::warn(std::string{"[Grid] Requested chunk resolution "} + std::to_string(r) +
                           " clamped to " + std::to_string(clamped) +
                           " (max=" + std::to_string(vibble::grid::kMaxResolution) + ")");
    }
    if (clamped == r_chunk_) {
        return;
    }
    r_chunk_ = clamped;
    rebuild_chunks();
}

void Grid::set_parallax_resolution(int r) {
    const int clamped = std::clamp(r, 0, vibble::grid::kMaxResolution);
    if (clamped == parallax_resolution_) {
        return;
    }
    parallax_resolution_ = clamped;
    parallax_entries_.clear();
}

int Grid::parallax_step_size() const {
    const int clamped = std::clamp(parallax_resolution_, 0, vibble::grid::kMaxResolution);
    return 1 << clamped;
}

void Grid::register_asset(Asset* a) {
    if (!a) return;
    const SDL_Point p{a->pos.x, a->pos.y};
    const int step = 1 << r_chunk_;
    const int i = floor_div(p.x - origin_.x, step);
    const int j = floor_div(p.y - origin_.y, step);
    Chunk& c = chunks_.ensure(i, j, r_chunk_, origin_);
    if (std::find(c.assets.begin(), c.assets.end(), a) == c.assets.end()) {
        c.assets.push_back(a);
        ++c.occlusion_revision;
    }
    residency_[a] = &c;
}

Chunk* Grid::ensure_chunk_from_world(SDL_Point world_px) {
    const int step = 1 << r_chunk_;
    const int i    = floor_div(world_px.x - origin_.x, step);
    const int j    = floor_div(world_px.y - origin_.y, step);
    return &chunks_.ensure(i, j, r_chunk_, origin_);
}

std::vector<Chunk*> Grid::all_chunks() const {
    std::vector<Chunk*> result;
    const auto& storage = chunks_.storage();
    result.reserve(storage.size());
    for (const auto& chunk : storage) {
        if (chunk) {
            result.push_back(chunk.get());
        }
    }
    return result;
}

void Grid::move_asset(Asset* a, SDL_Point old_pos, SDL_Point new_pos) {
    if (!a) return;
    const int step = 1 << r_chunk_;
    const int old_i = floor_div(old_pos.x - origin_.x, step);
    const int old_j = floor_div(old_pos.y - origin_.y, step);
    const int new_i = floor_div(new_pos.x - origin_.x, step);
    const int new_j = floor_div(new_pos.y - origin_.y, step);
    if (old_i == new_i && old_j == new_j) return;

    Chunk* current = nullptr;
    if (auto it = residency_.find(a); it != residency_.end()) {
        current = it->second;
    }
    if (current) {
        remove_from_chunk(a, current);
    }
    Chunk& dest = chunks_.ensure(new_i, new_j, r_chunk_, origin_);
    dest.assets.push_back(a);
    ++dest.occlusion_revision;
    residency_[a] = &dest;
}

void Grid::unregister_asset(Asset* a) {
    if (!a) return;
    auto it = residency_.find(a);
    if (it == residency_.end()) return;
    Chunk* c = it->second;
    remove_from_chunk(a, c);
    residency_.erase(it);
}

void Grid::rebuild_chunks() {
    invalidate_active_cache();
    std::vector<Asset*> assets;
    assets.reserve(residency_.size());
    for (auto& entry : residency_) {
        if (entry.first) {
            assets.push_back(entry.first);
        }
    }
    residency_.clear();
    chunks_ = ChunkManager{};
    for (Asset* asset : assets) {
        register_asset(asset);
    }
}

void Grid::update_active_chunks(const SDL_Rect& camera_world, int margin_px) {
    const SDL_Rect expanded{
        camera_world.x - margin_px,
        camera_world.y - margin_px,
        camera_world.w + margin_px * 2,
        camera_world.h + margin_px * 2
    };

    if (has_cached_camera_rect_ &&
        last_chunk_resolution_ == r_chunk_ &&
        last_margin_px_ == margin_px &&
        expanded.x == last_expanded_camera_.x &&
        expanded.y == last_expanded_camera_.y &&
        expanded.w == last_expanded_camera_.w &&
        expanded.h == last_expanded_camera_.h) {
        return;
    }

    chunks_.clear_active();
    const int step = 1 << r_chunk_;
    if (step <= 0) {
        last_expanded_camera_ = expanded;
        last_margin_px_ = margin_px;
        last_chunk_resolution_ = r_chunk_;
        has_cached_camera_rect_ = true;
        return;
    }

    const int inclusive_right  = (expanded.w > 0) ? (expanded.x + expanded.w - 1) : expanded.x;
    const int inclusive_bottom = (expanded.h > 0) ? (expanded.y + expanded.h - 1) : expanded.y;

    int i_min = floor_div(expanded.x - origin_.x, step);
    int j_min = floor_div(expanded.y - origin_.y, step);
    int i_max = floor_div(inclusive_right - origin_.x, step);
    int j_max = floor_div(inclusive_bottom - origin_.y, step);

    constexpr int kBorderRadiusChunks = 2;
    i_min -= kBorderRadiusChunks;
    j_min -= kBorderRadiusChunks;
    i_max += kBorderRadiusChunks;
    j_max += kBorderRadiusChunks;

    if (i_min > i_max || j_min > j_max) {
        last_expanded_camera_ = expanded;
        last_margin_px_ = margin_px;
        last_chunk_resolution_ = r_chunk_;
        has_cached_camera_rect_ = true;
        return;
    }

    for (int j = j_min; j <= j_max; ++j) {
        for (int i = i_min; i <= i_max; ++i) {
            Chunk& c = chunks_.ensure(i, j, r_chunk_, origin_);
            chunks_.active().push_back(&c);
        }
    }

    last_expanded_camera_ = expanded;
    last_margin_px_ = margin_px;
    last_chunk_resolution_ = r_chunk_;
    has_cached_camera_rect_ = true;
}

void Grid::remove_from_chunk(Asset* a, Chunk* c) {
    if (!c) return;
    auto& v = c->assets;
    const auto old_size = v.size();
    v.erase(std::remove(v.begin(), v.end(), a), v.end());
    if (v.size() != old_size) {
        ++c->occlusion_revision;
    }
}

void Grid::invalidate_active_cache() {
    has_cached_camera_rect_ = false;
    last_expanded_camera_ = SDL_Rect{0, 0, 0, 0};
    last_margin_px_ = -1;
    last_chunk_resolution_ = -1;
}

std::uint64_t Grid::parallax_key(int i, int j) const {
    const auto hi = static_cast<std::uint32_t>(i);
    const auto lo = static_cast<std::uint32_t>(j);
    return (static_cast<std::uint64_t>(hi) << 32) | static_cast<std::uint64_t>(lo);
}

namespace {
constexpr float  kDefaultParallaxDt = 1.0f / 60.0f;
constexpr double kParallaxEpsilon   = 1e-6;
constexpr double kParallaxSy        = 200.0;
constexpr double kParallaxKv        = 0.25;
constexpr double kParallaxSteepen   = 1.5;
constexpr double kParallaxMax       = 4000.0;

TransformSmoothingParams sanitize_smoothing(const TransformSmoothingParams& params) {
    TransformSmoothingParams out = params;
    if (!std::isfinite(out.lerp_rate) || out.lerp_rate < 0.0f) out.lerp_rate = 0.0f;
    if (!std::isfinite(out.spring_frequency) || out.spring_frequency < 0.0f) out.spring_frequency = 0.0f;
    if (!std::isfinite(out.max_step) || out.max_step < 0.0f) out.max_step = 0.0f;
    if (!std::isfinite(out.snap_threshold) || out.snap_threshold < 0.0f) out.snap_threshold = 0.0f;
    switch (out.method) {
    case TransformSmoothingMethod::None:
    case TransformSmoothingMethod::Lerp:
    case TransformSmoothingMethod::CriticallyDampedSpring:
        break;
    default:
        out.method = TransformSmoothingMethod::None;
        break;
    }
    return out;
}

int width_from_area(const Area& a) {
    int minx, miny, maxx, maxy;
    std::tie(minx, miny, maxx, maxy) = a.get_bounds();
    (void)miny; (void)maxy;
    return std::max(0, maxx - minx);
}

int height_from_area(const Area& a) {
    int minx, miny, maxx, maxy;
    std::tie(minx, miny, maxx, maxy) = a.get_bounds();
    (void)minx; (void)maxx;
    return std::max(0, maxy - miny);
}
}

void Grid::update_parallax(const camera& cam, float dt) {
    const float clamped_dt = (std::isfinite(dt) && dt > 0.0f) ? dt : kDefaultParallaxDt;
    ++parallax_frame_counter_;

    const camera::RealismSettings& settings = cam.realism_settings();
    const double parallax_strength = std::max(0.0f, settings.parallax_strength);
    const float  raw_scale         = cam.get_scale();
    const double scale_value       = std::isfinite(raw_scale) ? static_cast<double>(raw_scale) : 0.0;
    const double safe_scale        = std::max(kParallaxEpsilon, scale_value);
    const double pixels_per_world  = 1.0 / safe_scale;
    const double zoom_norm         = std::clamp(scale_value, 0.0, 1.0);
    const double height_at_zoom1   = std::max(0.0f, settings.height_at_zoom1);
    const double camera_height     = height_at_zoom1 * zoom_norm;

    parallax_active_ = cam.parallax_enabled() && parallax_strength > 0.0 && camera_height > kParallaxEpsilon;
    if (!parallax_active_) {
        parallax_entries_.clear();
        return;
    }

    const Area& view          = cam.get_camera_area();
    const double view_width   = std::max(1.0, static_cast<double>(width_from_area(view)));
    const double view_height  = std::max(1.0, static_cast<double>(height_from_area(view)));
    const double half_width   = std::max(1.0, view_width * 0.5);
    const double half_height  = std::max(1.0, view_height * 0.5);
    const double view_scale_y = view_height / kParallaxSy;

    const double tripod_distance = std::isfinite(settings.tripod_distance_y)
        ? static_cast<double>(settings.tripod_distance_y)
        : 0.0;
    const SDL_Point center_px = cam.get_screen_center();
    const double base_x = static_cast<double>(center_px.x);
    const double base_y = static_cast<double>(center_px.y) - tripod_distance;

    TransformSmoothingParams smoothing = sanitize_smoothing(settings.parallax_smoothing);
    if (!settings.smooth_motion_zoom) {
        smoothing.method = TransformSmoothingMethod::None;
    }

    const auto& active_chunks = chunks_.active();
    if (active_chunks.empty()) {
        parallax_entries_.clear();
        return;
    }

    int active_min_i = std::numeric_limits<int>::max();
    int active_max_i = std::numeric_limits<int>::min();
    int active_min_j = std::numeric_limits<int>::max();
    int active_max_j = std::numeric_limits<int>::min();
    for (const Chunk* chunk : active_chunks) {
        if (!chunk) {
            continue;
        }
        active_min_i = std::min(active_min_i, chunk->i);
        active_max_i = std::max(active_max_i, chunk->i);
        active_min_j = std::min(active_min_j, chunk->j);
        active_max_j = std::max(active_max_j, chunk->j);
    }

    if (active_min_i > active_max_i || active_min_j > active_max_j) {
        parallax_entries_.clear();
        return;
    }

    const int chunk_step    = 1 << r_chunk_;
    const int parallax_step = parallax_step_size();
    if (parallax_step <= 0) {
        parallax_entries_.clear();
        return;
    }

    int world_min_x = origin_.x + active_min_i * chunk_step;
    int world_max_x = origin_.x + (active_max_i + 1) * chunk_step;
    int world_min_y = origin_.y + active_min_j * chunk_step;
    int world_max_y = origin_.y + (active_max_j + 1) * chunk_step;

    world_min_x -= parallax_step;
    world_max_x += parallax_step;
    world_min_y -= parallax_step;
    world_max_y += parallax_step;

    const int cell_i_min = floor_div(world_min_x - origin_.x, parallax_step);
    const int cell_i_max = floor_div((world_max_x - 1) - origin_.x, parallax_step);
    const int cell_j_min = floor_div(world_min_y - origin_.y, parallax_step);
    const int cell_j_max = floor_div((world_max_y - 1) - origin_.y, parallax_step);

    const double step_d     = static_cast<double>(parallax_step);
    const double origin_x_d = static_cast<double>(origin_.x);
    const double origin_y_d = static_cast<double>(origin_.y);

    for (int cell_j = cell_j_min; cell_j <= cell_j_max; ++cell_j) {
        const double cell_cy = origin_y_d + (static_cast<double>(cell_j) + 0.5) * step_d;
        for (int cell_i = cell_i_min; cell_i <= cell_i_max; ++cell_i) {
            const double cell_cx = origin_x_d + (static_cast<double>(cell_i) + 0.5) * step_d;

            const double dx = cell_cx - base_x;
            const double dy = cell_cy - base_y;

            const double ndx = dx / half_width;
            const double ndy = dy / half_height;

            const double vertical_bias = 1.0 + kParallaxKv *
                std::tanh(ndy * view_scale_y * kParallaxSteepen);

            double zoom_gain = (height_at_zoom1 > kParallaxEpsilon)
                ? (height_at_zoom1 / (camera_height + kParallaxEpsilon))
                : 1.0;
            if (zoom_gain >= 1.0) {
                zoom_gain = std::pow(zoom_gain, 1.5);
            }

            double parallax_px = parallax_strength *
                                 ndx * ndy *
                                 pixels_per_world * vertical_bias * zoom_gain;
            parallax_px = std::clamp(parallax_px, -kParallaxMax, kParallaxMax);
            const float target = static_cast<float>(parallax_px);

            auto& entry = parallax_entries_[parallax_key(cell_i, cell_j)];
            entry.smoothing.set_params(smoothing);
            entry.last_used_frame = parallax_frame_counter_;

            bool force_snap = !entry.initialized || smoothing.method == TransformSmoothingMethod::None;
            if (!force_snap) {
                const float snap_threshold = std::max(0.0f, smoothing.snap_threshold);
                const float max_step       = std::max(0.0f, smoothing.max_step);
                const float current        = entry.smoothing.current;
                const float delta          = std::fabs(target - current);
                if (snap_threshold > 0.0f && delta > snap_threshold * 4.0f) {
                    force_snap = true;
                } else if (max_step > 0.0f && clamped_dt > 0.0f) {
                    const float max_delta = max_step * clamped_dt * 4.0f;
                    if (delta > max_delta) {
                        force_snap = true;
                    }
                }
            }

            if (force_snap) {
                entry.smoothing.reset(target);
                entry.smoothing.target = target;
                entry.initialized      = true;
            } else {
                entry.smoothing.target = target;
                entry.smoothing.advance(clamped_dt);
            }

            entry.last_value = entry.smoothing.value_for_render();
        }
    }

    const std::uint64_t prune_threshold = (parallax_frame_counter_ > 480)
        ? parallax_frame_counter_ - 480
        : 0;
    for (auto it = parallax_entries_.begin(); it != parallax_entries_.end(); ) {
        if (it->second.last_used_frame < prune_threshold) {
            it = parallax_entries_.erase(it);
        } else {
            ++it;
        }
    }
}

float Grid::parallax_offset(SDL_Point world) const {
    if (!parallax_active_) {
        return 0.0f;
    }
    const int step = parallax_step_size();
    if (step <= 0) {
        return 0.0f;
    }
    const int i = floor_div(world.x - origin_.x, step);
    const int j = floor_div(world.y - origin_.y, step);
    const auto key = parallax_key(i, j);
    auto it = parallax_entries_.find(key);
    if (it == parallax_entries_.end()) {
        return 0.0f;
    }
    return it->second.last_value;
}

float Grid::parallax_adjusted_screen_x(SDL_Point world, float base_screen_x) const {
    return base_screen_x + parallax_offset(world);
}

SDL_FPoint Grid::parallax_adjusted_screen_position(SDL_Point world, SDL_FPoint base_screen) const {
    base_screen.x = parallax_adjusted_screen_x(world, base_screen.x);
    return base_screen;
}

}

