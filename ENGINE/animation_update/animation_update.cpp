#include "animation_update.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "asset/Asset.hpp"
#include "asset/animation.hpp"
#include "asset/asset_info.hpp"
#include "animation_update_utils.hpp"
#include "core/AssetsManager.hpp"
#include "util/grid.hpp"
#include "animation_runtime.hpp"

AnimationUpdate::AnimationUpdate(Asset* self, Assets* assets)
    : self_(self), assets_owner_(assets), grid_service_(&vibble::grid::global_grid()) {
    if (!assets_owner_ && self_) {
        assets_owner_ = self_->get_assets();
    }
}

AnimationUpdate::AnimationUpdate(Asset* self, Assets* assets, double)
    : AnimationUpdate(self, assets) {}

void AnimationUpdate::auto_move(const std::vector<SDL_Point>& rel_checkpoints,
                                int visited_thresh_px,
                                std::optional<int> checkpoint_resolution,
                                bool override_non_locked) {
    if (!self_) {
        return;
    }
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

    plan_      = planner_(*self_, sanitizer_.sanitize(*self_, absolute, visited_thresh_), visited_thresh_);
    final_dest = plan_.final_dest;
    plan_.override_non_locked = override_non_locked;

    // Signal executor to re-evaluate plan
    input_event_ = true;
}

void AnimationUpdate::move(SDL_Point delta,
                           const std::string& animation,
                           bool               resort_z,
                           bool               override_non_locked) {
    if (!self_ || !self_->info) {
        return;
    }
    // Do not mutate Asset here; store request for executor
    pending_move_.delta        = delta;
    pending_move_.animation_id = animation;
    pending_move_.resort_z     = resort_z;
    pending_move_.override_non_locked = override_non_locked;
    move_pending_              = true;
    input_event_               = true;
}

void AnimationUpdate::clear_movement_plan() {
    plan_.strides.clear();
    plan_.sanitized_checkpoints.clear();
    plan_.final_dest = self_ ? self_->pos : SDL_Point{ 0, 0 };
    plan_.override_non_locked = true;
    final_dest       = plan_.final_dest;
    path_requested   = false;
    input_event_     = true;
}

std::size_t AnimationUpdate::path_index_for(const std::string& anim_id) const {
    if (runtime_) {
        return runtime_->path_index_for(anim_id);
    }
    return 0;
}

AnimationUpdate::MoveRequest AnimationUpdate::consume_move_request() {
    move_pending_ = false;
    return pending_move_;
}

bool AnimationUpdate::consume_input_event() {
    const bool had = input_event_;
    input_event_ = false;
    return had;
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

