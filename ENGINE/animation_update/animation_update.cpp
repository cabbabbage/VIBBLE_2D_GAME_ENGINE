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
#include "util/grid.hpp"

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
    : self_(self), assets_owner_(assets), grid_service_(&vibble::grid::global_grid()) {
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
    manual_animation_.reset();
    switch_to(anim_id);
}

void AnimationUpdate::set_animation_qued(const std::string& anim_id) {
    if (queued_anim_ && *queued_anim_ == anim_id) {
        return;
    }
    queued_anim_ = anim_id;
}

void AnimationUpdate::auto_move(const std::vector<SDL_Point>& rel_checkpoints,
                               int visited_thresh_px,
                               std::optional<int> checkpoint_resolution) {
    if (!self_) {
        return;
    }
    manual_animation_.reset();
    const int resolution = effective_grid_resolution(checkpoint_resolution);
    visited_thresh_      = std::max(0, visited_thresh_px);
    if (resolution > 0) {
        const int step = vibble::grid::delta(resolution);
        if (step > 1 && visited_thresh_ > 0) {
            visited_thresh_ = ((visited_thresh_ + step - 1) / step) * step;
        }
    }
    path_requested = false;

    std::vector<SDL_Point> absolute;
    absolute.reserve(rel_checkpoints.size());
    vibble::grid::Grid& grid_service = grid();
    SDL_Point           cursor_index = grid_service.world_to_index(self_->pos, resolution);
    for (const SDL_Point& delta : rel_checkpoints) {
        SDL_Point delta_indices = grid_service.convert_resolution(delta, 0, resolution);
        cursor_index.x += delta_indices.x;
        cursor_index.y += delta_indices.y;
        SDL_Point next_world = grid_service.index_to_world(cursor_index, resolution);
        absolute.push_back(next_world);
    }

    plan_ = planner_(*self_, sanitizer_.sanitize(*self_, absolute, visited_thresh_), visited_thresh_);
    final_dest = plan_.final_dest;
    stride_index_ = 0;
    stride_frame_counter_ = 0;
    next_checkpoint_index_ = 0;
    mark_progress_toward_checkpoints();

    if (plan_.strides.empty()) {
        if (self_->get_current_animation() != animation_update::detail::kDefaultAnimation) {
            switch_to(animation_update::detail::kDefaultAnimation);
        }
    }
}

void AnimationUpdate::just_move(SDL_Point delta, const std::string& animation_id, bool resort_z) {
    if (!self_ || !self_->info) {
        return;
    }

    queued_anim_.reset();
    manual_animation_.reset();
    clear_movement_plan();

    const int       resolution = effective_grid_resolution(std::nullopt);
    const SDL_Point from{ self_->pos.x, self_->pos.y };
    SDL_Point       world_delta = convert_delta_to_world(delta, resolution);
    const SDL_Point to{ from.x + world_delta.x, from.y + world_delta.y };

    if (world_delta.x != 0 || world_delta.y != 0) {
        if (!path_blocked(from, to, self_, nullptr)) {
            self_->pos = to;
            if (resort_z) {
                refresh_z_index();
            }
        }
    }

    plan_.final_dest = self_->pos;
    final_dest       = self_->pos;

    const std::string desired_animation = animation_id.empty() ? self_->current_animation : animation_id;
    if (desired_animation.empty()) {
        advance(self_->current_frame);
        return;
    }

    if (self_->current_animation != desired_animation) {
        switch_to(desired_animation, path_index_for(desired_animation));
        return;
    }

    if (advance(self_->current_frame)) {
        return;
    }

    auto anim_it = self_->info->animations.find(self_->current_animation);
    if (anim_it == self_->info->animations.end()) {
        switch_to(animation_update::detail::kDefaultAnimation);
        advance(self_->current_frame);
        return;
    }

    Animation& anim = anim_it->second;
    const bool force_loop_default = self_->current_animation == animation_update::detail::kDefaultAnimation;
    if (anim.loop || force_loop_default) {
        switch_to(self_->current_animation, path_index_for(self_->current_animation));
        advance(self_->current_frame);
    } else {
        switch_to(animation_update::detail::kDefaultAnimation);
        advance(self_->current_frame);
    }
}

void AnimationUpdate::clear_movement_plan() {
    plan_.strides.clear();
    plan_.sanitized_checkpoints.clear();
    plan_.final_dest = self_ ? self_->pos : SDL_Point{ 0, 0 };
    final_dest = plan_.final_dest;
    stride_index_ = 0;
    stride_frame_counter_ = 0;
    next_checkpoint_index_ = 0;
    path_requested = false;
}

