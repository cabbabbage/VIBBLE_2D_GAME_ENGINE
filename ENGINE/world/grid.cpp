#include "world/grid.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "asset/Asset.hpp"
#include "util/grid.hpp"
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
    , r_chunk_(std::clamp(r_chunk, 0, vibble::grid::kMaxResolution)) {
    const int default_subdivisions = std::clamp(1 << std::min(2, std::max(0, r_chunk_)), 1, 8);
    requested_lighting_subdivisions_ = default_subdivisions;
    cached_lighting_subdivisions_    = clamp_lighting_subdivisions(default_subdivisions);
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
    refresh_lighting_subdivision_cache(true);
    rebuild_chunks();
}

void Grid::register_asset(Asset* a) {
    if (!a) return;
    const SDL_Point p{a->pos.x, a->pos.y};
    const int step = 1 << r_chunk_;
    const int i = floor_div(p.x - origin_.x, step);
    const int j = floor_div(p.y - origin_.y, step);
    Chunk& c = chunks_.ensure(i, j, r_chunk_, origin_, lighting_subdivisions_per_chunk());
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
    return &chunks_.ensure(i, j, r_chunk_, origin_, lighting_subdivisions_per_chunk());
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
    Chunk& dest = chunks_.ensure(new_i, new_j, r_chunk_, origin_, lighting_subdivisions_per_chunk());
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

    const int i_min = floor_div(expanded.x - origin_.x, step);
    const int j_min = floor_div(expanded.y - origin_.y, step);
    const int i_max = floor_div((expanded.x + expanded.w) - origin_.x, step);
    const int j_max = floor_div((expanded.y + expanded.h) - origin_.y, step);
    for (int j = j_min; j <= j_max; ++j) {
        for (int i = i_min; i <= i_max; ++i) {
            Chunk& c = chunks_.ensure(i, j, r_chunk_, origin_, lighting_subdivisions_per_chunk());
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

int Grid::clamp_lighting_subdivisions(int subdivisions) const {
    return std::clamp(subdivisions, 1, 8);
}

bool Grid::refresh_lighting_subdivision_cache(bool apply_to_chunks) {
    const int clamped = clamp_lighting_subdivisions(requested_lighting_subdivisions_);
    if (clamped == cached_lighting_subdivisions_) {
        return false;
    }
    cached_lighting_subdivisions_ = clamped;
    if (apply_to_chunks) {
        for (const auto& chunk_ptr : chunks_.storage()) {
            if (chunk_ptr) {
                chunk_ptr->set_lighting_subdivisions(cached_lighting_subdivisions_);
                chunk_ptr->releaseLightingArtifacts();
            }
        }
    }
    return true;
}

int Grid::lighting_subdivisions_per_chunk() const {
    return clamp_lighting_subdivisions(cached_lighting_subdivisions_);
}

bool Grid::set_lighting_subdivisions_per_chunk(int subdivisions) {
    requested_lighting_subdivisions_ = clamp_lighting_subdivisions(subdivisions);
    return refresh_lighting_subdivision_cache(true);
}

}

