#include "Frog_controller.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "utils/log.hpp"

#include <sstream>
#include <iostream>

FrogController::FrogController(Assets* assets, Asset* self)
    : assets_(assets), self_(self) {
    if (self_ && self_->anim_) {
        self_->anim_->set_debug_enabled(false);
        self_->needs_target = true;
        vibble::log::info("[FrogController] initialized (needs_target=true)");
        std::cout << "[FrogController] initialized (needs_target=true)" << std::endl;
    }
}

void FrogController::update(const Input&) {
    if (!self_ || !self_->anim_ || !assets_) {
        return;
    }

    Asset* player = assets_->player;
    if (!player || player == self_ || player->dead || !player->active) {
        return;
    }

    if (self_->anim_->debug_enabled()) {
        std::ostringstream oss;
        oss << "[FrogController] pursuing player asset="
            << (player->info ? player->info->name : "<unknown>") << " (needs_target=" << (self_->needs_target ? "true" : "false") << ")";
        vibble::log::info(oss.str());
        std::cout << oss.str() << std::endl;
    }

    self_->anim_->auto_move(player);
}
