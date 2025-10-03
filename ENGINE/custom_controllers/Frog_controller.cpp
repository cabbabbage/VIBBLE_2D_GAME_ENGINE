#include "Frog_controller.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "utils/range_util.hpp"
#include <algorithm>
FrogController::FrogController(Assets* assets, Asset* self)
    : assets_(assets), self_(self) {
    if (self_ && self_->anim_) {
        self_->anim_->set_idle(idle_ratio_);
        state_ = State::Idle;
        last_run_target_ = nullptr;
    }
}

void FrogController::enter_idle(int rest_ratio) {
    if (!self_ || !self_->anim_) {
        return;
    }
    const int clamped = std::clamp(rest_ratio, 0, 100);
    if (state_ == State::Idle && idle_ratio_ == clamped) {
        return;
    }
    idle_ratio_ = clamped;
    state_ = State::Idle;
    last_run_target_ = nullptr;
    self_->anim_->set_idle(clamped);
}

void FrogController::enter_run(Asset* threat) {
    if (!self_ || !self_->anim_) {
        return;
    }
    if (state_ == State::Running && last_run_target_ == threat) {
        return;
    }
    state_ = State::Running;
    last_run_target_ = threat;
    self_->anim_->set_run(threat);
}

void FrogController::update(const Input&) {
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
            enter_idle(35);
            return;
        }

        const double distance = Range::get_distance(self_, player);
        constexpr double flee_trigger = 20.0;

        if (distance <= flee_trigger) {
            enter_run(player);
        } else {
            enter_idle(35);
        }
    } catch (...) {
        enter_idle(35);
    }
}
