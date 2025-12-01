#include "Bomb_controller.hpp"
#include "asset/Asset.hpp"
#include "utils/log.hpp"
#include "core/AssetsManager.hpp"
#include <iostream>
#include <sstream>

BombController::BombController(Assets* assets, Asset* self)
    : assets_(assets), self_(self) {
    if (self_ && self_->anim_) {
        self_->anim_->set_debug_enabled(true);
        self_->needs_target = true; // kick off first pursuit
        vibble::log::info("[BombController] initialized (needs_target=true)");
        std::cout << "[BombController] initialized (needs_target=true)" << std::endl;
    }
}

void BombController::update(const Input&) {
    if (!self_ || !self_->anim_ || !assets_) {
        return;
    }
    Asset* player = assets_->player;
    if (!player || player == self_ || player->dead || !player->active) {
        if (self_->anim_->debug_enabled()) {
            vibble::log::info("[BombController] no valid player target; clearing plan");
        }
        self_->anim_->auto_move(SDL_Point{0, 0});
        return;
    }

    // Always drive pursuit; planner handles pathing and will clear needs_target on success.
    if (self_->anim_->debug_enabled()) {
        vibble::log::info("[BombController] pursuing player via auto_move (needs_target="
                          + std::string(self_->needs_target ? "true" : "false") + ")");
        std::cout << "[BombController] pursuing player via auto_move (needs_target="
                  << (self_->needs_target ? "true" : "false") << ")" << std::endl;
    }
    self_->anim_->auto_move(player);
}
