#include "animation_runtime.hpp"

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
#include "core/AssetsManager.hpp"
#include "core/asset_list.hpp"
#include "stride_player.hpp"
#include "path_sanitizer.hpp"
#include "get_best_path.hpp"
#include "utils/area.hpp"
#include "utils/grid.hpp"
#include <iostream>
#include "animation_update.hpp" // planner interface
#include "animation_update/child_attachment_controller.hpp"
#include "utils/transform_smoothing.hpp"

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

std::string resolve_animation(const Asset& asset, const std::string& requested) {
    if (!asset.info) {
        return animation_update::detail::kDefaultAnimation;
    }

    if (!requested.empty()) {
        auto it = asset.info->animations.find(requested);
        if (it != asset.info->animations.end()) {
            return it->first;
        }
    }

    return animation_update::detail::kDefaultAnimation;
}

bool same_point(SDL_Point lhs, SDL_Point rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}
}

AnimationRuntime::AnimationRuntime(Asset* self, Assets* assets)
    : self_(self), assets_owner_(assets), grid_service_(&vibble::grid::global_grid()) {}

void AnimationRuntime::update() {
    if (!self_ || !self_->info || !planner_iface_) {
        return;
    }

    // Consume any input signal (planner sets this when controllers interacted)
    const bool got_input = planner_iface_->consume_input_event();

    const bool has_plan = !planner_iface_->plan_.strides.empty();
    const bool plan_deferred = has_plan &&
                               should_defer_for_non_locked(planner_iface_->plan_.override_non_locked);

    // Follow path plan if present
    if (has_plan && !plan_deferred &&
        player_.tick(*this, planner_iface_->plan_, stride_index_, stride_frame_counter_)) {
        just_applied_controller_move_ = false;
        return;
    }

    // If controller issued a direct move request, apply it now
    if (planner_iface_->has_pending_move()) {
        const auto& req = planner_iface_->pending_move_;
        if (!should_defer_for_non_locked(req.override_non_locked)) {
            apply_pending_move();
            just_applied_controller_move_ = true;
            return;
        }
    }

    // If no new input after a controller move, redirect immediately to on-end/default
    if (!got_input && just_applied_controller_move_) {
        auto it = self_->info->animations.find(self_->current_animation);
        if (it != self_->info->animations.end()) {
            Animation& anim = it->second;
            if (!anim.locked) {
                const std::string next_id = anim.on_end_animation.empty()
                                              ? std::string{ animation_update::detail::kDefaultAnimation }
                                              : anim.on_end_animation;
                switch_to(resolve_animation(*self_, next_id), path_index_for(next_id));
            }
        }
        just_applied_controller_move_ = false;
    }

    // Keep animations alive when idle
    if (self_->get_current_animation() != animation_update::detail::kDefaultAnimation) {
        if (!advance(self_->current_frame)) {
            switch_to(animation_update::detail::kDefaultAnimation);
            advance(self_->current_frame);
        }
        return;
    }

    advance(self_->current_frame);
}

void AnimationRuntime::apply_pending_move() {
    if (!planner_iface_ || !self_) return;

    const auto req = planner_iface_->consume_move_request();
    const int  resolution = effective_grid_resolution(std::nullopt);
    const SDL_Point from{ self_->pos.x, self_->pos.y };
    SDL_Point world_delta = convert_delta_to_world(req.delta, resolution);
    const SDL_Point to{ from.x + world_delta.x, from.y + world_delta.y };

    SDL_Point final_position = from;
    if (world_delta.x != 0 || world_delta.y != 0) {
        if (!path_blocked(from, to, self_, nullptr)) {
            final_position = to;
        } else {
            const int steps = std::max(std::abs(world_delta.x), std::abs(world_delta.y));
            if (steps > 0) {
                const double step_x = static_cast<double>(world_delta.x) / static_cast<double>(steps);
                const double step_y = static_cast<double>(world_delta.y) / static_cast<double>(steps);
                double       accum_x = static_cast<double>(from.x);
                double       accum_y = static_cast<double>(from.y);
                SDL_Point    current = from;
                for (int i = 0; i < steps; ++i) {
                    accum_x += step_x;
                    accum_y += step_y;
                    SDL_Point candidate{ static_cast<int>(std::round(accum_x)), static_cast<int>(std::round(accum_y)) };
                    if (candidate.x == current.x && candidate.y == current.y) continue;
                    if (path_blocked(current, candidate, self_, nullptr)) break;
                    final_position = candidate;
                    current        = candidate;
                }
            }
        }
    }

    if (final_position.x != self_->pos.x || final_position.y != self_->pos.y) {
        self_->pos = final_position;
        if (req.resort_z) {
            refresh_z_index();
        }
    }

    // Reflect new position as the destination for planners
    planner_iface_->final_dest = self_->pos;

    const std::string resolved = resolve_animation(*self_, req.animation_id);
    if (self_->current_animation != resolved) {
        switch_to(resolved, path_index_for(resolved));
    } else {
        if (!advance(self_->current_frame)) {
            switch_to(resolved, path_index_for(resolved));
        }
    }
}

