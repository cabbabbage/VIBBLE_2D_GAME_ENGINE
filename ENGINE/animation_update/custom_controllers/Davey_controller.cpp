#include "Davey_controller.hpp"
#include "animation_update/custom_controllers/controller_path_utils.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"

DaveyController::DaveyController(Assets* assets, Asset* self)
    : assets_(assets), self_(self) {
    if (self_ && self_->anim_) {
        self_->anim_->set_debug_enabled(false);
        self_->needs_target = true;
    }
}

void DaveyController::update(const Input&) {
    if (!self_ || !self_->anim_ || !assets_) {
        return;
    }

    Asset* player = assets_->player;
    if (!player || player == self_ || player->dead || !player->active) {
        return;
    }

    self_->anim_->auto_move_to(controller_paths::engagement_point(self_, player, 28, 18, 48));
}
