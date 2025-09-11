#include "Bomb_controller.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"

BombController::BombController(Assets* assets, Asset* self)
    : assets_(assets), self_(self) {}

void BombController::update(const Input&) {
    if (!self_ || !self_->info) return;

    Asset* player = assets_ ? assets_->player : nullptr;

    // If currently exploding or just triggered, bail early.
    if (self_->get_current_animation() == "explosion" || explosion_if_close(player)) {
        return;
    }

    // Decide behavior for this frame (pursue vs idle).
    if (player && self_->distance_to_player_sq <= static_cast<float>(follow_radius_sq_)) {
        pursue(player);   // only sets mode/target
    } else {
        think_random();   // only sets mode/target
    }
}

void BombController::think_random() {
    if (!self_ || !self_->anim_) return;
    // Idle within a probe radius, sometimes resting on "default".
    self_->anim_->set_idle(0, probe_, 3);
}

void BombController::pursue(Asset* player) {
    if (!self_ || !player || !self_->anim_) return;
    // Keep a small standoff distance so the pathing target isn't right on top of player.
    self_->anim_->set_pursue(player, 20, 30);
}

bool BombController::explosion_if_close(Asset* player) {
    if (!self_ || !player || !self_->anim_) return false;

    // Already handled upstream, but keep the guard in case of direct calls.
    if (self_->get_current_animation() == "explosion") {
        return false;
    }

    const float d_sq = self_->distance_to_player_sq;
    if (d_sq <= static_cast<float>(explosion_radius_sq_)) {
        self_->anim_->set_animation_now("explosion");
        return true;
    }

    return false;
}
