#include "Bomb_controller.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "utils/range_util.hpp"
#include <algorithm>

BombController::BombController(Assets* assets, Asset* self)
    : assets_(assets), self_(self) {
    if (self_ && self_->anim_) {
        self_->anim_->set_idle(idle_ratio_);
        state_ = State::Idle;
        current_target_ = nullptr;
    }
}

void BombController::enter_idle(int rest_ratio) {
    if (!self_ || !self_->anim_) {
        return;
    }
    if (state_ == State::Detonating) {
        return;
    }
    const int clamped = std::clamp(rest_ratio, 0, 100);
    if (state_ == State::Idle && idle_ratio_ == clamped) {
        return;
    }
    idle_ratio_ = clamped;
    state_ = State::Idle;
    current_target_ = nullptr;
    self_->anim_->set_idle(clamped);
}

void BombController::enter_pursue(Asset* target) {
    if (!self_ || !self_->anim_) {
        return;
    }
    if (!target || state_ == State::Detonating) {
        enter_idle(idle_ratio_);
        return;
    }
    if (state_ == State::Pursuing && current_target_ == target) {
        return;
    }
    state_ = State::Pursuing;
    current_target_ = target;
    self_->anim_->set_pursue(target);
}

void BombController::trigger_explosion() {
    if (!self_ || !self_->anim_) {
        return;
    }
    if (state_ == State::Detonating) {
        return;
    }
    state_ = State::Detonating;
    current_target_ = nullptr;
    self_->anim_->set_animation_now("explosion");
    self_->anim_->set_mode_none();
}

void BombController::update(const Input&) {
    if (!self_ || !self_->anim_) {
        return;
    }

    if (state_ == State::Detonating) {
        return;
    }

    if (!assets_ || !self_->info) {
        enter_idle(5);
        return;
    }

    try {
        Asset* player = assets_->player;
        if (!player || player == self_) {
            enter_idle(35);
            return;
        }

        const double distance = Range::get_distance(self_, player);
        constexpr double detonation_radius = 54.0;

        if (distance <= detonation_radius) {
            trigger_explosion();
            return;
        }

        enter_pursue(player);
    } catch (...) {
        enter_idle(5);
    }
}