bool AnimationRuntime::advance(AnimationFrame*& frame) {
    if (!self_ || !self_->info) {
        destroy_child_assets();
        return false;
    }

    auto it = self_->info->animations.find(self_->current_animation);
    if (it == self_->info->animations.end()) {
        destroy_child_assets();
        return false;
    }

    Animation& anim = it->second;
    std::size_t path_index = path_index_for(self_->current_animation);
    if (!frame) {
        frame = anim.get_first_frame(path_index);
        if (!frame) {
            destroy_child_assets();
            return false;
        }
    }

    // If the animation is locked, frozen, or the asset is flagged static, do not advance frames.
    // Exception: the player asset should always advance.
    // Exception: non-player assets with an active plan may override locked animations for auto-movement.
    const bool is_player = self_->info && self_->info->type == asset_types::player;
    bool should_skip = !is_player && (self_->static_frame || anim.locked || anim.is_frozen());
    bool has_overriding_plan = planner_iface_ && !planner_iface_->plan_.strides.empty() && planner_iface_->plan_.override_non_locked;
    if (should_skip && !has_overriding_plan) {
        self_->static_frame = self_->static_frame || anim.is_frozen() || anim.locked;
        update_child_attachments(anim, 0.0f);
        return true;
    }
    if (is_player) {
        // Ensure player is never considered static
        self_->static_frame = false;
    }

    // Time-based frame advance to respect desired playback FPS.
    int target_fps = anim.playback_fps;
    if (target_fps <= 0) target_fps = 24; // sane default
    const float frame_interval = 1.0f / static_cast<float>(target_fps);
    float dt = 0.0f;
    if (assets_owner_) {
        dt = assets_owner_->frame_delta_seconds();
    }
    if (!(dt > 0.0f)) {
        dt = 1.0f / 60.0f; // fallback to 60Hz
    }

    self_->frame_progress += dt;
    bool advanced_any = false;
    while (self_->frame_progress >= frame_interval) {
        self_->frame_progress -= frame_interval;
        if (frame->next) {
            frame = frame->next;
            advanced_any = true;
        } else {
            const bool force_loop_default = self_->current_animation == animation_update::detail::kDefaultAnimation;
            if (anim.loop || force_loop_default) {
                frame = anim.get_first_frame(path_index);
                advanced_any = true;
            } else {
                // Reached end of non-looping animation
                update_child_attachments(anim, dt);
                return false;
            }
        }
    }
    if (advanced_any) {
        self_->mark_composite_dirty();
    }
    update_child_attachments(anim, dt);
    return advanced_any || true;
}

