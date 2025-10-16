#include "default_controller.hpp"
#include "asset/Asset.hpp"
#include "asset/animation.hpp"
#include "asset/asset_info.hpp"
#include "animation_update/animation_update_utils.hpp"

#include <string>

DefaultController::DefaultController(Asset* self)
    : self_(self) {}

void DefaultController::update(const Input& ) {
    if (!self_ || !self_->info || !self_->anim_) {
        return;
    }

    self_->anim_->clear_manual_animation();

    const std::string default_anim{ animation_update::detail::kDefaultAnimation };

    auto it = self_->info->animations.find(default_anim);
    if (it == self_->info->animations.end() || it->second.frames.empty()) {
        return;
    }

    if (self_->current_animation != default_anim) {
        self_->anim_->set_animation_now(default_anim);
        return;
    }

}
