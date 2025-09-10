#include "Bomb_controller.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"

BombController::BombController(Assets* assets, Asset* self, ActiveAssetsManager& aam)
    : assets_(assets), self_(self), anim_(self, aam, true) {}

void BombController::update(const Input&) {
    if (!self_ || !self_->info) return;

    Asset* player = assets_ ? assets_->player : nullptr;

    // If currently exploding or just triggered, run animation update and bail.
    if (self_->get_current_animation() == "explosion" || explosion_if_close(player)) {
        anim_.update();
        return;
    }

    // Decide behavior for this frame (pursue vs idle), then run a single animation update.
    if (player && self_->distance_to_player_sq <= static_cast<float>(follow_radius_sq_)) {
        pursue(player);   // only sets mode/target
    } else {
        think_random();   // only sets mode/target
    }

    anim_.update();       // single per-frame step after behavior decision
}

void BombController::think_random() {
    if (!self_) return;
    // Idle within a probe radius, sometimes resting on "default".
    anim_.set_idle(0, probe_, 3);
    // NOTE: no anim_.update() here; the main update() calls it once per frame.
}

void BombController::pursue(Asset* player) {
    if (!self_ || !player) return;
    // Keep a small standoff distance so the pathing target isn't right on top of player.
    anim_.set_pursue(player, 20, 30);
    // NOTE: no anim_.update() here; the main update() calls it once per frame.
}

bool BombController::explosion_if_close(Asset* player) {
    if (!self_ || !player) return false;

    // Already handled upstream, but keep the guard in case of direct calls.
    if (self_->get_current_animation() == "explosion") {
        return false;
    }

    const float d_sq = self_->distance_to_player_sq;
    if (d_sq <= static_cast<float>(explosion_radius_sq_)) {
        anim_.set_animation_now("explosion");
        return true;
    }

    return false;
}
