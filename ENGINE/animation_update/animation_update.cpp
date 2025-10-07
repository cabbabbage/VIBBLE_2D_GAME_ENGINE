#include "animation_update.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "asset/Asset.hpp"
#include "asset/animation.hpp"
#include "asset/animation_frame.hpp"
#include "asset/asset_info.hpp"
#include "core/AssetsManager.hpp"
#include "core/asset_list.hpp"
#include "utils/area.hpp"

namespace {
constexpr const char* kDefaultAnimation = "default";

constexpr int kOverlapDistanceSq = 40 * 40;

int distance_sq(SDL_Point a, SDL_Point b) {
    const int dx = a.x - b.x;
    const int dy = a.y - b.y;
    return dx * dx + dy * dy;
}

bool segment_hits_area(SDL_Point from, SDL_Point to, const Area& area) {
    const int steps = std::max(std::abs(to.x - from.x), std::abs(to.y - from.y));
    if (steps == 0) {
        return area.contains_point(from);
    }

    const double step_x = (to.x - from.x) / static_cast<double>(steps);
    const double step_y = (to.y - from.y) / static_cast<double>(steps);

    for (int i = 0; i <= steps; ++i) {
        SDL_Point sample{ static_cast<int>(std::round(from.x + step_x * i)),
                          static_cast<int>(std::round(from.y + step_y * i)) };
        if (area.contains_point(sample)) {
            return true;
        }
    }
    return false;
}

SDL_Point bottom_middle_for(const Asset& asset, SDL_Point pos) {
    Area area = asset.get_area("collision_area");
    const auto& pts = area.get_points();
    if (pts.empty()) {
        return pos;
    }

    SDL_Point bottom = pts.front();
    for (const SDL_Point& pt : pts) {
        if (pt.y > bottom.y) {
            bottom = pt;
        }
    }

    const int offset_x = bottom.x - asset.pos.x;
    const int offset_y = bottom.y - asset.pos.y;
    return SDL_Point{ pos.x + offset_x, pos.y + offset_y };
}

std::vector<Asset*> gather_impassable_neighbors(const Asset& asset) {
    std::vector<Asset*> neighbors;
    const AssetList* list = asset.get_impassable_naighbors();
    if (!list) {
        return neighbors;
    }
    list->full_list(neighbors);
    return neighbors;
}
}

AnimationUpdate::AnimationUpdate(Asset* self, Assets* assets)
    : self_(self), assets_owner_(assets) {
    if (!assets_owner_ && self_) {
        assets_owner_ = self_->get_assets();
    }
}

AnimationUpdate::AnimationUpdate(Asset* self, Assets* assets, double)
    : AnimationUpdate(self, assets) {}

void AnimationUpdate::set_animation_now(const std::string& anim_id) {
    if (!self_ || !self_->info) {
        return;
    }
    if (anim_id.empty()) {
        return;
    }
    if (self_->current_animation == anim_id) {
        return;
    }
    queued_anim_.reset();
    switch_to(anim_id);
}

void AnimationUpdate::set_animation_qued(const std::string& anim_id) {
    if (queued_anim_ && *queued_anim_ == anim_id) {
        return;
    }
    queued_anim_ = anim_id;
}

void AnimationUpdate::move(const std::vector<SDL_Point>& rel_checkpoints, int visited_thresh_px) {
    if (!self_) {
        return;
    }
    visited_thresh_ = std::max(0, visited_thresh_px);
    path_requested = false;

    std::vector<SDL_Point> absolute;
    absolute.reserve(rel_checkpoints.size());
    SDL_Point cursor = self_->pos;
    for (const SDL_Point& delta : rel_checkpoints) {
        cursor.x += delta.x;
        cursor.y += delta.y;
        absolute.push_back(cursor);
    }

    plan_ = planner_(*self_, sanitizer_.sanitize(*self_, absolute, visited_thresh_), visited_thresh_);
    final_dest = plan_.final_dest;
    stride_index_ = 0;
    stride_frame_counter_ = 0;

    if (plan_.strides.empty()) {
        if (self_->get_current_animation() != kDefaultAnimation) {
            switch_to(kDefaultAnimation);
        }
    }
}

