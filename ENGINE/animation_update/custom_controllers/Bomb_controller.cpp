#include "Bomb_controller.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"

BombController::BombController(Assets* assets, Asset* self)
    : assets_(assets), self_(self) {
    if (self_ && self_->anim_) {
        self_->anim_->set_debug_enabled(false);
        self_->needs_target = true;
    }
}

void BombController::update(const Input&) {
    if (!self_ || !self_->anim_ || !assets_) {
        return;
    }
    Asset* player = assets_->player;

    if (!player || player == self_ || player->dead || !player->active) {
        return;
    }

    int distance_sq = (self_->pos.x - player->pos.x) * (self_->pos.x - player->pos.x) + (self_->pos.y - player->pos.y) * (self_->pos.y - player->pos.y);

    if (distance_sq <= 700) {
        if (self_->info && self_->info->animations.count("explosion")) {
            self_->anim_->set_animation("explosion");
        }
    }
    else if (self_->needs_target) {
        self_->anim_->auto_move(player);
    }
}
