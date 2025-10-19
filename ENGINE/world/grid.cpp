#include "world/grid.hpp"

#include <algorithm>

#include "asset/Asset.hpp"

namespace world {

void Grid::register_asset(Asset* a) {
    if (!a) return;
    const SDL_Point p{a->pos.x, a->pos.y};
    const int step = 1 << r_chunk_;
    const int i = (p.x - origin_.x) / step;
    const int j = (p.y - origin_.y) / step;
    Chunk& c = chunks_.ensure(i, j, r_chunk_, origin_);
    if (std::find(c.assets.begin(), c.assets.end(), a) == c.assets.end()) {
        c.assets.push_back(a);
    }
    residency_[a] = &c;
}

void Grid::move_asset(Asset* a, SDL_Point old_pos, SDL_Point new_pos) {
    if (!a) return;
    const int step = 1 << r_chunk_;
    const int old_i = (old_pos.x - origin_.x) / step;
    const int old_j = (old_pos.y - origin_.y) / step;
    const int new_i = (new_pos.x - origin_.x) / step;
    const int new_j = (new_pos.y - origin_.y) / step;
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

void Grid::update_active_chunks(const SDL_Rect& camera_world, int margin_px) {
    chunks_.clear_active();
    const int step = 1 << r_chunk_;

    const SDL_Rect expanded{
        camera_world.x - margin_px,
        camera_world.y - margin_px,
        camera_world.w + margin_px * 2,
        camera_world.h + margin_px * 2
    };
    const int i_min = (expanded.x - origin_.x) / step;
    const int j_min = (expanded.y - origin_.y) / step;
    const int i_max = ((expanded.x + expanded.w) - origin_.x) / step;
    const int j_max = ((expanded.y + expanded.h) - origin_.y) / step;
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

