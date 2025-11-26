#include "Bomb_controller.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "core/AssetsManager.hpp"
#include "animation_update/custom_controllers/controller_path_utils.hpp"
#include "animation_update/custom_controllers/controller_visit_threshold.hpp"
#include "utils/range_util.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

BombController::BombController(Assets* assets, Asset* self)
    : assets_(assets), self_(self) {
}

bool BombController::target_active(Asset* asset) {
    return asset && !asset->dead && asset->active;
}

Asset* BombController::resolve_player_target() const {
    if (!assets_) {
        return nullptr;
    }

    Asset* player = assets_->player;
    if (target_active(player)) {
        return player;
    }

    const auto& active_assets = assets_->getActive();
    if (active_assets.empty()) {
        return nullptr;
    }

    Asset*  closest       = nullptr;
    double  best_distance = std::numeric_limits<double>::infinity();

    for (Asset* candidate : active_assets) {
        if (!target_active(candidate) || !candidate->info) {
            continue;
        }
        const std::string canonical_type = asset_types::canonicalize(candidate->info->type);
        if (canonical_type != asset_types::player) {
            continue;
        }

        const double distance = Range::get_distance(self_, candidate);
        if (!std::isfinite(distance)) {
            continue;
        }
        if (!closest || distance < best_distance) {
            closest       = candidate;
            best_distance = distance;
        }
    }

    return closest;
}

void BombController::enter_pursue(Asset* target) {
    if (!self_ || !self_->anim_) {
        return;
    }
    if (!target) {
        self_->anim_->clear_movement_plan();
        state_ = State::Idle;
        current_target_ = nullptr;
        return;
    }

    state_ = State::Pursuing;
    current_target_ = target;

    const auto path = controller_paths::pursue_path(self_, target);
    self_->anim_->auto_move(path, controller_utils::controller_visit_threshold(self_, path));
}

void BombController::update(const Input&) {
    if (!self_ || !self_->anim_) return;

    // Re-plan current behavior if path execution completed or failed
    if (self_->anim_->path_requested) {
        if (state_ == State::Pursuing && target_active(current_target_)) {
            enter_pursue(current_target_);  // Continue pursuit
        } else {
            self_->anim_->clear_movement_plan();
            state_ = State::Idle;
            current_target_ = nullptr;
        }
    }

    // Evaluate pursuit conditions
    Asset* player = nullptr;
    try {
        player = resolve_player_target();
    } catch (...) {
        player = nullptr;
    }

    if (!target_active(player) || player == self_) {
        if (state_ != State::Idle) {
            self_->anim_->clear_movement_plan();
            state_ = State::Idle;
            current_target_ = nullptr;
        }
        return;
    }

    // Check if target is within neighbor radius
    const int neighbor_radius = controller_paths::neighbor_radius(self_);
    const double distance = Range::get_distance(self_, player);

    if (distance <= static_cast<double>(neighbor_radius)) {
        // Target changed or not yet pursuing - initiate/update pursuit
        if (current_target_ != player || state_ != State::Pursuing) {
            enter_pursue(player);
        }
    } else {
        // Out of range - stop pursuit
        if (state_ != State::Idle) {
            self_->anim_->clear_movement_plan();
            state_ = State::Idle;
            current_target_ = nullptr;
        }
    }
}
