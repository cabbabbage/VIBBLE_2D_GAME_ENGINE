#include "Davey_controller.hpp"
#include "utils/input.hpp"
#include "asset/Asset.hpp"
#include "asset/animation_update.hpp"
#include "utils/area.hpp"
#include "core/active_assets_manager.hpp"
#include "core/AssetsManager.hpp"
#include "utils/range_util.hpp"
#include <cmath>
#include <iostream>
#include <random>
#include <algorithm>
DaveyController::DaveyController(Assets* assets, Asset* self, ActiveAssetsManager& aam)
: assets_(assets), self_(self), aam_(aam), anim_(self, aam, true) {}

void DaveyController::update(const Input& ) {
        anim_.update();
        if (!self_ || !assets_ || !self_->info) { return; }
        Asset* player = assets_->player;
        if (!player) {
                return;
        }
        if (Range::is_in_range(player, self_, 1000)) {
                anim_.set_pursue(player, 20, 30);
                anim_.update();
        } else {
        }
}
