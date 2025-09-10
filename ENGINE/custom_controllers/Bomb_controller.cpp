#include "Bomb_controller.hpp"
#include "utils/input.hpp"
#include "asset/Asset.hpp"
#include "asset/animation_update.hpp"
#include "core/active_assets_manager.hpp"
#include "core/AssetsManager.hpp"
#include <algorithm>
BombController::BombController(Assets* assets, Asset* self, ActiveAssetsManager& aam)
: assets_(assets)
, self_(self)
, aam_(aam)
, anim_(self, aam, true)
{
	rng_seed_ ^= reinterpret_cast<uintptr_t>(self_) + 0x9e3779b9u;
}

BombController::~BombController() {}

void BombController::update(const Input& ) {
        anim_.update();
        if (!self_ || !self_->info) { return; }
        Asset* player = assets_ ? assets_->player : nullptr;
        explosion_if_close(player);
        if (self_->get_current_animation() == "explosion" || self_->next_animation == "explosion") {
                return;
        }
        if (player && self_->distance_to_player_sq <= static_cast<float>(follow_radius_sq_))
                pursue(player);
        else
                think_random();
}

void BombController::think_random() {
        if (!self_) return;
        anim_.set_idle(0, probe_, 3);
        anim_.move();
}

void BombController::pursue(Asset* player) {
        if (!self_ || !player) return;
        anim_.set_pursue(player, 20, 30);
        anim_.move();
}

void BombController::explosion_if_close(Asset* player) {
        if (!self_ || !player) return;
        if (self_->get_current_animation() == "explosion" || self_->next_animation == "explosion") {
                return;
        }
        float d_sq = self_->distance_to_player_sq;
        if (d_sq <= static_cast<float>(explosion_radius_sq_)) {
                self_->change_animation_qued("explosion");
        }
}