void AnimationUpdate::set_manual_animation(const std::string& anim_id, bool loop) {
    if (!self_ || !self_->info || anim_id.empty()) {
        return;
    }

    clear_movement_plan();

    ManualAnimationState desired{ anim_id, loop };
    if (!manual_animation_ || manual_animation_->id != desired.id || manual_animation_->loop != desired.loop) {
        manual_animation_ = desired;
    }

    queued_anim_.reset();

    if (self_->current_animation != desired.id) {
        switch_to(desired.id, path_index_for(desired.id));
    }
}

void AnimationUpdate::clear_manual_animation() {
    manual_animation_.reset();
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
        manual_animation_.reset();
        return;
    }

    if (queued_anim_ && self_->is_current_animation_last_frame()) {
        switch_to(*queued_anim_);
        queued_anim_.reset();
        manual_animation_.reset();
    }

    if (manual_animation_) {
        const std::string& anim_id = manual_animation_->id;
        if (self_->current_animation != anim_id) {
            switch_to(anim_id, path_index_for(anim_id));
        }
        if (!advance(self_->current_frame)) {
            if (manual_animation_->loop) {
                switch_to(anim_id, path_index_for(anim_id));
                advance(self_->current_frame);
            } else {
                manual_animation_.reset();
                switch_to(animation_update::detail::kDefaultAnimation);
                advance(self_->current_frame);
            }
        }
        return;
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
    std::size_t path_index = path_index_for(self_->current_animation);
    if (!frame) {
        frame = anim.get_first_frame(path_index);
        if (!frame) {
            return false;
        }
    }

    if (anim.locked) {
        self_->static_frame = anim.is_static() || anim.locked;
        return true;
    }

    if (frame->next) {
        frame = frame->next;
    } else {
        const bool force_loop_default =
            self_->current_animation == animation_update::detail::kDefaultAnimation;
        if (anim.loop || force_loop_default) {
            frame = anim.get_first_frame(path_index);
        } else {
            return false;
        }
    }

    return true;
}

void AnimationUpdate::switch_to(const std::string& anim_id, std::size_t path_index) {
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
    path_index = anim.clamp_path_index(path_index);
    AnimationFrame* new_frame = anim.get_first_frame(path_index);
    self_->current_animation = it->first;
    self_->current_frame = new_frame;
    self_->static_frame = anim.is_static() || anim.locked;
    self_->frame_progress = 0.0f;
    active_paths_[self_->current_animation] = path_index;
}

