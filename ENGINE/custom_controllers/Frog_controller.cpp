#include "Frog_controller.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "utils/range_util.hpp"


FrogController::FrogController(Assets* assets, Asset* self)
    : assets_(assets), self_(self) {
    if (self_ && self_->anim_) {
        self_->anim_->set_idle(40, 80, 5);
    }
}

void FrogController::update(const Input&) {
    if (!self_ || !self_->anim_) {
        return;
    }

    if (!assets_ || !self_->info) {
        self_->anim_->set_idle(40, 80, 5);
        return;
    }

    try {
        Asset* player = assets_->player;
        if (!player || player == self_) {
            self_->anim_->set_idle(40, 80, 5);
            return;
        }

        const double distance = Range::get_distance(self_, player);

        constexpr int flee_trigger   = 72;
        constexpr int alert_trigger  = 260;
        constexpr int flee_min_range = 96;
        constexpr int flee_max_range = 160;

        if (distance <= static_cast<double>(flee_trigger)) {
            self_->anim_->set_weights(1.0, 0.0);
            self_->anim_->set_run(player, flee_min_range, flee_max_range);
        } else if (distance <= static_cast<double>(alert_trigger)) {
            self_->anim_->set_idle(16, 48, 35);
        } else {
            self_->anim_->set_idle(48, 96, 60);
        }
    } catch (...) {
        self_->anim_->set_idle(40, 80, 5);
    }
}
