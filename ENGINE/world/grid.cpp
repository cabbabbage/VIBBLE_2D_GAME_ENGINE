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

namespace {

// Default dt when caller passes bad dt.
constexpr float  kDefaultParallaxDt = 1.0f / 60.0f;
constexpr double kParallaxEpsilon   = 1e-6;
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

} // anonymous namespace

// ParallaxSmoother1D implementation

void Grid::ParallaxSmoother1D::set_params(const TransformSmoothingParams& p) {
    params = p;
    if (!std::isfinite(params.lerp_rate) || params.lerp_rate < 0.0f) {
        params.lerp_rate = 0.0f;
    }
    if (!std::isfinite(params.spring_frequency) || params.spring_frequency < 0.0f) {
        params.spring_frequency = 0.0f;
    }
    if (!std::isfinite(params.max_step) || params.max_step < 0.0f) {
        params.max_step = 0.0f;
    }
    if (!std::isfinite(params.snap_threshold) || params.snap_threshold < 0.0f) {
        params.snap_threshold = 0.0f;
    }
}

void Grid::ParallaxSmoother1D::reset(float value) {
    current  = value;
    target   = value;
    velocity = 0.0f;
}

void Grid::ParallaxSmoother1D::advance(float dt) {
    if (!std::isfinite(dt) || dt <= 0.0f) {
        return;
    }

    const float snap_threshold = std::max(0.0f, params.snap_threshold);
    const float max_step       = std::max(0.0f, params.max_step);

    const float delta = target - current;
    if (snap_threshold > 0.0f && std::fabs(delta) < snap_threshold) {
        current  = target;
        velocity = 0.0f;
        return;
    }

    switch (params.method) {
    case TransformSmoothingMethod::None: {
        current  = target;
        velocity = 0.0f;
        break;
    }
    case TransformSmoothingMethod::Lerp: {
        const float rate = std::max(0.0f, params.lerp_rate);
        if (rate <= 0.0f) {
            current  = target;
            velocity = 0.0f;
            break;
        }
        const float t = 1.0f - std::exp(-rate * dt);
        float step = delta * t;
        if (max_step > 0.0f) {
            const float max_delta = max_step * dt;
            if (step >  max_delta) step =  max_delta;
            if (step < -max_delta) step = -max_delta;
        }
        current += step;
        velocity = step / std::max(dt, 1e-6f);
        break;
    }
    case TransformSmoothingMethod::CriticallyDampedSpring: {
        const float PI_F = 3.14159265358979323846f;
        const float freq = std::max(0.0f, params.spring_frequency);
        if (freq <= 0.0f) {
            current  = target;
            velocity = 0.0f;
            break;
        }
        const float omega = 2.0f * PI_F * freq;
        const float x0    = current - target;
        const float v0    = velocity;
        const float e     = std::exp(-omega * dt);

        const float x = (x0 + (v0 + omega * x0) * dt) * e;
        const float v = (v0 - omega * (v0 + omega * x0) * dt) * e;

        current  = target + x;
        velocity = v;
        break;
    }
    default:
        break;
    }
}

// ParallaxCache implementation

void Grid::ParallaxCache::reset() {
    origin_i = origin_j = 0;
    cells_x  = cells_y  = 0;
    step     = 0;
    values.clear();
    ready = false;
}

void Grid::ParallaxCache::configure(int origin_i_, int origin_j_,
                                    int cells_x_, int cells_y_, int step_) {
    origin_i = origin_i_;
    origin_j = origin_j_;
    cells_x  = cells_x_;
    cells_y  = cells_y_;
    step     = step_;
    const std::size_t total = (cells_x > 0 && cells_y > 0)
        ? static_cast<std::size_t>(cells_x) * static_cast<std::size_t>(cells_y)
        : 0;
    values.assign(total, 0.0f);
    ready = false;
}

