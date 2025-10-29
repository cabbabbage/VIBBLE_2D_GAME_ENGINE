#include "default_controller.hpp"
#include "asset/Asset.hpp"
#include "asset/animation.hpp"
#include "asset/asset_info.hpp"
#include "animation_update/animation_update.hpp"

#include <string>

DefaultController::DefaultController(Asset* self)
    : self_(self) {}

void DefaultController::update(const Input& ) {
    if (!self_ || !self_->info || !self_->anim_) {
        return;
    }

    const std::string default_anim{ animation_update::detail::kDefaultAnimation };

    auto it = self_->info->animations.find(default_anim);
    if (it == self_->info->animations.end() || it->second.frames.empty()) {
        return;
    }

    if (self_->current_animation != default_anim || self_->current_frame == nullptr) {
        self_->anim_->move(SDL_Point{ 0, 0 }, default_anim);
        return;
    }

}

