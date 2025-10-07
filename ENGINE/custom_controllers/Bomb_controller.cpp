#include "Bomb_controller.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "custom_controllers/controller_path_utils.hpp"
#include "utils/range_util.hpp"

#include <algorithm>

namespace {

int visit_threshold(const Asset* asset) {
    return std::max(2, controller_paths::default_visit_threshold(asset));
}

} // namespace

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
    current_target_ = nullptr;

    const auto path = controller_paths::idle_path(self_, idle_ratio_);
    self_->anim_->move(path, visit_threshold(self_));
}

void BombController::enter_pursue(Asset* target) {
    if (!self_ || !self_->anim_) {
        return;
    }
    if (!target || state_ == State::Detonating) {
        enter_idle(idle_ratio_);
        return;
    }

    state_ = State::Pursuing;
    current_target_ = target;

    const auto path = controller_paths::pursue_path(self_, target);
    self_->anim_->move(path, visit_threshold(self_));
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
    self_->anim_->move({}, visit_threshold(self_));
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
