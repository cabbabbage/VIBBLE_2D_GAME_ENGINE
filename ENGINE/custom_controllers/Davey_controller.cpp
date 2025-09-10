#include "Davey_controller.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "utils/range_util.hpp"

DaveyController::DaveyController(Assets* assets, Asset* self, ActiveAssetsManager& aam)
    : assets_(assets), self_(self), anim_(self, aam, true) {}

void DaveyController::update(const Input&) {
    if (!self_ || !assets_ || !self_->info) {
        anim_.update();
        return;
    }

    if (Asset* player = assets_->player; player && Range::is_in_range(player, self_, 1000)) {
        anim_.set_pursue(player, 20, 30);
    }

    anim_.update();
}
