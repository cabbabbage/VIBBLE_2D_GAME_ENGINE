#include "world/grid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <tuple>
#include <vector>

#include "asset/Asset.hpp"
#include "render/camera_grid.hpp"
#include "utils/grid.hpp"
#include "utils/area.hpp"
#include "utils/log.hpp"

namespace world {

namespace {

constexpr float kDefaultParallaxDt = 1.0f / 60.0f;
constexpr double kPi = 3.14159265358979323846;
constexpr double kHalfFovY = 3.14159265358979323846 / 3.0; // 60 degrees
constexpr double kParallaxEpsilon = 1e-3;
constexpr double kParallaxMaxScreenRatio = 0.35;

int grid_floor_div(int a, int b) {
    if (b == 0) {
        return 0;
    }
    int q = a / b;
    int r = a % b;
    if ((r != 0) && ((r > 0) != (b > 0))) {
        --q;
    }
    return q;
}

TransformSmoothingParams sanitize_smoothing(TransformSmoothingParams p) {
    TransformSmoothingParams out{};
    out.method = p.method;
    switch (p.method) {
    case TransformSmoothingMethod::None:
        out.lerp_rate = 0.0f;
        out.spring_frequency = 0.0f;
        out.max_step = 0.0f;
        out.snap_threshold = 0.0f;
        break;
    case TransformSmoothingMethod::Lerp:
        out.lerp_rate = std::isfinite(p.lerp_rate) && p.lerp_rate > 0.0f ? p.lerp_rate : 0.0f;
        out.spring_frequency = 0.0f;
        out.max_step = std::isfinite(p.max_step) && p.max_step > 0.0f ? p.max_step : 0.0f;
        out.snap_threshold = std::isfinite(p.snap_threshold) && p.snap_threshold > 0.0f ? p.snap_threshold : 0.0f;
        break;
    case TransformSmoothingMethod::CriticallyDampedSpring:
        out.spring_frequency = std::isfinite(p.spring_frequency) && p.spring_frequency > 0.0f ? p.spring_frequency : 0.0f;
        out.lerp_rate = 0.0f;
        out.max_step = std::isfinite(p.max_step) && p.max_step > 0.0f ? p.max_step : 0.0f;
        out.snap_threshold = std::isfinite(p.snap_threshold) && p.snap_threshold > 0.0f ? p.snap_threshold : 0.0f;
        break;
    default:
        out.method = TransformSmoothingMethod::None;
        out.lerp_rate = 0.0f;
        out.spring_frequency = 0.0f;
        out.max_step = 0.0f;
        out.snap_threshold = 0.0f;
        break;
    }
    return out;
}

double wrap_degrees_0_360(double raw_value) {
    if (!std::isfinite(raw_value)) {
        return 0.0;
    }
    double wrapped = std::fmod(raw_value, 360.0);
    if (wrapped < 0.0) wrapped += 360.0;
    if (wrapped >= 360.0) wrapped -= 360.0;
    return wrapped;
}

} // namespace

Grid::ParallaxSmoothingState::ParallaxSmoothingState()
    : current(0.0f)
    , target(0.0f)
    , velocity(0.0f)
    , params{}
    , initialized(false) {
}

void Grid::ParallaxSmoothingState::set_params(const TransformSmoothingParams& p) {
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

void Grid::ParallaxSmoothingState::reset(float value) {
    current  = value;
    target   = value;
    this->velocity = 0.0f;
}

void Grid::ParallaxSmoothingState::advance(float dt) {
    if (!std::isfinite(dt) || dt <= 0.0f) {
        return;
    }

    const float snap_threshold = std::max(0.0f, params.snap_threshold);
    const float max_step       = std::max(0.0f, params.max_step);

    const float delta = target - current;
    if (!std::isfinite(delta)) {
        return;
    }

    if (snap_threshold > 0.0f && std::fabs(delta) >= snap_threshold) {
        current  = target;
        velocity = 0.0f;
        return;
    }

    switch (params.method) {
    case TransformSmoothingMethod::None:
        current  = target;
        velocity = 0.0f;
        break;
    case TransformSmoothingMethod::Lerp:
        {
            float rate = params.lerp_rate;
            if (!std::isfinite(rate) || rate <= 0.0f) {
                rate = 0.0f;
            }
            const float alpha = 1.0f - std::exp(-rate * dt);
            current = current + alpha * (target - current);
            velocity = 0.0f;
            break;
        }
    case TransformSmoothingMethod::CriticallyDampedSpring:
        {
            float freq = params.spring_frequency;
            if (!std::isfinite(freq) || freq <= 0.0f) {
                freq = 0.0f;
            }
            if (freq <= 0.0f) {
                current  = target;
                velocity = 0.0f;
            } else {
                const float omega = 2.0f * 3.14159265358979323846f * freq;
                const float x     = current - target;
                const float exp_term = std::exp(-omega * dt);
                const float new_x    = (x + (velocity / omega)) * exp_term - (velocity / omega) * exp_term * exp_term;
                const float new_v    = (velocity * exp_term * exp_term) - (omega * new_x);
                current  = target + new_x;
                velocity = new_v;
            }
            break;
        }
    default:
        current  = target;
        velocity = 0.0f;
        break;
    }

    if (max_step > 0.0f && dt > 0.0f) {
        const float max_delta = max_step * dt;
        const float applied_delta = current - target;
        if (std::fabs(applied_delta) > max_delta) {
            const float clipped = target + std::copysign(max_delta, applied_delta);
            current = clipped;
        }
    }
}



Grid::ParallaxCache::ParallaxCache()
    : origin_i(0)
    , origin_j(0)
    , width(0)
    , height(0)
    , step(0)
    , ready(false) {
}

void Grid::ParallaxCache::configure(int i0, int j0, int w, int h, int s) {
    origin_i = i0;
    origin_j = j0;
    width    = std::max(0, w);
    height   = std::max(0, h);
    step     = s;
    const std::size_t count = static_cast<std::size_t>(std::max(0, width * height));
    values.assign(count, 0.0f);
    ready = true;
}

bool Grid::ParallaxCache::try_index(int i, int j, int s, std::size_t& out_index) const {
    if (!ready || s != step || width <= 0 || height <= 0) {
        return false;
    }
    const int local_i = i - origin_i;
    const int local_j = j - origin_j;
    if (local_i < 0 || local_i >= width || local_j < 0 || local_j >= height) {
        return false;
    }
    const std::size_t idx = static_cast<std::size_t>(local_j * width + local_i);
    if (idx >= values.size()) {
        return false;
    }
    out_index = idx;
    return true;
}



void Grid::ParallaxCache::clear() {
    origin_i = 0;
    origin_j = 0;
    width    = 0;
    height   = 0;
    step     = 0;
    ready    = false;
    values.clear();
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
    invalidate_active_cache();
}

void Grid::set_origin(SDL_Point origin) {
    origin_ = origin;
    invalidate_active_cache();
}

void Grid::invalidate_active_cache() {
    chunks_.clear_active();
    parallax_entries_.clear();
    parallax_cache_.clear();
}

const ChunkManager& Grid::chunks() const {
    return chunks_;
}

ChunkManager& Grid::chunks() {
    return chunks_;
}

SDL_Point Grid::grid_index_from_world(SDL_Point world) const {
    return vibble::grid::world_to_grid_index(world, parallax_resolution_, origin_);
}

GridId Grid::point_id_from_world(SDL_Point world) const {
    SDL_Point idx = grid_index_from_world(world);
    return parallax_key(idx.x, idx.y);
}

GridPoint* Grid::point_for_id(GridId id) {
    auto it = points_.find(id);
    if (it == points_.end()) {
        return nullptr;
    }
    return &it->second;
}

const GridPoint* Grid::point_for_id(GridId id) const {
    auto it = points_.find(id);
    if (it == points_.end()) {
        return nullptr;
    }
    return &it->second;
}

GridPoint* Grid::point_for_asset(const Asset* asset) {
    if (!asset) {
        return nullptr;
    }
    auto it = asset_to_point_.find(const_cast<Asset*>(asset));
    if (it == asset_to_point_.end()) {
        return nullptr;
    }
    return point_for_id(it->second);
}

const GridPoint* Grid::point_for_asset(const Asset* asset) const {
    if (!asset) {
        return nullptr;
    }
    auto it = asset_to_point_.find(const_cast<Asset*>(asset));
    if (it == asset_to_point_.end()) {
        return nullptr;
    }
    return point_for_id(it->second);
}

Asset* Grid::create_asset_at_point(std::unique_ptr<Asset> a) {
    return register_asset(std::move(a));
}

Asset* Grid::create_asset_at_point(Asset* a) {
    return register_asset(std::unique_ptr<Asset>(a));
}

Asset* Grid::move_asset_to_point(Asset* a, SDL_Point old_pos, SDL_Point new_pos) {
    move_asset(a, old_pos, new_pos);
    return a;
}

Asset* Grid::remove_asset(Asset* a) {
    if (!a) {
        return nullptr;
    }

    bool removed_from_point = false;
    auto point_lookup = asset_to_point_.find(a);
    if (point_lookup != asset_to_point_.end()) {
        auto point_it = points_.find(point_lookup->second);
        if (point_it != points_.end()) {
            remove_asset_from_point(a, point_it->second);
        }
        asset_to_point_.erase(point_lookup);
        removed_from_point = true;
    }

    if (!removed_from_point) {
        for (auto& entry : points_) {
            auto& point = entry.second;
            auto it = std::find_if(point.occupants.begin(), point.occupants.end(),
                [a](const std::unique_ptr<Asset>& up) { return up.get() == a; });
            if (it != point.occupants.end()) {
                remove_asset_from_point(a, point);
                removed_from_point = true;
                break;
            }
        }
    }

    auto it = residency_.find(a);
    if (it != residency_.end()) {
        remove_from_chunk(a, it->second);
        residency_.erase(it);
    }

    prune_empty_points();

    return a;
}

std::vector<Asset*> Grid::all_assets() const {
    std::vector<Asset*> out;
    out.reserve(asset_to_point_.size());
    for (const auto& entry : asset_to_point_) {
        out.push_back(entry.first);
    }
    return out;
}

void Grid::remove_asset_from_point(Asset* a, GridPoint& point) {
    if (!a) {
        return;
    }
    auto it = std::find_if(point.occupants.begin(), point.occupants.end(),
        [a](const std::unique_ptr<Asset>& up) { return up.get() == a; });
    if (it != point.occupants.end()) {
        point.occupants.erase(it);
    }
    if (a->grid_id() == point.id) {
        a->clear_grid_id();
    }
}

GridPoint& Grid::ensure_point(SDL_Point grid_index) {
    const GridId id = parallax_key(grid_index.x, grid_index.y);
    auto [it, inserted] = points_.try_emplace(id);
    GridPoint& point = it->second;
    if (inserted) {
        point.id = id;
        point.occupants.clear();
    }
    point.grid_index = grid_index;
    return point;
}

std::unique_ptr<Asset> Grid::extract_from_point(Asset* a, GridPoint& point) {
    if (!a) {
        return nullptr;
    }
    auto it = std::find_if(point.occupants.begin(), point.occupants.end(),
        [a](const std::unique_ptr<Asset>& up) { return up.get() == a; });
    if (it == point.occupants.end()) {
        return nullptr;
    }
    std::unique_ptr<Asset> owned = std::move(*it);
    point.occupants.erase(it);
    if (owned) {
        owned->clear_grid_id();
    }
    return owned;
}

void Grid::bind_asset_to_point(Asset* a,
                               GridPoint& point,
                               SDL_Point world_pos,
                               Chunk* owning_chunk,
                               SDL_Point chunk_index) {
    point.id          = parallax_key(point.grid_index.x, point.grid_index.y);
    point.world       = world_pos;
    point.chunk       = owning_chunk;
    point.chunk_index = chunk_index;
    if (a) {
        asset_to_point_[a] = point.id;
        a->set_grid_id(point.id);
    }
}

void Grid::prune_empty_points() {
    for (auto it = points_.begin(); it != points_.end(); ) {
        if (it->second.occupants.empty()) {
            it = points_.erase(it);
        } else {
            ++it;
        }
    }
}

namespace {
SDL_Point world_point_for_asset(const Asset* asset) {
    if (!asset) {
        return SDL_Point{0, 0};
    }
    return SDL_Point{asset->pos.x, asset->pos.y};
}
} // namespace

Asset* Grid::register_asset(std::unique_ptr<Asset> a) {
    if (!a) {
        return nullptr;
    }
    Asset* raw = a.get();
    const int chunk_step = 1 << r_chunk_;
    if (chunk_step <= 0) {
        return raw;
    }

    const SDL_Point world_pos = world_point_for_asset(raw);
    const SDL_Point grid_index = grid_index_from_world(world_pos);
    const GridId new_point_id = parallax_key(grid_index.x, grid_index.y);

    auto existing_point_it = asset_to_point_.find(raw);
    if (existing_point_it != asset_to_point_.end() && existing_point_it->second != new_point_id) {
        auto point_it = points_.find(existing_point_it->second);
        if (point_it != points_.end()) {
            remove_asset_from_point(raw, point_it->second);
        }
        asset_to_point_.erase(existing_point_it);
        prune_empty_points();
    }

    const int i = grid_floor_div(world_pos.x - origin_.x, chunk_step);
    const int j = grid_floor_div(world_pos.y - origin_.y, chunk_step);
    Chunk& chunk = chunks_.ensure(i, j, r_chunk_, origin_);

    auto ensure_asset_in_chunk = [&]() {
        auto it = std::find(chunk.assets.begin(), chunk.assets.end(), raw);
        if (it == chunk.assets.end()) {
            chunk.assets.push_back(raw);
        }
    };

    auto existing = residency_.find(raw);
    if (existing != residency_.end()) {
        Chunk* previous = existing->second;
        if (previous == &chunk) {
            ensure_asset_in_chunk();
            return raw;
        }
        remove_from_chunk(raw, previous);
        existing->second = &chunk;
    } else {
        residency_[raw] = &chunk;
    }
    ensure_asset_in_chunk();

    GridPoint& point = ensure_point(grid_index);
    bind_asset_to_point(raw, point, world_pos, &chunk, SDL_Point{i, j});
    point.occupants.push_back(std::move(a));
    return raw;
}

Asset* Grid::register_asset(Asset* a) {
    return register_asset(std::unique_ptr<Asset>(a));
}

Chunk* Grid::ensure_chunk_from_world(SDL_Point world_px) {
    const int chunk_step = 1 << r_chunk_;
    if (chunk_step <= 0) {
        return nullptr;
    }
    const int i = grid_floor_div(world_px.x - origin_.x, chunk_step);
    const int j = grid_floor_div(world_px.y - origin_.y, chunk_step);
    return get_or_create_chunk_ij(i, j);
}

Chunk* Grid::chunk_from_world(SDL_Point world_px) const {
    const int chunk_step = 1 << r_chunk_;
    if (chunk_step <= 0) {
        return nullptr;
    }
    const int i = grid_floor_div(world_px.x - origin_.x, chunk_step);
    const int j = grid_floor_div(world_px.y - origin_.y, chunk_step);
    return chunks_.find(i, j);
}

Chunk* Grid::get_or_create_chunk_ij(int i, int j) {
    return &chunks_.ensure(i, j, r_chunk_, origin_);
}

/*
std::vector<Chunk*> Grid::all_chunks() const {
    const auto& storage = chunks_.storage();
    std::vector<Chunk*> result;
    result.reserve(storage.size());
    for (const auto& chunk : storage) {
        if (chunk) {
            result.push_back(chunk.get());
        }
    }
    return result;
}
*/

void Grid::remove_from_chunk(Asset* a, Chunk* c) {
    if (!a || !c) {
        return;
    }
    auto it = std::find(c->assets.begin(), c->assets.end(), a);
    if (it != c->assets.end()) {
        c->assets.erase(it);
    }
}

Asset* Grid::move_asset(Asset* a, SDL_Point old_pos, SDL_Point new_pos) {
    if (!a) {
        return nullptr;
    }
    const int chunk_step = 1 << r_chunk_;
    if (chunk_step <= 0) {
        return nullptr;
    }
    const int old_i = grid_floor_div(old_pos.x - origin_.x, chunk_step);
    const int old_j = grid_floor_div(old_pos.y - origin_.y, chunk_step);
    const int new_i = grid_floor_div(new_pos.x - origin_.x, chunk_step);
    const int new_j = grid_floor_div(new_pos.y - origin_.y, chunk_step);
    if (old_i == new_i && old_j == new_j) {
        return a;
    }

    Chunk* previous = nullptr;
    auto existing = residency_.find(a);
    if (existing != residency_.end()) {
        previous = existing->second;
    } else {
        previous = chunks_.find(old_i, old_j);
    }
    if (previous) {
        remove_from_chunk(a, previous);
    }

    Chunk& target = chunks_.ensure(new_i, new_j, r_chunk_, origin_);
    if (std::find(target.assets.begin(), target.assets.end(), a) == target.assets.end()) {
        target.assets.push_back(a);
    }
    residency_[a] = &target;

    const SDL_Point old_index = grid_index_from_world(old_pos);
    const SDL_Point new_index = grid_index_from_world(new_pos);
    const GridId    new_point_id = parallax_key(new_index.x, new_index.y);
    const GridId    old_point_id = parallax_key(old_index.x, old_index.y);

    std::unique_ptr<Asset> owned;
    if (new_point_id != old_point_id) {
        auto old_point_it = points_.find(old_point_id);
        if (old_point_it != points_.end()) {
            owned = extract_from_point(a, old_point_it->second);
        }

        if (!owned) {
            if (GridPoint* existing_point = point_for_asset(a)) {
                owned = extract_from_point(a, *existing_point);
            }
        }
    }

    GridPoint& point = ensure_point(new_index);
    bind_asset_to_point(a, point, new_pos, &target, SDL_Point{new_i, new_j});
    if (owned) {
        point.occupants.push_back(std::move(owned));
    } else {
        point.occupants.push_back(std::unique_ptr<Asset>(a));
    }
    prune_empty_points();

    return a;
}

void Grid::unregister_asset(Asset* a) {
    (void)remove_asset(a);
}

void Grid::rebuild_chunks() {
    std::vector<std::unique_ptr<Asset>> owned_assets;
    for (auto& entry : points_) {
        for (auto& occ : entry.second.occupants) {
            if (occ) {
                owned_assets.push_back(std::move(occ));
            }
        }
        entry.second.occupants.clear();
    }
    points_.clear();
    asset_to_point_.clear();
    residency_.clear();
    chunks_.reset();
    invalidate_active_cache();

    for (auto& uptr : owned_assets) {
        register_asset(std::move(uptr));
    }
}

const std::vector<Chunk*>& Grid::active_chunks() const {
    return chunks_.active();
}

void Grid::update_active_chunks(const SDL_Rect& camera_world, int margin_px) {
    const int margin = std::max(0, margin_px);
    SDL_Rect expanded{
        camera_world.x - margin,
        camera_world.y - margin,
        std::max(0, camera_world.w + margin * 2),
        std::max(0, camera_world.h + margin * 2)
    };

    const bool needs_update = !has_cached_camera_rect_ ||
        last_margin_px_ != margin_px ||
        last_chunk_resolution_ != r_chunk_ ||
        expanded.x != last_expanded_camera_.x ||
        expanded.y != last_expanded_camera_.y ||
        expanded.w != last_expanded_camera_.w ||
        expanded.h != last_expanded_camera_.h;

    if (!needs_update) {
        return;
    }

    chunks_.clear_active();
    auto& active = chunks_.active();
    const auto& storage = chunks_.storage();
    active.reserve(storage.size());
    for (const auto& chunk : storage) {
        if (!chunk) {
            continue;
        }
        if (chunk->world_bounds.w <= 0 || chunk->world_bounds.h <= 0) {
            continue;
        }
        if (SDL_HasIntersection(&chunk->world_bounds, &expanded) == SDL_TRUE) {
            active.push_back(chunk.get());
        }
    }

    last_expanded_camera_ = expanded;
    last_margin_px_ = margin_px;
    last_chunk_resolution_ = r_chunk_;
    has_cached_camera_rect_ = true;
}

void Grid::set_parallax_resolution(int r) {
    const int clamped = std::clamp(r, 0, vibble::grid::kMaxResolution);
    if (clamped != parallax_resolution_) {
        parallax_resolution_ = clamped;
        parallax_entries_.clear();
        parallax_cache_.clear();
    }
}

int Grid::parallax_resolution() const {
    return parallax_resolution_;
}

int Grid::parallax_step_size() const {
    const int r = std::clamp(parallax_resolution_, 0, vibble::grid::kMaxResolution);
    return 1 << r;
}

ParallaxKey Grid::parallax_key(int i, int j) const {
    return (static_cast<std::uint64_t>(i) << 32) | static_cast<std::uint64_t>(j);
}

void Grid::clear_parallax_state() {
    parallax_active_ = false;
    parallax_entries_.clear();
    parallax_cache_.clear();
}

bool Grid::parallax_active() const {
    return parallax_active_;
}

void Grid::update_parallax(const camera_grid& cam, float dt) {
    const float clamped_dt = (std::isfinite(dt) && dt > 0.0f) ? dt : kDefaultParallaxDt;
    ++parallax_frame_counter_;

    const camera_grid::FloorDepthParams& floor = cam.current_floor_depth_params();
    parallax_active_ = cam.realism_enabled();
    const bool floor_ready = floor.enabled &&
        std::isfinite(floor.camera_height) &&
        std::isfinite(floor.pitch_radians);

    if (!parallax_active_ || !floor_ready || floor.camera_height <= kParallaxEpsilon) {
        clear_parallax_state();
        return;
    }

    const double camera_height = std::max(kParallaxEpsilon, floor.camera_height);
    const auto& settings = cam.realism_settings();

    const double pitch_rad  = floor.pitch_radians;

    const SDL_FPoint center_px = cam.get_view_center_f();
    const double base_x = static_cast<double>(center_px.x);
    const double anchor_y = floor.base_world_y;

    const auto warped_screen_y = [&](double world_y) -> double {
        const float wy = static_cast<float>(world_y);
        SDL_FPoint linear = cam.map_to_screen_f(SDL_FPoint{
            static_cast<float>(base_x),
            wy
        });
        const float warped = cam.warp_floor_screen_y(wy, linear.y);
        return std::isfinite(warped)
            ? static_cast<double>(warped)
            : static_cast<double>(linear.y);
    };

    TransformSmoothingParams smoothing =
        sanitize_smoothing(settings.parallax_smoothing);
    if (smoothing.method == TransformSmoothingMethod::Lerp &&
        smoothing.lerp_rate <= 0.0f) {
        smoothing.lerp_rate = 12.5f;
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

    const float  scale_value = cam.get_scale();
    const double inv_scale   = (std::isfinite(scale_value) && scale_value > 1e-6f)
        ? 1.0 / static_cast<double>(scale_value)
        : 0.0;

    const Area& view_area     = cam.get_camera_area();
    const double view_w_world = static_cast<double>(width_from_area(view_area));
    const double view_h_world = static_cast<double>(height_from_area(view_area));
    if (inv_scale <= 0.0 || view_w_world <= kParallaxEpsilon || view_h_world <= kParallaxEpsilon) {
        clear_parallax_state();
        return;
    }

    const double screen_w_px = std::max(kParallaxEpsilon, view_w_world * inv_scale);
    const double screen_h_px = std::max(kParallaxEpsilon, view_h_world * inv_scale);
    if (screen_w_px <= kParallaxEpsilon || screen_h_px <= kParallaxEpsilon) {
        clear_parallax_state();
        return;
    }

    const double aspect_ratio    = std::max(kParallaxEpsilon, screen_w_px / screen_h_px);
    const double tan_fov_y       = std::tan(kHalfFovY);
    const double tan_fov_x       = std::max(kParallaxEpsilon, tan_fov_y * aspect_ratio);
    const double focal_px        = 0.5 * screen_w_px / tan_fov_x;
    const double max_parallax_px = screen_w_px * kParallaxMaxScreenRatio;

    int world_min_x = origin_.x + active_min_i * chunk_step;
    int world_max_x = origin_.x + (active_max_i + 1) * chunk_step;
    int world_min_y = origin_.y + active_min_j * chunk_step;
    int world_max_y = origin_.y + (active_max_j + 1) * chunk_step;

    world_min_x -= parallax_step;
    world_max_x += parallax_step;
    world_min_y -= parallax_step;
    world_max_y += parallax_step;

    const int cell_i_min = grid_floor_div(world_min_x - origin_.x, parallax_step);
    const int cell_i_max = grid_floor_div((world_max_x - 1) - origin_.x, parallax_step);
    const int cell_j_min = grid_floor_div(world_min_y - origin_.y, parallax_step);
    const int cell_j_max = grid_floor_div((world_max_y - 1) - origin_.y, parallax_step);
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
    const double cos_p       = std::cos(pitch_rad);
    const double sin_p       = std::sin(pitch_rad);

    for (int cell_j = cell_j_min; cell_j <= cell_j_max; ++cell_j) {
        const double cell_cy = origin_y_d + (static_cast<double>(cell_j) + 0.5) * step_d;
        const std::size_t row_offset =
            static_cast<std::size_t>(cell_j - cell_j_min) * row_stride;

        for (int cell_i = cell_i_min; cell_i <= cell_i_max; ++cell_i) {
            const double cell_cx = origin_x_d + (static_cast<double>(cell_i) + 0.5) * step_d;

            const double dx_world = cell_cx - base_x;

            const double depth_world = cell_cy - anchor_y;
            const double y_cam = depth_world * cos_p + camera_height * sin_p;
            const double z_cam = depth_world * sin_p - camera_height * cos_p;
            const double forward = -z_cam;
            if (forward <= kParallaxEpsilon || !std::isfinite(forward)) {
                continue;
            }

            const double ortho_x_px = dx_world * inv_scale;
            const double projected_x_px = (dx_world / forward) * focal_px;

            double parallax_px = projected_x_px - ortho_x_px;
            // Apply a strength multiplier to make parallax more visible (increase from 1.0 to make stronger)
            parallax_px *= 3.0;
            if (!std::isfinite(parallax_px)) {
                parallax_px = 0.0;
            }
            parallax_px = std::clamp(parallax_px, -max_parallax_px, max_parallax_px);
            const float target = static_cast<float>(parallax_px);

            auto& entry = parallax_entries_[parallax_key(cell_i, cell_j)];
            entry.smoothing.set_params(smoothing);
            entry.last_used_frame = parallax_frame_counter_;

            bool force_snap = !entry.initialized;
            if (!force_snap && smoothing.method != TransformSmoothingMethod::None) {
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
    const int i = grid_floor_div(world.x - origin_.x, step);
    const int j = grid_floor_div(world.y - origin_.y, step);
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

SDL_FPoint Grid::floor_warped_screen_position(const camera_grid& cam, SDL_Point world) const {
    SDL_FPoint base = cam.map_to_screen(world);

    const float safe_world_y  = std::isfinite(static_cast<float>(world.y)) ? static_cast<float>(world.y) : 0.0f;
    const float safe_linear_y = std::isfinite(base.y) ? base.y : 0.0f;
    const float warped_y      = cam.warp_floor_screen_y(safe_world_y, safe_linear_y);
    base.y = std::isfinite(warped_y) ? warped_y : safe_linear_y;

    return parallax_adjusted_screen_position(world, base);
}

} // namespace world
