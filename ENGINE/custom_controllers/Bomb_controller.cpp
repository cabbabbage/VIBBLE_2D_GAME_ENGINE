#include "Bomb_controller.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "custom_controllers/controller_path_utils.hpp"
#include "custom_controllers/controller_visit_threshold.hpp"
#include "utils/range_util.hpp"

#include <algorithm>
#include <cmath>

BombController::BombController(Assets* assets, Asset* self)
    : assets_(assets), self_(self) {
    if (self_ && self_->anim_) {
        enter_idle(idle_ratio_);
    }
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
    self_->anim_->move(path, controller_utils::controller_visit_threshold(self_, path));
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
    self_->anim_->move(path, controller_utils::controller_visit_threshold(self_, path));
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
        self_->anim_->set_animation_now("explosion");
        animation_started = true;
    } else {
        const auto it = self_->info->animations.find("explosion");
        if (it != self_->info->animations.end()) {
            self_->anim_->set_animation_now("explosion");
            animation_started = true;
        }
    }

    explosion_started_ = animation_started;
    if (!animation_started) {
        self_->Delete();
        return;
    }
    self_->anim_->move({}, controller_utils::controller_visit_threshold(self_));
}

void BombController::update(const Input&) {
    if (!self_ || !self_->anim_) {
        return;
    }

    if (state_ == State::Detonating) {
        if (explosion_started_ && (!self_->info || self_->get_current_animation() == "explosion")) {
            if (!self_->is_current_animation_looping() && self_->is_current_animation_last_frame()) {
                self_->Delete();
            }
        }
        return;
    }

    auto target_active = [](Asset* asset) {
        return asset && !asset->dead && asset->active;
};

    if (self_->anim_->path_requested) {
        switch (state_) {
        case State::Idle:
            enter_idle(idle_ratio_);
            break;
        case State::Pursuing:
            if (target_active(current_target_)) {
                enter_pursue(current_target_);
            } else {
                pursuit_locked_ = false;
                enter_idle(idle_ratio_);
            }
            break;
        case State::Detonating:
            break;
        }
    }

    if (!assets_ || !self_->info) {
        enter_idle(5);
        return;
    }

    try {
        Asset* player = assets_->player;
        if (!target_active(player) || player == self_) {
            if (!pursuit_locked_) {
                enter_idle(35);
            }
            return;
        }

        const double distance = Range::get_distance(self_, player);
        if (!std::isfinite(distance)) {
            if (!pursuit_locked_) {
                enter_idle(idle_ratio_);
            }
            return;
        }

        constexpr double detection_radius = 800.0;
        constexpr double detonation_radius = 30.0;

        if (distance <= detonation_radius) {
            trigger_explosion();
            return;
        }

        if (!pursuit_locked_ && distance <= detection_radius) {
            pursuit_locked_ = true;
        }

        if (pursuit_locked_) {
            enter_pursue(player);
            return;
        }

        if (state_ != State::Idle) {
            enter_idle(idle_ratio_);
        }
    } catch (...) {
        enter_idle(5);
    }
}
