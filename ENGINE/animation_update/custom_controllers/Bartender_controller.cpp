#include "Bartender_controller.hpp"

#include "animation_update/custom_controllers/controller_path_utils.hpp"
#include "animation_update/custom_controllers/controller_visit_threshold.hpp"
#include "asset/Asset.hpp"
#include "asset/animation.hpp"
#include "asset/asset_info.hpp"
#include "animation_update/animation_update.hpp"
#include "utils/range_util.hpp"
#include <random>
#include <string>

BartenderController::BartenderController(Assets* assets, Asset* self)
    : assets_(assets),
      self_(self),
      rng_(std::random_device{}()),
      idle_range_(15, 45) {}

void BartenderController::init() {
    if (!self_ || !self_->info || !self_->anim_) return;

    const std::string default_anim{ animation_update::detail::kDefaultAnimation };

    auto it = self_->info->animations.find(default_anim);
    if (it != self_->info->animations.end() && !it->second.frames.empty()) {
        self_->anim_->move(SDL_Point{0, 0}, default_anim);
    }
}

void BartenderController::update(const Input& ) {
    if (!self_ || !self_->info || !self_->anim_) return;

    if (!self_->needs_target) {
        return;
    }

    const int rest_ratio = idle_range_(rng_);
    const auto path = controller_paths::idle_path(self_, rest_ratio);
    if (path.empty()) {
        return;
    }

    const int visit_threshold = controller_utils::controller_visit_threshold(self_, path);
    self_->anim_->auto_move(path, visit_threshold);
}
