#include "animation_update.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "asset/Asset.hpp"
#include "asset/animation.hpp"
#include "asset/animation_frame.hpp"
#include "core/AssetsManager.hpp"

namespace {
constexpr const char* kDefaultAnimation = "default";
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
    forced_active_ = !self_->static_frame;
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

    plan_ = planner_(*self_, sanitizer_.sanitize(*self_, absolute), visited_thresh_);
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

    if (forced_active_) {
        if (!advance(self_->current_frame)) {
            forced_active_ = false;
            if (queued_anim_) {
                switch_to(*queued_anim_);
                queued_anim_.reset();
            }
        }
        return;
    }

    if (queued_anim_ && self_->is_current_animation_last_frame()) {
        switch_to(*queued_anim_);
        queued_anim_.reset();
        forced_active_ = !self_->static_frame;
        if (forced_active_) {
            advance(self_->current_frame);
            return;
        }
    }

    if (!plan_.strides.empty() && player_.tick(*this, plan_, stride_index_, stride_frame_counter_)) {
        return;
    }

    if (self_->get_current_animation() != kDefaultAnimation) {
        switch_to(kDefaultAnimation);
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
    const auto area = self_->get_area("collision_area");
    if (area.get_points().empty()) {
        return pos;
    }
    SDL_Point bottom = area.get_points().front();
    for (const SDL_Point& pt : area.get_points()) {
        if (pt.y > bottom.y) {
            bottom = pt;
        }
    }
    bottom.x += pos.x;
    bottom.y += pos.y;
    return bottom;
}

bool AnimationUpdate::point_in_impassable(SDL_Point, const Asset*) const {
    return false;
}

bool AnimationUpdate::path_blocked(SDL_Point, SDL_Point, const Asset*) const {
    return false;
}