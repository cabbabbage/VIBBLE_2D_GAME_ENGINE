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

BombController::BombController(Assets* assets, Asset* self)
    : assets_(assets), self_(self) {
    if (self_ && self_->anim_) {
        enter_idle(idle_ratio_);
    }
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

void BombController::enter_idle(int rest_ratio) {
    if (!self_ || !self_->anim_) {
        return;
    }
    if (state_ == State::Detonating) {
        return;
    }

    idle_ratio_ = std::clamp(rest_ratio, 0, 100);
    state_       = State::Idle;
    current_target_  = nullptr;
    pursuit_locked_  = false;

    const auto path = controller_paths::idle_path(self_, idle_ratio_);
    self_->anim_->auto_move(path, controller_utils::controller_visit_threshold(self_, path));
}

void BombController::enter_pursue(Asset* target) {
    if (!self_ || !self_->anim_) {
        return;
    }
    if (!target || state_ == State::Detonating) {
        enter_idle(idle_ratio_);
        return;
    }

    state_          = State::Pursuing;
    current_target_ = target;
    pursuit_locked_ = true;

    const auto path = controller_paths::pursue_path(self_, target);
    self_->anim_->auto_move(path, controller_utils::controller_visit_threshold(self_, path));
}

void BombController::trigger_explosion() {
    if (!self_ || !self_->anim_) {
        return;
    }
    if (state_ == State::Detonating) {
        return;
    }

    state_           = State::Detonating;
    current_target_  = nullptr;
    pursuit_locked_  = false;

    bool animation_started = false;
    if (!self_->info) {
        self_->anim_->move(SDL_Point{ 0, 0 }, "explosion");
        animation_started = true;
    } else {
        const auto it = self_->info->animations.find("explosion");
        if (it != self_->info->animations.end()) {
            self_->anim_->move(SDL_Point{ 0, 0 }, "explosion");
            animation_started = true;
        }
    }

    explosion_started_ = animation_started;
    if (!animation_started) {
        self_->Delete();
        return;
    }
    self_->anim_->auto_move({}, controller_utils::controller_visit_threshold(self_));
}

void BombController::update(const Input&) {
    if (!self_ || !self_->anim_) {
        return;
    }

    // Handle detonation lifecycle first
    if (state_ == State::Detonating) {
        if (explosion_started_ && (!self_->info || self_->get_current_animation() == "explosion")) {
            if (!self_->is_current_animation_looping() && self_->is_current_animation_last_frame()) {
                self_->Delete();
            }
        }
        return;
    }

    if (!assets_ || !self_->info) {
        // Fallback idle when missing context
        if (self_->anim_->path_requested) {
            enter_idle(5);
        }
        return;
    }

    try {
        Asset* player = resolve_player_target();
        if (!target_active(player) || player == self_) {
            pursuit_locked_ = false;
            if (self_->anim_->path_requested) {
                enter_idle(35);
            }
            return;
        }

        const double distance = Range::get_distance(self_, player);
        if (!std::isfinite(distance)) {
            pursuit_locked_ = false;
            if (self_->anim_->path_requested) {
                enter_idle(idle_ratio_);
            }
            return;
        }

        constexpr double detection_radius  = 800.0;
        constexpr double detonation_radius = 30.0;

        if (distance <= detonation_radius) {
            // Detonate immediately
            trigger_explosion();
            return;
        }

        if (distance <= detection_radius) {
            pursuit_locked_ = true;
            current_target_ = player;
        } else {
            pursuit_locked_ = false;
            current_target_ = nullptr;
        }

        // Only issue movement when the animation runtime requests a new path
        if (self_->anim_->path_requested) {
            if (pursuit_locked_ && target_active(current_target_)) {
                enter_pursue(current_target_);
            } else {
                enter_idle(idle_ratio_);
            }
        }
    } catch (...) {
        if (self_->anim_->path_requested) {
            enter_idle(5);
        }
    }
}

