#include "world/world_grid.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "asset/Asset.hpp"
#include "render/warped_screen_grid.hpp"
#include "utils/log.hpp"
#include "world/chunk.hpp"
#include "world/chunk_manager.hpp"

namespace world {

namespace {

int grid_floor_div(int numerator, int denominator) {
    if (denominator == 0) {
        return 0;
    }
    return static_cast<int>(std::floor(static_cast<double>(numerator) / denominator));
}

constexpr float kParallaxEpsilon = 1e-3f;

SDL_Point world_point_for_asset(const Asset* asset) {
    if (!asset) {
        return SDL_Point{0, 0};
    }
    return SDL_Point{asset->pos.x, asset->pos.y};
}

}

WorldGrid::WorldGrid(SDL_Point origin, int r_chunk)
    : origin_(origin)
    , r_chunk_(std::clamp(r_chunk, 0, vibble::grid::kMaxResolution))
    , grid_resolution_(r_chunk_) {
    invalidate_active_cache();
}

void WorldGrid::set_chunk_resolution(int r) {
    const int clamped = std::clamp(r, 0, vibble::grid::kMaxResolution);
    if (clamped != r) {
        vibble::log::warn(std::string{"[WorldGrid] Requested chunk resolution "} +
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

void WorldGrid::set_origin(SDL_Point origin) {
    origin_ = origin;
    invalidate_active_cache();
}

void WorldGrid::invalidate_active_cache() {
    chunks_.clear_active();
}

const ChunkManager& WorldGrid::chunks() const {
    return chunks_;
}

ChunkManager& WorldGrid::chunks() {
    return chunks_;
}

GridId WorldGrid::make_point_id(int i, int j) const {
    const std::uint32_t ux = static_cast<std::uint32_t>(i);
    const std::uint32_t uy = static_cast<std::uint32_t>(j);
    return (static_cast<GridId>(ux) << 32) | static_cast<GridId>(uy);
}

SDL_Point WorldGrid::grid_index_from_world(SDL_Point world) const {
    return vibble::grid::world_to_grid_index(world, grid_resolution_, origin_);
}

GridId WorldGrid::point_id_from_world(SDL_Point world) const {
    SDL_Point idx = grid_index_from_world(world);
    return make_point_id(idx.x, idx.y);
}

GridPoint* WorldGrid::point_for_id(GridId id) {
    auto it = points_.find(id);
    if (it == points_.end()) {
        return nullptr;
    }
    return &it->second;
}

const GridPoint* WorldGrid::point_for_id(GridId id) const {
    auto it = points_.find(id);
    if (it == points_.end()) {
        return nullptr;
    }
    return &it->second;
}

GridPoint* WorldGrid::point_for_asset(const Asset* asset) {
    if (!asset) {
        return nullptr;
    }
    auto it = asset_to_point_.find(const_cast<Asset*>(asset));
    if (it == asset_to_point_.end()) {
        return nullptr;
    }
    return point_for_id(it->second);
}

const GridPoint* WorldGrid::point_for_asset(const Asset* asset) const {
    if (!asset) {
        return nullptr;
    }
    auto it = asset_to_point_.find(const_cast<Asset*>(asset));
    if (it == asset_to_point_.end()) {
        return nullptr;
    }
    return point_for_id(it->second);
}

Asset* WorldGrid::create_asset_at_point(std::unique_ptr<Asset> a) {
    return register_asset(std::move(a));
}

Asset* WorldGrid::create_asset_at_point(Asset* a) {
    return register_asset(std::unique_ptr<Asset>(a));
}

Asset* WorldGrid::move_asset_to_point(Asset* a, SDL_Point old_pos, SDL_Point new_pos) {
    move_asset(a, old_pos, new_pos);
    return a;
}

Asset* WorldGrid::remove_asset(Asset* a) {
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

std::vector<Asset*> WorldGrid::all_assets() const {
    std::vector<Asset*> out;
    out.reserve(asset_to_point_.size());
    for (const auto& entry : asset_to_point_) {
        out.push_back(entry.first);
    }
    return out;
}

void WorldGrid::remove_asset_from_point(Asset* a, GridPoint& point) {
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

GridPoint& WorldGrid::ensure_point(SDL_Point grid_index) {
    const GridId id = make_point_id(grid_index.x, grid_index.y);
    auto [it, inserted] = points_.try_emplace(id);
    GridPoint& point = it->second;
    if (inserted) {
        point.id = id;
        point.occupants.clear();
    }
    point.grid_index = grid_index;
    return point;
}

std::unique_ptr<Asset> WorldGrid::extract_from_point(Asset* a, GridPoint& point) {
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

void WorldGrid::bind_asset_to_point(Asset* a,
                               GridPoint& point,
                               SDL_Point world_pos,
                               Chunk* owning_chunk,
                               SDL_Point chunk_index) {
    point.id          = make_point_id(point.grid_index.x, point.grid_index.y);
    point.world       = world_pos;
    point.chunk       = owning_chunk;
    point.chunk_index = chunk_index;
    if (a) {
        asset_to_point_[a] = point.id;
        a->set_grid_id(point.id);
    }
}

void WorldGrid::prune_empty_points() {
    for (auto it = points_.begin(); it != points_.end(); ) {
        if (it->second.occupants.empty()) {
            it = points_.erase(it);
        } else {
            ++it;
        }
    }
}

Asset* WorldGrid::register_asset(std::unique_ptr<Asset> a) {
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
    const GridId new_point_id = make_point_id(grid_index.x, grid_index.y);

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

Asset* WorldGrid::register_asset(Asset* a) {
    return register_asset(std::unique_ptr<Asset>(a));
}

Chunk* WorldGrid::ensure_chunk_from_world(SDL_Point world_px) {
    const int chunk_step = 1 << r_chunk_;
    if (chunk_step <= 0) {
        return nullptr;
    }
    const int i = grid_floor_div(world_px.x - origin_.x, chunk_step);
    const int j = grid_floor_div(world_px.y - origin_.y, chunk_step);
    return get_or_create_chunk_ij(i, j);
}

Chunk* WorldGrid::chunk_from_world(SDL_Point world_px) const {
    const int chunk_step = 1 << r_chunk_;
    if (chunk_step <= 0) {
        return nullptr;
    }
    const int i = grid_floor_div(world_px.x - origin_.x, chunk_step);
    const int j = grid_floor_div(world_px.y - origin_.y, chunk_step);
    return chunks_.find(i, j);
}

Chunk* WorldGrid::get_or_create_chunk_ij(int i, int j) {
    return &chunks_.ensure(i, j, r_chunk_, origin_);
}

void WorldGrid::remove_from_chunk(Asset* a, Chunk* c) {
    if (!a || !c) {
        return;
    }
    auto it = std::find(c->assets.begin(), c->assets.end(), a);
    if (it != c->assets.end()) {
        c->assets.erase(it);
    }
}

Asset* WorldGrid::move_asset(Asset* a, SDL_Point old_pos, SDL_Point new_pos) {
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

    Chunk* previous = nullptr;
    auto existing = residency_.find(a);
    if (existing != residency_.end()) {
        previous = existing->second;
    } else {
        previous = chunks_.find(old_i, old_j);
    }
    Chunk& target = chunks_.ensure(new_i, new_j, r_chunk_, origin_);

    if (previous != &target) {
        if (previous) {
            remove_from_chunk(a, previous);
        }
        if (std::find(target.assets.begin(), target.assets.end(), a) == target.assets.end()) {
            target.assets.push_back(a);
        }
        residency_[a] = &target;
    }

    const SDL_Point old_index = grid_index_from_world(old_pos);
    const SDL_Point new_index = grid_index_from_world(new_pos);
    const GridId    new_point_id = make_point_id(new_index.x, new_index.y);
    const GridId    old_point_id = make_point_id(old_index.x, old_index.y);

    std::unique_ptr<Asset> owned;
    const bool point_changed = (new_point_id != old_point_id);
    if (point_changed) {
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

    if (point_changed) {
        if (owned) {
            point.occupants.push_back(std::move(owned));
        } else {

            point.occupants.push_back(std::unique_ptr<Asset>(a));
        }
    } else {

        point.invalidate_screen_data();
    }
    prune_empty_points();

    return a;
}

void WorldGrid::unregister_asset(Asset* a) {
    (void)remove_asset(a);
}

void WorldGrid::rebuild_chunks() {
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

const std::vector<Chunk*>& WorldGrid::active_chunks() const {
    return chunks_.active();
}

void WorldGrid::update_active_chunks(const SDL_Rect& camera_world, int margin_px) {
    const int margin = std::max(0, margin_px);
    SDL_Rect expanded{
        camera_world.x - margin,
        camera_world.y - margin,
        std::max(0, camera_world.w + margin * 2), std::max(0, camera_world.h + margin * 2) };

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

void WorldGrid::set_grid_resolution(int r) {
    const int clamped = std::clamp(r, 0, vibble::grid::kMaxResolution);
    grid_resolution_ = clamped;
}

int WorldGrid::grid_resolution() const {
    return grid_resolution_;
}

}