bool Grid::ParallaxCache::try_index(int i, int j, int step_param, std::size_t& out_index) const {
    if (!ready) {
        return false;
    }
    if (step_param != step || cells_x <= 0 || cells_y <= 0) {
        return false;
    }
    const int local_i = i - origin_i;
    const int local_j = j - origin_j;
    if (local_i < 0 || local_j < 0 || local_i >= cells_x || local_j >= cells_y) {
        return false;
    }
    const std::size_t idx =
        static_cast<std::size_t>(local_j) * static_cast<std::size_t>(cells_x) +
        static_cast<std::size_t>(local_i);
    if (idx >= values.size()) {
        return false;
    }
    out_index = idx;
    return true;
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
        vibble::log::warn(std::string{"[Grid] Requested chunk resolution "} +
                          std::to_string(r) +
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
    clear_parallax_state();
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

Chunk* Grid::chunk_from_world(SDL_Point world_px) const {
    const int step = 1 << r_chunk_;
    const int i    = floor_div(world_px.x - origin_.x, step);
    const int j    = floor_div(world_px.y - origin_.y, step);
    return chunks_.find(i, j);
}

Chunk* Grid::get_or_create_chunk_ij(int i, int j) {
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
    chunks_.reset();
    for (Asset* asset : assets) {
        register_asset(asset);
    }
}

const std::vector<Chunk*>& Grid::active_chunks() const {
    return chunks_.active();
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

    constexpr int kBorderRadiusChunks = 1;
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

void Grid::clear_parallax_state() {
    parallax_entries_.clear();
    parallax_cache_.reset();
}

std::uint64_t Grid::parallax_key(int i, int j) const {
    const auto hi = static_cast<std::uint32_t>(i);
    const auto lo = static_cast<std::uint32_t>(j);
    return (static_cast<std::uint64_t>(hi) << 32) | static_cast<std::uint64_t>(lo);
}
void Grid::update_parallax(const camera& cam, float dt) {
    const float clamped_dt = (std::isfinite(dt) && dt > 0.0f) ? dt : kDefaultParallaxDt;
    ++parallax_frame_counter_;

    const double camera_height = std::max(kParallaxEpsilon, cam.current_camera_height());

    // Realism flag controls whether parallax runs at all.
    parallax_active_ = cam.realism_enabled();
    if (!parallax_active_ || camera_height <= kParallaxEpsilon) {
        clear_parallax_state();
        return;
    }

    const auto& settings = cam.realism_settings();

    // Camera pitch (in degrees) from runtime, clamped to a sane range.
    double pitch_deg = std::isfinite(cam.current_pitch_degrees())
        ? static_cast<double>(cam.current_pitch_degrees())
        : static_cast<double>(settings.grid_pitch_degrees);
    pitch_deg = std::clamp(pitch_deg,
                           static_cast<double>(camera::kMinPitchDegrees),
                           static_cast<double>(camera::kMaxPitchDegrees));

    constexpr double PI_D = 3.14159265358979323846;
    const double pitch_rad  = pitch_deg * (PI_D / 180.0);
    const double pitch_norm = std::min(std::abs(pitch_rad) / (PI_D / 3.0), 1.0);

    // Camera position in world coordinates (same basis as world positions).
    const SDL_FPoint center_px = cam.get_view_center_f();
    const double base_x = static_cast<double>(center_px.x);
    // Ground anchor represents where the camera rig meets the floor plane.
    const double anchor_y = cam.current_anchor_world_y();

    // Depth reference derived from camera height plus user depth offset.
    const double depth_ref_effect = std::max(
        kParallaxEpsilon,
        camera_height + static_cast<double>(settings.grid_depth_offset_px)
    );

    // Only use parallax_smoothing for temporal smoothing, not for strength.
    TransformSmoothingParams smoothing =
        sanitize_smoothing(settings.parallax_smoothing);
    if (!settings.smooth_motion_zoom) {
        smoothing.method = TransformSmoothingMethod::None;
    }
    // Make parallax smoothing snappy by default if unset.
    if (smoothing.method == TransformSmoothingMethod::Lerp &&
        smoothing.lerp_rate <= 0.0f) {
        smoothing.lerp_rate = 12.5f; // ~0.08s time constant
    } else if (smoothing.method == TransformSmoothingMethod::CriticallyDampedSpring &&
               smoothing.spring_frequency <= 0.0f) {
        smoothing.spring_frequency = 10.0f;
    }

    const auto& active_chunks = chunks_.active();
    if (active_chunks.empty()) {
        clear_parallax_state();
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
        clear_parallax_state();
        return;
    }

    const int chunk_step    = 1 << r_chunk_;
    const int parallax_step = parallax_step_size();
    if (parallax_step <= 0) {
        clear_parallax_state();
        return;
    }

    int world_min_x = origin_.x + active_min_i * chunk_step;
    int world_max_x = origin_.x + (active_max_i + 1) * chunk_step;
    int world_min_y = origin_.y + active_min_j * chunk_step;
    int world_max_y = origin_.y + (active_max_j + 1) * chunk_step;

    // Grow bounds slightly so we have stable parallax near edges.
    world_min_x -= parallax_step;
    world_max_x += parallax_step;
    world_min_y -= parallax_step;
    world_max_y += parallax_step;

    const int cell_i_min = floor_div(world_min_x - origin_.x, parallax_step);
    const int cell_i_max = floor_div((world_max_x - 1) - origin_.x, parallax_step);
    const int cell_j_min = floor_div(world_min_y - origin_.y, parallax_step);
    const int cell_j_max = floor_div((world_max_y - 1) - origin_.y, parallax_step);
    const int cells_x    = std::max(0, cell_i_max - cell_i_min + 1);
    const int cells_y    = std::max(0, cell_j_max - cell_j_min + 1);
    if (cells_x == 0 || cells_y == 0) {
        clear_parallax_state();
        return;
    }

    parallax_cache_.configure(cell_i_min, cell_j_min, cells_x, cells_y, parallax_step);
    const std::size_t row_stride = static_cast<std::size_t>(cells_x);

    const double step_d     = static_cast<double>(parallax_step);
    const double origin_x_d = static_cast<double>(origin_.x);
    const double origin_y_d = static_cast<double>(origin_.y);

    // Base parallax gain. Pitch makes it stronger as tilt increases.
    constexpr double kParallaxAmountBase = 0.5;
    const double pitch_gain              = 1.0 + 0.5 * std::sin(std::abs(pitch_rad));
    const double parallax_amount         = kParallaxAmountBase * pitch_gain;
    const double cos_p                   = std::cos(pitch_rad);
    const double sin_p                   = std::sin(pitch_rad);
    const double height_projection       = std::max(kParallaxEpsilon, camera_height * cos_p);

    for (int cell_j = cell_j_min; cell_j <= cell_j_max; ++cell_j) {
        const double cell_cy = origin_y_d + (static_cast<double>(cell_j) + 0.5) * step_d;
        const std::size_t row_offset =
            static_cast<std::size_t>(cell_j - cell_j_min) * row_stride;

        for (int cell_i = cell_i_min; cell_i <= cell_i_max; ++cell_i) {
            const double cell_cx = origin_x_d + (static_cast<double>(cell_i) + 0.5) * step_d;

            const double dx_world = cell_cx - base_x;

            // Measure ground distance relative to the camera's anchor on the floor so
            // rows closer to the camera gain stronger parallax than rows near the
            // horizon.
            const double ground_distance = std::max(0.0, anchor_y - cell_cy);

            // Project the ground point into camera space using the real camera height
            // and pitch so the parallax gain matches the floor perspective.
            const double y_cam = ground_distance * cos_p + camera_height * sin_p;
            const double z_cam = ground_distance * sin_p - camera_height * cos_p;
            const double forward_depth = std::max(kParallaxEpsilon, -z_cam);

            // Depth based attenuation driven by real camera height and pitch. Parallax
            // must shrink as world Y moves away from the anchor toward the horizon.
            const double depth_gain  = height_projection / (forward_depth + depth_ref_effect);

            double parallax_px = dx_world * depth_gain * parallax_amount;
            parallax_px = std::clamp(parallax_px, -kParallaxMax, kParallaxMax);
            const float target = static_cast<float>(parallax_px);

            auto& entry = parallax_entries_[parallax_key(cell_i, cell_j)];
            entry.smoothing.set_params(smoothing);
            entry.last_used_frame = parallax_frame_counter_;

            bool force_snap = !entry.initialized ||
                              smoothing.method == TransformSmoothingMethod::None;
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

            const std::size_t col_index = static_cast<std::size_t>(cell_i - cell_i_min);
            const std::size_t idx       = row_offset + col_index;
            if (idx < parallax_cache_.values.size()) {
                parallax_cache_.values[idx] = entry.last_value;
            }
        }
    }
    parallax_cache_.mark_ready();

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
    std::size_t cache_index = 0;
    if (parallax_cache_.try_index(i, j, step, cache_index)) {
        return parallax_cache_.values[cache_index];
    }
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

SDL_FPoint Grid::floor_warped_screen_position(const camera& cam, SDL_Point world) const {
    // Map to linear screen space first.
    SDL_FPoint base = cam.map_to_screen(world);

    // Warp vertical placement to simulate perspective floor spacing.
    const float warped_y = cam.warp_floor_screen_y(
        static_cast<float>(world.y),
        base.y
    );
    base.y = warped_y;

    // Apply existing x parallax shift.
    return parallax_adjusted_screen_position(world, base);
}

} // namespace world