void AnimationUpdate::update() {
    if (!self_ || !self_->info) {
        return;
    }

    if (!plan_.strides.empty() && player_.tick(*this, plan_, stride_index_, stride_frame_counter_)) {
        return;
    }

    if (queued_anim_ && self_->is_current_animation_last_frame()) {
        switch_to(*queued_anim_);
        queued_anim_.reset();
    }

    if (self_->get_current_animation() != kDefaultAnimation) {
        if (!advance(self_->current_frame)) {
            switch_to(kDefaultAnimation);
            advance(self_->current_frame);
        }
        return;
    }

    advance(self_->current_frame);
}

bool AnimationUpdate::advance(AnimationFrame*& frame) {
    if (!self_ || !self_->info) {
        return false;
    }

    auto it = self_->info->animations.find(self_->current_animation);
    if (it == self_->info->animations.end()) {
        return false;
    }

    Animation& anim = it->second;
    if (!frame) {
        frame = anim.get_first_frame();
        if (!frame) {
            return false;
        }
    }

    if (frame->next) {
        frame = frame->next;
    } else if (anim.loop) {
        frame = anim.get_first_frame();
    } else {
        return false;
    }

    return true;
}

void AnimationUpdate::switch_to(const std::string& anim_id) {
    if (!self_ || !self_->info) {
        return;
    }

    auto it = self_->info->animations.find(anim_id);
    if (it == self_->info->animations.end()) {
        auto def = self_->info->animations.find(kDefaultAnimation);
        if (def == self_->info->animations.end()) {
            if (self_->info->animations.empty()) {
                return;
            }
            it = self_->info->animations.begin();
        } else {
            it = def;
        }
    }

    Animation& anim = it->second;
    AnimationFrame* new_frame = anim.get_first_frame();
    self_->current_animation = it->first;
    self_->current_frame = new_frame;
    self_->static_frame = anim.is_static();
    self_->frame_progress = 0.0f;
}

SDL_Point AnimationUpdate::bottom_middle(SDL_Point pos) const {
    if (!self_ || !self_->info) {
        return pos;
    }
    return bottom_middle_for(*self_, pos);
}

bool AnimationUpdate::point_in_impassable(SDL_Point pt, const Asset* ignored) const {
    if (!self_ || !self_->info) {
        return false;
    }

    for (Asset* neighbor : gather_impassable_neighbors(*self_)) {
        if (!neighbor || neighbor == self_ || neighbor == ignored || !neighbor->info) {
            continue;
        }

        Area area = neighbor->get_area("impassable");
        if (area.get_points().empty()) {
            area = neighbor->get_area("collision_area");
        }
        if (area.get_points().empty()) {
            continue;
        }

        if (area.contains_point(pt)) {
            return true;
        }
    }

    return false;
}

bool AnimationUpdate::path_blocked(SDL_Point from, SDL_Point to, const Asset* ignored) const {
    if (!self_ || !self_->info) {
        return false;
    }

    const SDL_Point dest_bottom = bottom_middle_for(*self_, to);

    for (Asset* neighbor : gather_impassable_neighbors(*self_)) {
        if (!neighbor || neighbor == self_ || neighbor == ignored || !neighbor->info) {
            continue;
        }

        Area area = neighbor->get_area("impassable");
        if (area.get_points().empty()) {
            area = neighbor->get_area("collision_area");
        }

        if (!area.get_points().empty() && segment_hits_area(from, to, area)) {
            return true;
        }

        const bool overlap_check = (self_->info && neighbor->info && self_->info->type == neighbor->info->type) ||
                                   (assets_owner_ && assets_owner_->player == neighbor);
        if (overlap_check) {
            const SDL_Point neighbor_bottom = bottom_middle_for(*neighbor, neighbor->pos);
            if (distance_sq(dest_bottom, neighbor_bottom) < kOverlapDistanceSq) {
                return true;
            }
        }
    }

    return false;
}