std::size_t AnimationUpdate::path_index_for(const std::string& anim_id) const {
    auto it = active_paths_.find(anim_id);
    if (it != active_paths_.end()) {
        return it->second;
    }
    return 0;
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

    const Assets* assets = assets_owner_ ? assets_owner_ : (self_ ? self_->get_assets() : nullptr);
    if (!animation_update::detail::bottom_point_inside_playable_area(assets, pt)) {
        return true;
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

bool AnimationUpdate::path_blocked(SDL_Point from,
                                   SDL_Point to,
                                   const Asset* ignored,
                                   std::vector<const Asset*>* blockers) const {
    if (!self_ || !self_->info) {
        return false;
    }

    const SDL_Point bottom_from = animation_update::detail::bottom_middle_for(*self_, from);
    const SDL_Point dest_bottom = animation_update::detail::bottom_middle_for(*self_, to);

    const Assets* assets = assets_owner_ ? assets_owner_ : (self_ ? self_->get_assets() : nullptr);
    if (animation_update::detail::segment_leaves_playable_area(assets, bottom_from, dest_bottom)) {
        return true;
    }

    bool blocked = false;
    visit_impassable_neighbors(*self_, [&](Asset* neighbor) {
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

        const bool contains_from = area.contains_point(bottom_from);
        const bool contains_to   = area.contains_point(dest_bottom);
        const bool touches_segment = animation_update::detail::segment_hits_area(from, to, area);

        bool overlaps = false;
        if (!contains_from && !contains_to && !touches_segment) {
            const bool overlap_check = animation_update::detail::should_consider_overlap(*self_, *neighbor);
            if (overlap_check) {
                const SDL_Point neighbor_bottom =
                    animation_update::detail::bottom_middle_for(*neighbor, neighbor->pos);
                overlaps = animation_update::detail::distance_sq(dest_bottom, neighbor_bottom) <
                           animation_update::detail::kOverlapDistanceSq;
            }
        }

        if (!(contains_from || contains_to || touches_segment || overlaps)) {
            return false;
        }

        blocked = true;
        if (blockers) {
            const auto it = std::find(blockers->begin(), blockers->end(), neighbor);
            if (it == blockers->end()) {
                blockers->push_back(neighbor);
            }
        }
        return false;
    });

    return blocked;
}

bool AnimationUpdate::attempt_unstick(SDL_Point from,
                                      SDL_Point to,
                                      const std::vector<const Asset*>& blockers) {
    if (!self_ || !self_->info) {
        return false;
    }

    SDL_Point bottom_from = animation_update::detail::bottom_middle_for(*self_, from);
    SDL_Point bottom_to   = animation_update::detail::bottom_middle_for(*self_, to);

    SDL_Point push{0, 0};
    std::vector<const Asset*> blocking_neighbors = blockers;

    const Assets* assets = assets_owner_ ? assets_owner_ : (self_ ? self_->get_assets() : nullptr);

    if (blocking_neighbors.empty()) {
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
    } else {
        for (const Asset* neighbor : blocking_neighbors) {
            if (!neighbor || neighbor == self_ || !neighbor->info) {
                continue;
            }
            Area area = neighbor->get_area("impassable");
            if (area.get_points().empty()) {
                area = neighbor->get_area("collision_area");
            }
            if (area.get_points().empty()) {
                continue;
            }
            SDL_Point center = area.get_center();
            push.x += bottom_from.x - center.x;
            push.y += bottom_from.y - center.y;
        }
    }

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
        if (!animation_update::detail::bottom_point_inside_playable_area(assets, bottom)) {
            return true;
        }
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
        if (!animation_update::detail::bottom_point_inside_playable_area(assets, bottom)) {
            return false;
        }
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

void AnimationUpdate::mark_progress_toward_checkpoints() {
    if (!self_ || !self_->info) {
        return;
    }

    const int visited_sq = visited_thresh_ * visited_thresh_;
    while (next_checkpoint_index_ < plan_.sanitized_checkpoints.size()) {
        const SDL_Point target = plan_.sanitized_checkpoints[next_checkpoint_index_];
        const int       dist_sq = animation_update::detail::distance_sq(self_->pos, target);

        bool reached = false;
        if (visited_thresh_ == 0) {
            reached = (self_->pos.x == target.x) && (self_->pos.y == target.y);
        } else {
            reached = dist_sq <= visited_sq;
        }

        if (!reached) {
            break;
        }
        ++next_checkpoint_index_;
    }
}

namespace {
bool same_point(SDL_Point lhs, SDL_Point rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}
}

bool AnimationUpdate::adjust_next_checkpoint(const std::vector<const Asset*>& blockers) {
    if (!self_ || !self_->info) {
        return false;
    }

    mark_progress_toward_checkpoints();

    SDL_Point target = (next_checkpoint_index_ < plan_.sanitized_checkpoints.size()) ? plan_.sanitized_checkpoints[next_checkpoint_index_] : final_dest;

    SDL_Point bottom_target = animation_update::detail::bottom_middle_for(*self_, target);

    SDL_Point push{0, 0};
    std::vector<const Asset*> influencing_neighbors;

    auto consider_neighbor = [&](const Asset* neighbor) {
        if (!neighbor || neighbor == self_ || !neighbor->info) {
            return;
        }
        Area area = neighbor->get_area("impassable");
        if (area.get_points().empty()) {
            area = neighbor->get_area("collision_area");
        }
        if (area.get_points().empty()) {
            return;
        }

        bool relevant = area.contains_point(bottom_target) || animation_update::detail::segment_hits_area(self_->pos, target, area);

        if (!relevant) {
            const bool overlap_check = animation_update::detail::should_consider_overlap(*self_, *neighbor);
            if (overlap_check) {
                const SDL_Point neighbor_bottom =
                    animation_update::detail::bottom_middle_for(*neighbor, neighbor->pos);
                relevant = animation_update::detail::distance_sq(bottom_target, neighbor_bottom) < animation_update::detail::kOverlapDistanceSq;
            }
        }

        if (!relevant) {
            return;
        }

        SDL_Point center = area.get_center();
        push.x += bottom_target.x - center.x;
        push.y += bottom_target.y - center.y;
        influencing_neighbors.push_back(neighbor);
};

    if (!blockers.empty()) {
        for (const Asset* neighbor : blockers) {
            consider_neighbor(neighbor);
        }
    }

    if (influencing_neighbors.empty()) {
        visit_impassable_neighbors(*self_, [&](Asset* neighbor) {
            consider_neighbor(neighbor);
            return false;
        });
    }

    if (push.x == 0 && push.y == 0) {
        push.x = target.x - self_->pos.x;
        push.y = target.y - self_->pos.y;
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

    std::vector<SDL_Point> tail;
    for (std::size_t i = next_checkpoint_index_ + 1; i < plan_.sanitized_checkpoints.size(); ++i) {
        tail.push_back(plan_.sanitized_checkpoints[i]);
    }
    if (tail.empty() || !same_point(tail.back(), final_dest)) {
        tail.push_back(final_dest);
    }

    auto try_plan_with_targets = [&](const std::vector<SDL_Point>& targets) {
        if (targets.empty()) {
            return false;
        }
        auto sanitized = sanitizer_.sanitize(*self_, targets, visited_thresh_);
        if (sanitized.empty()) {
            return false;
        }
        Plan new_plan = planner_(*self_, sanitized, visited_thresh_);
        if (new_plan.strides.empty()) {
            return false;
        }

        plan_                  = std::move(new_plan);
        final_dest             = plan_.final_dest;
        stride_index_          = 0;
        stride_frame_counter_  = 0;
        next_checkpoint_index_ = 0;
        path_requested         = false;
        mark_progress_toward_checkpoints();
        return true;
};

    const int max_steps = 24;
    for (SDL_Point dir : directions) {
        SDL_Point candidate = target;

        for (int step = 0; step < max_steps; ++step) {
            SDL_Point next{ candidate.x + dir.x, candidate.y + dir.y };
            if (same_point(next, candidate)) {
                continue;
            }

            SDL_Point bottom_next = animation_update::detail::bottom_middle_for(*self_, next);
            if (point_in_impassable(bottom_next, self_)) {
                break;
            }

            candidate = next;

            std::vector<SDL_Point> attempt_targets;
            attempt_targets.push_back(candidate);
            auto it_begin = tail.begin();
            if (!tail.empty() && same_point(tail.front(), candidate)) {
                ++it_begin;
            }
            attempt_targets.insert(attempt_targets.end(), it_begin, tail.end());

            if (try_plan_with_targets(attempt_targets)) {
                return true;
            }
        }
    }

    return false;
}

bool AnimationUpdate::handle_blocked_path(SDL_Point from,
                                          SDL_Point to,
                                          const std::vector<const Asset*>& blockers) {
    bool moved = attempt_unstick(from, to, blockers);
    if (moved) {
        mark_progress_toward_checkpoints();
    }

    if (adjust_next_checkpoint(blockers)) {
        return true;
    }

    if (replan_to_destination()) {
        return true;
    }

    return moved;
}

bool AnimationUpdate::replan_to_destination() {
    if (!self_ || !self_->info) {
        return false;
    }

    const int visited_sq = visited_thresh_ * visited_thresh_;
    if (visited_sq > 0 &&
        animation_update::detail::distance_sq(self_->pos, final_dest) <= visited_sq) {
        return false;
    }

    mark_progress_toward_checkpoints();

    std::vector<SDL_Point> checkpoints;
    for (std::size_t i = next_checkpoint_index_; i < plan_.sanitized_checkpoints.size(); ++i) {
        checkpoints.push_back(plan_.sanitized_checkpoints[i]);
    }
    if (checkpoints.empty() || !same_point(checkpoints.back(), final_dest)) {
        checkpoints.push_back(final_dest);
    }

    auto sanitized = sanitizer_.sanitize(*self_, checkpoints, visited_thresh_);
    if (sanitized.empty()) {
        return false;
    }

    Plan new_plan = planner_(*self_, sanitized, visited_thresh_);
    if (new_plan.strides.empty()) {
        return false;
    }

    plan_                = std::move(new_plan);
    final_dest           = plan_.final_dest;
    stride_index_        = 0;
    stride_frame_counter_ = 0;
    path_requested       = false;
    next_checkpoint_index_ = 0;
    mark_progress_toward_checkpoints();
    return true;
}

vibble::grid::Grid& AnimationUpdate::grid() const {
    if (grid_service_) {
        return *grid_service_;
    }
    return vibble::grid::global_grid();
}

int AnimationUpdate::effective_grid_resolution(std::optional<int> override_resolution) const {
    if (override_resolution.has_value()) {
        return vibble::grid::clamp_resolution(*override_resolution);
    }
    if (self_) {
        return vibble::grid::clamp_resolution(self_->grid_resolution);
    }
    return 0;
}

SDL_Point AnimationUpdate::convert_delta_to_world(SDL_Point delta, int resolution) const {
    const int           clamped_resolution = vibble::grid::clamp_resolution(resolution);
    vibble::grid::Grid& grid_service       = grid();
    SDL_Point           indices            = grid_service.convert_resolution(delta, 0, clamped_resolution);
    const SDL_Point     origin_world       = grid_service.index_to_world(SDL_Point{ 0, 0 }, clamped_resolution);
    const SDL_Point     target_world       = grid_service.index_to_world(indices, clamped_resolution);
    return SDL_Point{ target_world.x - origin_world.x, target_world.y - origin_world.y };
}