void AnimationRuntime::switch_to(const std::string& anim_id, std::size_t path_index) {
    if (!self_ || !self_->info) {
        return;
    }

    const bool animation_changed = self_->current_animation != anim_id;
    if (animation_changed) {
        destroy_child_assets();
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
    self_->current_frame     = new_frame;
    {
        const bool is_player = self_->info && self_->info->type == asset_types::player;
        self_->static_frame  = is_player ? false : (anim.is_frozen() || anim.locked);
    }
    self_->frame_progress    = 0.0f;
    active_paths_[self_->current_animation] = path_index;
    self_->mark_composite_dirty();
    ensure_child_slots(anim);
    apply_child_frame_data(self_->current_frame);
}

bool AnimationRuntime::should_defer_for_non_locked(bool override_non_locked) const {
    if (override_non_locked || !self_ || !self_->info) {
        return false;
    }

    auto it = self_->info->animations.find(self_->current_animation);
    if (it == self_->info->animations.end()) {
        return false;
    }

    if (self_->current_animation == animation_update::detail::kDefaultAnimation) {
        return false;
    }

    const Animation& anim = it->second;
    return !anim.locked;
}

std::size_t AnimationRuntime::path_index_for(const std::string& anim_id) const {
    auto it = active_paths_.find(anim_id);
    if (it != active_paths_.end()) {
        return it->second;
    }
    return 0;
}

void AnimationRuntime::reset_plan_progress() {
    stride_index_ = 0;
    stride_frame_counter_ = 0;
    next_checkpoint_index_ = 0;
}

void AnimationRuntime::update_child_attachments(Animation& anim, float dt) {
    if (!self_) {
        return;
    }
    if (!anim.has_child_assets()) {
        destroy_child_assets();
        sync_child_assets();
        return;
    }
    ensure_child_slots(anim);
    if (self_->animation_children_.empty()) {
        return;
    }
    advance_child_frames(dt);
    apply_child_frame_data(self_->current_frame);
}

void AnimationRuntime::ensure_child_slots(Animation& anim) {
    if (!self_) {
        return;
    }
    auto& slots = self_->animation_children_;
    const auto& requested = anim.child_assets();
    if (slots.size() < requested.size()) {
        slots.resize(requested.size());
    }

    AssetLibrary* library = assets_owner_ ? &assets_owner_->library() : nullptr;

    bool any_slot_changed = false;
    for (std::size_t i = 0; i < requested.size(); ++i) {
        auto& slot = slots[i];
        const AnimationFrame* previous_frame = slot.current_frame;
        const bool index_changed = slot.child_index != static_cast<int>(i);
        const bool name_changed = slot.asset_name != requested[i];
        if (index_changed || name_changed) {
            if (slot.spawned_asset) {
                slot.spawned_asset->set_hidden(true);
            }
            if (name_changed) {
                slot.spawned_asset = nullptr;
                slot.info.reset();
                slot.animation = nullptr;
                slot.current_frame = nullptr;
            }
            slot.child_index = static_cast<int>(i);
            slot.asset_name = requested[i];
            slot.frame_progress = 0.0f;
            slot.cached_w = 0;
            slot.cached_h = 0;
            slot.was_visible = false;
            slot.last_parent_frame_index = -1;
            slot.visible = false;
            any_slot_changed = true;
        }
        if (!slot.info && library && !slot.asset_name.empty()) {
            slot.info = library->get(slot.asset_name);
        }
        if (!slot.animation && slot.info) {
            auto child_anim_it =
                slot.info->animations.find(animation_update::detail::kDefaultAnimation);
            if (child_anim_it == slot.info->animations.end() && !slot.info->animations.empty()) {
                child_anim_it = slot.info->animations.begin();
            }
            if (child_anim_it != slot.info->animations.end()) {
                slot.animation = &child_anim_it->second;
                slot.current_frame = nullptr;
                slot.frame_progress = 0.0f;
                slot.cached_w = 0;
                slot.cached_h = 0;
                slot.was_visible = false;
                slot.last_parent_frame_index = -1;
            } else {
            }
        }
        if (slot.animation && !slot.current_frame) {
            animation_update::child_attachments::restart(slot);
        }
        if (!slot.spawned_asset && slot.info && slot.visible) {
            slot.spawned_asset = spawn_child_asset(slot);
        }
        if (slot.current_frame != previous_frame) {
            animation_update::child_attachments::update_dimensions(slot);
        }
    }

    // Force update child visibility if any slot changed
    if (any_slot_changed && self_) {
        animation_update::child_attachments::ParentState parent_state;
        SDL_Point render_pos{ static_cast<int>(std::lround(self_->smoothed_translation_x())),
                              static_cast<int>(std::lround(self_->smoothed_translation_y())) };
        parent_state.position = render_pos;
        parent_state.base_position = animation_update::detail::bottom_middle_for(*self_, render_pos);
        parent_state.flipped = self_->flipped;
        parent_state.animation_id = self_->current_animation;
        animation_update::child_attachments::apply_frame_data(self_->animation_children_, parent_state, self_->current_frame);
    }

    for (std::size_t i = requested.size(); i < slots.size(); ++i) {
        auto& slot = slots[i];
        slot.child_index = -1;
        slot.visible = false;
        slot.was_visible = false;
        slot.last_parent_frame_index = -1;
        if (slot.spawned_asset) {
            slot.spawned_asset->set_hidden(true);
        }
    }
}

void AnimationRuntime::advance_child_frames(float dt) {
    if (!self_ || self_->animation_children_.empty()) {
        return;
    }
    animation_update::child_attachments::ParentState parent_state;
    SDL_Point render_pos{ static_cast<int>(std::lround(self_->smoothed_translation_x())),
                          static_cast<int>(std::lround(self_->smoothed_translation_y())) };
    parent_state.position = render_pos;
    parent_state.base_position = animation_update::detail::bottom_middle_for(*self_, render_pos);
    parent_state.flipped = self_->flipped;
    parent_state.animation_id = self_->current_animation;
    animation_update::child_attachments::advance_frames(self_->animation_children_, parent_state, dt);
}

void AnimationRuntime::apply_child_frame_data(const AnimationFrame* frame) {
    if (!self_ || self_->animation_children_.empty()) {
        return;
    }
    animation_update::child_attachments::ParentState parent_state;
    SDL_Point render_pos{ static_cast<int>(std::lround(self_->smoothed_translation_x())),
                          static_cast<int>(std::lround(self_->smoothed_translation_y())) };
    parent_state.position = render_pos;
    parent_state.base_position = animation_update::detail::bottom_middle_for(*self_, render_pos);
    parent_state.flipped = self_->flipped;
    parent_state.animation_id = self_->current_animation;
    animation_update::child_attachments::apply_frame_data(self_->animation_children_, parent_state, frame);
    sync_child_assets();
}

Asset* AnimationRuntime::spawn_child_asset(Asset::AnimationChildAttachment& slot) {
    if (!assets_owner_ || !self_ || !slot.info) {
        return nullptr;
    }
    if (slot.spawned_asset && slot.spawned_asset->dead) {
        slot.spawned_asset = nullptr;
    }
    if (slot.spawned_asset) {
        return slot.spawned_asset;
    }

    SDL_Point spawn_pos{
        static_cast<int>(std::lround(self_->smoothed_translation_x())),
        static_cast<int>(std::lround(self_->smoothed_translation_y()))
    };
    Asset* child = assets_owner_->spawn_asset(slot.asset_name, spawn_pos);
    if (!child) {
        return nullptr;
    }

    // Attach to parent and inherit core spatial/Z context.
    child->parent = self_;
    child->depth = self_->depth;
    child->grid_resolution = self_->grid_resolution;
    child->set_z_offset(self_->z_offset);
    child->set_z_index();
    if (std::find(self_->asset_children.begin(), self_->asset_children.end(), child) ==
        self_->asset_children.end()) {
        self_->add_child(child);
    }

    slot.spawned_asset = child;
    return child;
}

void AnimationRuntime::destroy_child_assets() {
    if (!self_) {
        return;
    }
    // Do not delete child assets; park them and clear slot state until they are needed again.
    for (auto& slot : self_->animation_children_) {
        slot.child_index = -1;
        slot.visible = false;
        slot.was_visible = false;
        slot.render_in_front = true;
        slot.frame_progress = 0.0f;
        slot.last_parent_frame_index = -1;
        if (slot.animation) {
            slot.current_frame = slot.animation->get_first_frame();
        } else {
            slot.current_frame = nullptr;
        }
        if (slot.spawned_asset) {
            slot.spawned_asset->set_hidden(true);
        }
    }
}

void AnimationRuntime::sync_child_assets() {
    if (!self_) {
        return;
    }
    for (auto& slot : self_->animation_children_) {
        if (slot.child_index < 0) {
            if (slot.spawned_asset) {
                slot.spawned_asset->hidden = true;
                slot.spawned_asset->alpha_smoothing_.target = 0.0f;
            }
            continue;
        }
        Asset* child = slot.spawned_asset;
        if (!child && slot.info && slot.visible) {
            child = spawn_child_asset(slot);
        }
        if (!child) {
            continue;
        }
        if (child->dead) {
            slot.spawned_asset = nullptr;
            continue;
        }

        if (!slot.visible) {
            child->hidden = true;
            child->alpha_smoothing_.target = 0.0f;
            continue;
        }

        // Keep attachment in parent's ownership list.
        if (std::find(self_->asset_children.begin(), self_->asset_children.end(), child) ==
            self_->asset_children.end()) {
            self_->add_child(child);
        }

        // slot.world_pos is the child's base (bottom-middle) position in world coordinates.
        // Convert to top-left position for the child asset using its dimensions.
        int child_w = slot.cached_w > 0 ? slot.cached_w : child->cached_w;
        int child_h = slot.cached_h > 0 ? slot.cached_h : child->cached_h;
        SDL_Point child_top_left{
            slot.world_pos.x - child_w / 2,
            slot.world_pos.y - child_h
        };
        child->pos = child_top_left;
        child->grid_resolution = self_->grid_resolution;
        child->depth = self_->depth;
        child->flipped = self_->flipped;
        child->hidden = !slot.visible;
        child->z_offset = self_->z_offset + (slot.render_in_front ? 1 : -1);
        child->set_z_index();
        // Keep child aligned exactly with parent: disable smoothing and snap to the attachment position.
        TransformSmoothingParams snap{};
        snap.method = TransformSmoothingMethod::None;
        snap.snap_threshold = 0.0f;
        child->translation_smoothing_x_.set_params(snap);
        child->translation_smoothing_y_.set_params(snap);
        child->translation_smoothing_x_.reset(static_cast<float>(child->pos.x));
        child->translation_smoothing_y_.reset(static_cast<float>(child->pos.y));
        child->alpha_smoothing_.set_params(snap);
        child->alpha_smoothing_.reset(child->hidden ? 0.0f : 1.0f);
    }
}

SDL_Point AnimationRuntime::bottom_middle(SDL_Point pos) const {
    if (!self_ || !self_->info) {
        return pos;
    }
    return animation_update::detail::bottom_middle_for(*self_, pos);
}

bool AnimationRuntime::point_in_impassable(SDL_Point pt, const Asset* ignored) const {
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

bool AnimationRuntime::path_blocked(SDL_Point from,
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
                const SDL_Point neighbor_bottom = animation_update::detail::bottom_middle_for(*neighbor, neighbor->pos);
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

bool AnimationRuntime::attempt_unstick(SDL_Point from,
                                       SDL_Point to,
                                       const std::vector<const Asset*>& blockers) {
    if (!self_ || !self_->info) {
        return false;
    }
    SDL_Point bottom_from = animation_update::detail::bottom_middle_for(*self_, from);
    SDL_Point bottom_to   = animation_update::detail::bottom_middle_for(*self_, to);
    SDL_Point push{0, 0};
    std::vector<const Asset*> blocking_neighbors = blockers;
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
                    const SDL_Point neighbor_bottom = animation_update::detail::bottom_middle_for(*neighbor, neighbor->pos);
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
        if (dir.x == 0 && dir.y == 0) return;
        const auto it = std::find_if(dirs.begin(), dirs.end(), [&](const SDL_Point& existing) {
            return existing.x == dir.x && existing.y == dir.y;
        });
        if (it == dirs.end()) dirs.push_back(dir);
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
        const Assets* assets = assets_owner_ ? assets_owner_ : (self_ ? self_->get_assets() : nullptr);
        if (!animation_update::detail::bottom_point_inside_playable_area(assets, bottom)) {
            return true;
        }
        visit_impassable_neighbors(*self_, [&](Asset* neighbor) {
            if (!neighbor || neighbor == self_ || !neighbor->info) return false;
            Area area = neighbor->get_area("impassable");
            if (area.get_points().empty()) area = neighbor->get_area("collision_area");
            if (area.get_points().empty()) return false;
            if (!area.contains_point(bottom)) return false;
            const auto it = std::find(blocking_neighbors.begin(), blocking_neighbors.end(), neighbor);
            if (it == blocking_neighbors.end()) { blocked = true; return true; }
            return false;
        });
        return blocked;
};
    const auto inside_any = [&](SDL_Point bottom) {
        const Assets* assets = assets_owner_ ? assets_owner_ : (self_ ? self_->get_assets() : nullptr);
        if (!animation_update::detail::bottom_point_inside_playable_area(assets, bottom)) {
            return false;
        }
        bool inside = false;
        visit_impassable_neighbors(*self_, [&](Asset* neighbor) {
            if (!neighbor || neighbor == self_ || !neighbor->info) return false;
            Area area = neighbor->get_area("impassable");
            if (area.get_points().empty()) area = neighbor->get_area("collision_area");
            if (area.get_points().empty()) return false;
            if (area.contains_point(bottom)) { inside = true; return true; }
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
            if (next.x == candidate.x && next.y == candidate.y) continue;
            SDL_Point bottom_next = animation_update::detail::bottom_middle_for(*self_, next);
            if (inside_disallowed(bottom_next)) break;
            candidate = next;
            moved = true;
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

void AnimationRuntime::mark_progress_toward_checkpoints() {
    if (!self_ || !self_->info || !planner_iface_) {
        return;
    }
    const int visited_thresh = planner_iface_->visited_thresh_;
    const int visited_sq     = visited_thresh * visited_thresh;
    while (next_checkpoint_index_ < planner_iface_->plan_.sanitized_checkpoints.size()) {
        const SDL_Point target  = planner_iface_->plan_.sanitized_checkpoints[next_checkpoint_index_];
        const int       dist_sq = animation_update::detail::distance_sq(self_->pos, target);
        bool reached = false;
        if (visited_thresh == 0) {
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

bool AnimationRuntime::adjust_next_checkpoint(const std::vector<const Asset*>& blockers) {
    if (!self_ || !self_->info || !planner_iface_) {
        return false;
    }
    mark_progress_toward_checkpoints();
    SDL_Point target = (next_checkpoint_index_ < planner_iface_->plan_.sanitized_checkpoints.size())
                         ? planner_iface_->plan_.sanitized_checkpoints[next_checkpoint_index_]
                         : planner_iface_->final_dest;
    SDL_Point bottom_target = animation_update::detail::bottom_middle_for(*self_, target);
    SDL_Point push{0, 0};
    std::vector<const Asset*> influencing_neighbors;
    auto consider_neighbor = [&](const Asset* neighbor) {
        if (!neighbor || neighbor == self_ || !neighbor->info) return;
        Area area = neighbor->get_area("impassable");
        if (area.get_points().empty()) area = neighbor->get_area("collision_area");
        if (area.get_points().empty()) return;
        bool relevant = area.contains_point(bottom_target) || animation_update::detail::segment_hits_area(self_->pos, target, area);
        if (!relevant) {
            const bool overlap_check = animation_update::detail::should_consider_overlap(*self_, *neighbor);
            if (overlap_check) {
                const SDL_Point neighbor_bottom = animation_update::detail::bottom_middle_for(*neighbor, neighbor->pos);
                relevant = animation_update::detail::distance_sq(bottom_target, neighbor_bottom) < animation_update::detail::kOverlapDistanceSq;
            }
        }
        if (!relevant) return;
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
        if (dir.x == 0 && dir.y == 0) return;
        const auto it = std::find_if(dirs.begin(), dirs.end(), [&](const SDL_Point& existing) {
            return existing.x == dir.x && existing.y == dir.y;
        });
        if (it == dirs.end()) dirs.push_back(dir);
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
    for (std::size_t i = next_checkpoint_index_ + 1; i < planner_iface_->plan_.sanitized_checkpoints.size(); ++i) {
        tail.push_back(planner_iface_->plan_.sanitized_checkpoints[i]);
    }
    if (tail.empty() || !same_point(tail.back(), planner_iface_->final_dest)) {
        tail.push_back(planner_iface_->final_dest);
    }
    auto try_plan_with_targets = [&](const std::vector<SDL_Point>& targets) {
        if (targets.empty()) return false;
        auto sanitized = sanitizer_.sanitize(*self_, targets, planner_iface_->visited_thresh_);
        if (sanitized.empty()) return false;
        Plan new_plan = planner_(*self_, sanitized, planner_iface_->visited_thresh_, grid());
        new_plan.override_non_locked = planner_iface_->plan_.override_non_locked;
        if (new_plan.strides.empty()) return false;
        planner_iface_->plan_ = std::move(new_plan);
        planner_iface_->final_dest = planner_iface_->plan_.final_dest;
        stride_index_ = 0;
        stride_frame_counter_ = 0;
        next_checkpoint_index_ = 0;
        planner_iface_->path_requested = false;
        mark_progress_toward_checkpoints();
        return true;
};
    const int max_steps = 24;
    for (SDL_Point dir : directions) {
        SDL_Point candidate = target;
        for (int step = 0; step < max_steps; ++step) {
            SDL_Point next{ candidate.x + dir.x, candidate.y + dir.y };
            if (same_point(next, candidate)) continue;
            SDL_Point bottom_next = animation_update::detail::bottom_middle_for(*self_, next);
            if (point_in_impassable(bottom_next, self_)) break;
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

bool AnimationRuntime::handle_blocked_path(SDL_Point from,
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

bool AnimationRuntime::replan_to_destination() {
    if (!self_ || !self_->info || !planner_iface_) {
        return false;
    }
    const int visited_sq = planner_iface_->visited_thresh_ * planner_iface_->visited_thresh_;
    if (visited_sq > 0 && animation_update::detail::distance_sq(self_->pos, planner_iface_->final_dest) <= visited_sq) {
        return false;
    }
    mark_progress_toward_checkpoints();
    std::vector<SDL_Point> checkpoints;
    for (std::size_t i = next_checkpoint_index_; i < planner_iface_->plan_.sanitized_checkpoints.size(); ++i) {
        checkpoints.push_back(planner_iface_->plan_.sanitized_checkpoints[i]);
    }
    if (checkpoints.empty() || !same_point(checkpoints.back(), planner_iface_->final_dest)) {
        checkpoints.push_back(planner_iface_->final_dest);
    }
    auto sanitized = sanitizer_.sanitize(*self_, checkpoints, planner_iface_->visited_thresh_);
    if (sanitized.empty()) {
        return false;
    }
    Plan new_plan = planner_(*self_, sanitized, planner_iface_->visited_thresh_, grid());
    new_plan.override_non_locked = planner_iface_->plan_.override_non_locked;
    if (new_plan.strides.empty()) {
        return false;
    }
    planner_iface_->plan_ = std::move(new_plan);
    planner_iface_->final_dest = planner_iface_->plan_.final_dest;
    stride_index_ = 0;
    stride_frame_counter_ = 0;
    planner_iface_->path_requested = false;
    next_checkpoint_index_ = 0;
    mark_progress_toward_checkpoints();
    return true;
}

vibble::grid::Grid& AnimationRuntime::grid() const {
    if (grid_service_) return *grid_service_;
    return vibble::grid::global_grid();
}

int AnimationRuntime::effective_grid_resolution(std::optional<int> override_resolution) const {
    (void)override_resolution;
    // Animation runtime always operates at pixel precision.
    return 0;
}

SDL_Point AnimationRuntime::convert_delta_to_world(SDL_Point delta, int resolution) const {
    (void)resolution;
    return delta;
}

void AnimationRuntime::refresh_z_index() {
    if (self_) {
        self_->set_z_index();
    }
}
