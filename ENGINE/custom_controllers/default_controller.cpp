#include "default_controller.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "animation.hpp"
#include "core/AssetsManager.hpp"
#include "core/active_assets_manager.hpp"

DefaultController::DefaultController(Assets* assets, Asset* self, ActiveAssetsManager& aam)
: assets_(assets), self_(self), aam_(aam) {}

DefaultController::~DefaultController() = default;

void DefaultController::update(const Input& /*in*/) {
    if (!self_ || !self_->info) return;

    auto pick_default = [&]() -> std::string {
        auto it = self_->info->animations.find("default");
        if (it != self_->info->animations.end()) return "default";
        it = self_->info->animations.find("Default");
        if (it != self_->info->animations.end()) return "Default";
        return self_->info->animations.empty()
             ? std::string()
             : self_->info->animations.begin()->first;
    };

    std::string chosen;

    
    const std::string& cur = self_->get_current_animation();
    if (!cur.empty()) {
        auto it = self_->info->animations.find(cur);
        if (it != self_->info->animations.end()) {
            const Animation& anim = it->second;
            const std::string& oe = anim.on_end_animation;
            if (!oe.empty()) {
                
                if (oe == "end" || oe == "freeze_on_last" ||
                    self_->info->animations.find(oe) != self_->info->animations.end()) {
                    chosen = oe;
                }
            }
        }
    }

    
    if (chosen.empty()) chosen = pick_default();

    
    self_->next_animation = chosen;
}
