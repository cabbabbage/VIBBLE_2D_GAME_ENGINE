#include "Davey_controller.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "utils/range_util.hpp"

DaveyController::DaveyController(Assets* assets, Asset* self)
    : assets_(assets), self_(self) {
    if (self_ && self_->anim_) {
        self_->anim_->set_idle(40, 80, 5);
    }
}

void DaveyController::update(const Input&) {
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

        constexpr int orbit_radius      = 44;
        constexpr int chase_trigger     = 360;
        constexpr int pursue_min_stride = 28;
        constexpr int pursue_max_stride = 56;

        if (distance <= static_cast<double>(orbit_radius)) {
            self_->anim_->set_weights(0.9, 0.1);
            self_->anim_->set_orbit_ccw(player, orbit_radius, orbit_radius);
        } else if (distance <= static_cast<double>(chase_trigger)) {
            self_->anim_->set_weights(1.0, 0.0);
            self_->anim_->set_pursue(player, pursue_min_stride, pursue_max_stride);
        } else {
            self_->anim_->set_idle(40, 90, 10);
        }
    } catch (...) {
        self_->anim_->set_idle(40, 80, 5);
    }
}
