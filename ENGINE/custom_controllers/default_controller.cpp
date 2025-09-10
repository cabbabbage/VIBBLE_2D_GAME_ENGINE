#include "default_controller.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"

DefaultController::DefaultController(Asset* self, ActiveAssetsManager& aam)
    : self_(self), anim_(self, aam, true) {}

void DefaultController::update(const Input& /*in*/) {
    if (!self_ || !self_->info) { 
        anim_.update();
        return; 
    }

    // Pick an idle/default animation for safety
    auto pick_default = [&]() -> std::string {
        if (self_->info->animations.count("default")) return "default";
        if (self_->info->animations.count("Default")) return "Default";
        return self_->info->animations.empty() ? std::string()
                                               : self_->info->animations.begin()->first;
    };

    const std::string& cur = self_->get_current_animation();
    if (cur.empty()) {
        std::string chosen = pick_default();
        if (!chosen.empty()) {
            anim_.set_animation_now(chosen);
        }
    }

    // Default controller: just stay idle
    anim_.set_idle(0, 20, 3);
    anim_.update();
}
