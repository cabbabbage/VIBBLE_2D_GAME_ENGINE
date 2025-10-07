#include "Frog_controller.hpp"
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

FrogController::FrogController(Assets* assets, Asset* self)
    : assets_(assets), self_(self) {
    if (self_ && self_->anim_) {
        enter_idle(idle_ratio_);
    }
}

void FrogController::enter_idle(int rest_ratio) {
    if (!self_ || !self_->anim_) {
        return;
    }

    idle_ratio_ = std::clamp(rest_ratio, 0, 100);
    state_ = State::Idle;
    last_run_target_ = nullptr;

    const auto path = controller_paths::idle_path(self_, idle_ratio_);
    self_->anim_->move(path, visit_threshold(self_));
}

void FrogController::enter_run(Asset* threat) {
    if (!self_ || !self_->anim_) {
        return;
    }

    state_ = State::Running;
    last_run_target_ = threat;

    const auto path = controller_paths::flee_path(self_, threat);
    self_->anim_->move(path, visit_threshold(self_));
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
