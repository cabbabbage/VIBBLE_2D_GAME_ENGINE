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
    if (!self_ || !assets_ || !self_->info) {
        return;
    }

    try {
        Asset* player = assets_->player;
        if (!player || !self_->anim_) return;

        const bool near   = Range::is_in_range(self_, player, 40);
        const bool inView = Range::is_in_range(self_, player, 1000);

        if (near) {
            self_->anim_->set_animation_now("explosion");
            self_->anim_->set_mode_none();
        } else if (inView) {
            self_->anim_->set_pursue(player, 20, 30);
        } else {
            self_->anim_->set_idle(40, 80, 5);
        }
    } catch (...) {

    }
}
