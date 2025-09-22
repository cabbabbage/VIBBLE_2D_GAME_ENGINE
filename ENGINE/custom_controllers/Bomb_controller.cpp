#include "Bomb_controller.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "utils/range_util.hpp"

BombController::BombController(Assets* assets, Asset* self)

    : assets_(assets), self_(self) {
    if (self_ && self_->anim_) {
        self_->anim_->set_idle(40, 80, 5);
    }
}

void BombController::update(const Input&) {
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

        constexpr int detonation_radius = 54;
        constexpr int charge_min_dist   = 24;
        constexpr int charge_max_dist   = 48;

        if (distance <= static_cast<double>(detonation_radius)) {
            self_->anim_->set_animation_now("explosion");
            self_->anim_->set_mode_none();
            return;
        }

        self_->anim_->set_weights(1.0, 0.0);
        self_->anim_->set_pursue(player, charge_min_dist, charge_max_dist);
    } catch (...) {
        self_->anim_->set_idle(40, 80, 5);
    }
}
