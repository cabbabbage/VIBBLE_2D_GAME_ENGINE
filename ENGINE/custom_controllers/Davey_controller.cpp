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
    if (!self_ || !assets_ || !self_->info) {
        return;
    }

    try {
        if (Asset* player = assets_->player; player && Range::is_in_range(player, self_, 1000)) {
            if (self_->anim_) self_->anim_->set_pursue(player, 20, 30);
        } else {
            if (self_->anim_) self_->anim_->set_idle(40, 80, 5);
        }
    } catch (...) {
    }
}
