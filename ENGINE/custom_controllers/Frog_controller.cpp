#include "Frog_controller.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"

FrogController::FrogController(Asset* self)
    : self_(self) {}

void FrogController::update(const Input&) {
    if (!self_ || !self_->info) {
        return;
    }

    auto pick_default = [&]() -> std::string {
        if (self_->info->animations.count("default")) return "default";
        if (self_->info->animations.count("Default")) return "Default";
        return self_->info->animations.empty() ? std::string()
                                               : self_->info->animations.begin()->first;
    };

    const std::string& cur = self_->get_current_animation();
    if (cur.empty()) {
        std::string chosen = pick_default();
        if (!chosen.empty() && self_->anim_) {
            self_->anim_->set_animation_now(chosen);
        }
    }

    if (self_->anim_) {
        self_->anim_->set_idle(0, 20, 3);
    }
}

