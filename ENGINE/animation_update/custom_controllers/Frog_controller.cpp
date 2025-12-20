#include "Frog_controller.hpp"

#include "animation_update/custom_controllers/controller_path_utils.hpp"
#include "animation_update/custom_controllers/controller_visit_threshold.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"

FrogController::FrogController(Assets* assets, Asset* self)
    : assets_(assets), self_(self) {
    if (self_ && self_->anim_) {
        self_->anim_->set_debug_enabled(false);
        self_->needs_target = true;
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

    constexpr int kFleeThresholdPx = 64;
    const int distance_sq = (self_->pos.x - player->pos.x) * (self_->pos.x - player->pos.x) +
                            (self_->pos.y - player->pos.y) * (self_->pos.y - player->pos.y);

    if (distance_sq <= (kFleeThresholdPx * kFleeThresholdPx)) {
        if (!self_->needs_target) {
            return;
        }
        const auto path = controller_paths::flee_path(self_, player);
        if (path.empty()) {
            return;
        }
        const int visit_threshold = controller_utils::controller_visit_threshold(self_, path);
        self_->anim_->auto_move(path, visit_threshold);
        return;
    }

    self_->anim_->auto_move(player);
}
