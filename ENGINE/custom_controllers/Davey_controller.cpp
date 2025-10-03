#include "Davey_controller.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "utils/range_util.hpp"
#include <algorithm>
#include <cmath>
DaveyController::DaveyController(Assets* assets, Asset* self)
    : assets_(assets), self_(self) {
    if (self_ && self_->anim_) {
        self_->anim_->set_path_bias(default_bias_);
        self_->anim_->set_idle(idle_ratio_);
        state_ = State::Idle;
        active_bias_ = default_bias_;
        current_target_ = nullptr;
    }
}

void DaveyController::apply_path_bias(double desired_bias) {
    if (!self_ || !self_->anim_) {
        return;
    }
    const double clamped = std::clamp(desired_bias, 0.0, 1.0);
    if (std::abs(active_bias_ - clamped) < 1e-4) {
        return;
    }
    active_bias_ = clamped;
    self_->anim_->set_path_bias(clamped);
}

void DaveyController::enter_idle(int rest_ratio) {
    if (!self_ || !self_->anim_) {
        return;
    }
    const int clamped = std::clamp(rest_ratio, 0, 100);
    if (state_ == State::Idle && idle_ratio_ == clamped) {
        return;
    }
    idle_ratio_ = clamped;
    state_ = State::Idle;
    current_target_ = nullptr;
    apply_path_bias(default_bias_);
    self_->anim_->set_idle(clamped);
}

void DaveyController::enter_pursue(Asset* target) {
    if (!self_ || !self_->anim_) {
        return;
    }
    if (!target) {
        enter_idle(idle_ratio_);
        return;
    }
    if (state_ == State::Pursuing && current_target_ == target) {
        return;
    }
    state_ = State::Pursuing;
    current_target_ = target;
    apply_path_bias(default_bias_);
    self_->anim_->set_pursue(target);
}

void DaveyController::enter_orbit(Asset* center, int radius) {
    if (!self_ || !self_->anim_) {
        return;
    }
    if (!center) {
        enter_idle(idle_ratio_);
        return;
    }
    if (state_ == State::Orbiting && current_target_ == center) {
        return;
    }
    state_ = State::Orbiting;
    current_target_ = center;
    apply_path_bias(orbit_bias_);
    self_->anim_->set_orbit(center, radius, radius, 1'000'000);
}

void DaveyController::update(const Input&) {
    if (!self_ || !self_->anim_) {
        return;
    }

    if (!assets_ || !self_->info) {
        enter_idle(5);
        return;
    }

    try {
        Asset* player = assets_->player;
        if (!player || player == self_) {
            enter_idle(5);
            return;
        }

        const double distance = Range::get_distance(self_, player);
        constexpr double orbit_radius = 44.0;
        constexpr double chase_trigger = 360.0;

        if (distance <= orbit_radius) {
            enter_orbit(player, static_cast<int>(orbit_radius));
        } else if (distance <= chase_trigger) {
            enter_pursue(player);
        } else {
            enter_idle(10);
        }
    } catch (...) {
        enter_idle(5);
    }
}
