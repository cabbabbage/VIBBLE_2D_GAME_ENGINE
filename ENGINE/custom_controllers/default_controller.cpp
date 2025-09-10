#include "default_controller.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "core/AssetsManager.hpp"
#include "core/active_assets_manager.hpp"

DefaultController::DefaultController(Assets* assets, Asset* self, ActiveAssetsManager& aam)
: assets_(assets), self_(self), aam_(aam), anim_(self, aam, true) {}

DefaultController::~DefaultController() = default;

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
    if (cur.empty() && self_->next_animation.empty()) {
        std::string chosen = pick_default();
        if (!chosen.empty()) {
            anim_.update(chosen); // immediately switch if no animation running
            return;
        }
    }

    // Default controller: just stay idle
    anim_.set_idle(0, 20, 3);  
    anim_.update();
}
