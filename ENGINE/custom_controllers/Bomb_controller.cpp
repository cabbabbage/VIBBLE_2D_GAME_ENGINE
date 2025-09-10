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

void BombController::update(const Input&) {
    if (!self_ || !self_->info) return;

    // If we are already in (or queued for) explosion, let AnimationUpdate handle it and bail.
    if (self_->get_current_animation() == "explosion" || self_->next_animation == "explosion") {
        anim_.update(); // step explosion animation
        return;
    }

    // Try to explode if the player is close. If triggered, switch immediately (this frame) and return.
    Asset* player = assets_ ? assets_->player : nullptr;
    if (explosion_if_close(player)) {
        return; // anim_.update("explosion") already applied movement this frame
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
    if (self_->get_current_animation() == "explosion" || self_->next_animation == "explosion") {
        return false;
    }

    const float d_sq = self_->distance_to_player_sq;
    if (d_sq <= static_cast<float>(explosion_radius_sq_)) {
        // Immediate switch + movement for this frame using the new AnimationUpdate API.
        anim_.update("explosion");
        return true;
    }

    return false;
}
