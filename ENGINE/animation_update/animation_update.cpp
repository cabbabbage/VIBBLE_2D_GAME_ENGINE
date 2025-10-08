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
template <typename Fn>
bool visit_impassable_neighbors(const Asset& asset, Fn&& fn) {
    const AssetList* list = asset.get_impassable_naighbors();
    if (!list) {
        return false;
    }

    const auto visit_bucket = [&](const std::vector<Asset*>& bucket) {
        for (Asset* neighbor : bucket) {
            if (fn(neighbor)) {
                return true;
            }
        }
        return false;
    };

    if (visit_bucket(list->top_unsorted())) {
        return true;
    }
    if (visit_bucket(list->middle_sorted())) {
        return true;
    }
    if (visit_bucket(list->bottom_unsorted())) {
        return true;
    }

    return false;
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

    return visit_impassable_neighbors(*self_, [&](Asset* neighbor) {
        if (!neighbor || neighbor == self_ || neighbor == ignored || !neighbor->info) {
            return false;
        }

        if (neighbor->info->type == asset_types::player) {
            return false;
        }

        Area area = neighbor->get_area("impassable");
        if (area.get_points().empty()) {
            area = neighbor->get_area("collision_area");
        }
        if (area.get_points().empty()) {
            return false;
        }

        return area.contains_point(pt);
    });
}

bool AnimationUpdate::path_blocked(SDL_Point from, SDL_Point to, const Asset* ignored) const {
    if (!self_ || !self_->info) {
        return false;
    }

    const SDL_Point dest_bottom = animation_update::detail::bottom_middle_for(*self_, to);

    return visit_impassable_neighbors(*self_, [&](Asset* neighbor) {
        if (!neighbor || neighbor == self_ || neighbor == ignored || !neighbor->info) {
            return false;
        }

        if (neighbor->info->type == asset_types::player) {
            return false;
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

        return false;
    });
}

bool AnimationUpdate::attempt_unstick(SDL_Point from, SDL_Point to) {
    if (!self_ || !self_->info) {
        return false;
    }

    SDL_Point bottom_from = animation_update::detail::bottom_middle_for(*self_, from);
    SDL_Point bottom_to   = animation_update::detail::bottom_middle_for(*self_, to);

    SDL_Point push{0, 0};
    std::vector<const Asset*> blocking_neighbors;

    visit_impassable_neighbors(*self_, [&](Asset* neighbor) {
        if (!neighbor || neighbor == self_ || !neighbor->info) {
            return false;
        }

        Area area = neighbor->get_area("impassable");
        if (area.get_points().empty()) {
            area = neighbor->get_area("collision_area");
        }
        if (area.get_points().empty()) {
            return false;
        }

        const bool contains_from = area.contains_point(bottom_from);
        const bool contains_to   = area.contains_point(bottom_to);
        const bool touches_segment = animation_update::detail::segment_hits_area(from, to, area);

        bool overlaps = false;
        if (!contains_from && !contains_to && !touches_segment) {
            const bool overlap_check = animation_update::detail::should_consider_overlap(*self_, *neighbor);
            if (overlap_check) {
                const SDL_Point neighbor_bottom =
                    animation_update::detail::bottom_middle_for(*neighbor, neighbor->pos);
                overlaps = animation_update::detail::distance_sq(bottom_from, neighbor_bottom) <
                           animation_update::detail::kOverlapDistanceSq;
            }
        }

        if (!(contains_from || contains_to || touches_segment || overlaps)) {
            return false;
        }

        SDL_Point center = area.get_center();
        push.x += bottom_from.x - center.x;
        push.y += bottom_from.y - center.y;
        blocking_neighbors.push_back(neighbor);
        return false;
    });

    if (push.x == 0 && push.y == 0) {
        push.x = from.x - to.x;
        push.y = from.y - to.y;
    }
    if (push.x == 0 && push.y == 0) {
        push.y = -1;
    }

    SDL_Point primary{ (push.x > 0) ? 1 : (push.x < 0 ? -1 : 0),
                       (push.y > 0) ? 1 : (push.y < 0 ? -1 : 0) };

    auto add_direction = [&](std::vector<SDL_Point>& dirs, SDL_Point dir) {
        if (dir.x == 0 && dir.y == 0) {
            return;
        }
        const auto it = std::find_if(dirs.begin(), dirs.end(), [&](const SDL_Point& existing) {
            return existing.x == dir.x && existing.y == dir.y;
        });
        if (it == dirs.end()) {
            dirs.push_back(dir);
        }
    };

    std::vector<SDL_Point> directions;
    if (primary.x == 0 && primary.y == 0) {
        directions.push_back(SDL_Point{ 1, 0 });
        directions.push_back(SDL_Point{ -1, 0 });
        directions.push_back(SDL_Point{ 0, 1 });
        directions.push_back(SDL_Point{ 0, -1 });
    } else {
        add_direction(directions, primary);
        add_direction(directions, SDL_Point{ primary.x, 0 });
        add_direction(directions, SDL_Point{ 0, primary.y });
        add_direction(directions, SDL_Point{ primary.y, -primary.x });
        add_direction(directions, SDL_Point{ -primary.y, primary.x });
    }

    const auto inside_disallowed = [&](SDL_Point bottom) {
        bool blocked = false;
        visit_impassable_neighbors(*self_, [&](Asset* neighbor) {
            if (!neighbor || neighbor == self_ || !neighbor->info) {
                return false;
            }
            Area area = neighbor->get_area("impassable");
            if (area.get_points().empty()) {
                area = neighbor->get_area("collision_area");
            }
            if (area.get_points().empty()) {
                return false;
            }
            if (!area.contains_point(bottom)) {
                return false;
            }
            const auto it = std::find(blocking_neighbors.begin(), blocking_neighbors.end(), neighbor);
            if (it == blocking_neighbors.end()) {
                blocked = true;
                return true;
            }
            return false;
        });
        return blocked;
    };

    const auto inside_any = [&](SDL_Point bottom) {
        bool inside = false;
        visit_impassable_neighbors(*self_, [&](Asset* neighbor) {
            if (!neighbor || neighbor == self_ || !neighbor->info) {
                return false;
            }
            Area area = neighbor->get_area("impassable");
            if (area.get_points().empty()) {
                area = neighbor->get_area("collision_area");
            }
            if (area.get_points().empty()) {
                return false;
            }
            if (area.contains_point(bottom)) {
                inside = true;
                return true;
            }
            return false;
        });
        return inside;
    };

    const int max_steps = 12;
    for (SDL_Point dir : directions) {
        SDL_Point candidate = self_->pos;
        bool      moved     = false;

        for (int step = 0; step < max_steps; ++step) {
            SDL_Point next{ candidate.x + dir.x, candidate.y + dir.y };
            if (next.x == candidate.x && next.y == candidate.y) {
                continue;
            }

            SDL_Point bottom_next = animation_update::detail::bottom_middle_for(*self_, next);
            if (inside_disallowed(bottom_next)) {
                break;
            }

            candidate = next;
            moved      = true;

            if (!inside_any(bottom_next)) {
                break;
            }
        }

        if (moved) {
            self_->pos = candidate;
            refresh_z_index();
            return true;
        }
    }

    return false;
}
