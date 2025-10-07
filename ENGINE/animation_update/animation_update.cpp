#include "animation_update.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "asset/Asset.hpp"
#include "asset/animation.hpp"
#include "asset/animation_frame.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_types.hpp"
#include "animation_update_utils.hpp"
#include "core/AssetsManager.hpp"
#include "core/asset_list.hpp"
#include "utils/area.hpp"

namespace {
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
        if (self_->get_current_animation() != animation_update::detail::kDefaultAnimation) {
            switch_to(animation_update::detail::kDefaultAnimation);
        }
    }
}

void AnimationUpdate::refresh_z_index() {
    if (self_) {
        self_->set_z_index();
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

    if (self_->get_current_animation() != animation_update::detail::kDefaultAnimation) {
        if (!advance(self_->current_frame)) {
            switch_to(animation_update::detail::kDefaultAnimation);
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
    } else {
        const bool force_loop_default =
            self_->current_animation == animation_update::detail::kDefaultAnimation;
        if (anim.loop || force_loop_default) {
            frame = anim.get_first_frame();
        } else {
            return false;
        }
    }

    return true;
}

void AnimationUpdate::switch_to(const std::string& anim_id) {
    if (!self_ || !self_->info) {
        return;
    }

    auto it = self_->info->animations.find(anim_id);
    if (it == self_->info->animations.end()) {
        auto def = self_->info->animations.find(animation_update::detail::kDefaultAnimation);
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
    return animation_update::detail::bottom_middle_for(*self_, pos);
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

    const SDL_Point dest_bottom = animation_update::detail::bottom_middle_for(*self_, to);

    for (Asset* neighbor : gather_impassable_neighbors(*self_)) {
        if (!neighbor || neighbor == self_ || neighbor == ignored || !neighbor->info) {
            continue;
        }

        Area area = neighbor->get_area("impassable");
        if (area.get_points().empty()) {
            area = neighbor->get_area("collision_area");
        }

        if (!area.get_points().empty() && animation_update::detail::segment_hits_area(from, to, area)) {
            return true;
        }

        const bool overlap_check = animation_update::detail::should_consider_overlap(*self_, *neighbor);
        if (overlap_check) {
            const SDL_Point neighbor_bottom = animation_update::detail::bottom_middle_for(*neighbor, neighbor->pos);
            if (animation_update::detail::distance_sq(dest_bottom, neighbor_bottom) <
                animation_update::detail::kOverlapDistanceSq) {
                return true;
            }
        }
    }

    return false;
}