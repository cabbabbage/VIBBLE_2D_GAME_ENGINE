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

void Grid::register_asset(Asset* a) {
    if (!a) return;
    const SDL_Point p{a->pos.x, a->pos.y};
    const int step = 1 << r_chunk_;
    const int i = floor_div(p.x - origin_.x, step);
    const int j = floor_div(p.y - origin_.y, step);
    Chunk& c = chunks_.ensure(i, j, r_chunk_, origin_);
    if (std::find(c.assets.begin(), c.assets.end(), a) == c.assets.end()) {
        c.assets.push_back(a);
    }
    residency_[a] = &c;
}

Chunk* Grid::ensure_chunk_from_world(SDL_Point world_px) {
    const int step = 1 << r_chunk_;
    const int i    = floor_div(world_px.x - origin_.x, step);
    const int j    = floor_div(world_px.y - origin_.y, step);
    return &chunks_.ensure(i, j, r_chunk_, origin_);
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
    chunks_.clear_active();
    const int step = 1 << r_chunk_;

    const SDL_Rect expanded{
        camera_world.x - margin_px,
        camera_world.y - margin_px,
        camera_world.w + margin_px * 2,
        camera_world.h + margin_px * 2
    };
    const int i_min = floor_div(expanded.x - origin_.x, step);
    const int j_min = floor_div(expanded.y - origin_.y, step);
    const int i_max = floor_div((expanded.x + expanded.w) - origin_.x, step);
    const int j_max = floor_div((expanded.y + expanded.h) - origin_.y, step);
    for (int j = j_min; j <= j_max; ++j) {
        for (int i = i_min; i <= i_max; ++i) {
            Chunk& c = chunks_.ensure(i, j, r_chunk_, origin_);
            chunks_.active().push_back(&c);
        }
    }
}

void Grid::remove_from_chunk(Asset* a, Chunk* c) {
    if (!c) return;
    auto& v = c->assets;
    v.erase(std::remove(v.begin(), v.end(), a), v.end());
}

} // namespace world

