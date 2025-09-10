#include "default_controller.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "animation.hpp"
#include "core/AssetsManager.hpp"
#include "core/active_assets_manager.hpp"

DefaultController::DefaultController(Assets* assets, Asset* self, ActiveAssetsManager& aam)
: assets_(assets), self_(self), aam_(aam), anim_(self, aam, true) {}

DefaultController::~DefaultController() = default;

void DefaultController::update(const Input& /*in*/) {
    anim_.update();
    if (!self_ || !self_->info) { return; }

    auto pick_default = [&]() -> std::string {
        auto it = self_->info->animations.find("default");
        if (it != self_->info->animations.end()) return "default";
        it = self_->info->animations.find("Default");
        if (it != self_->info->animations.end()) return "Default";
        return self_->info->animations.empty() ? std::string()
                                              : self_->info->animations.begin()->first;
    };

    const std::string& cur = self_->get_current_animation();
    if (cur.empty()) {
        if (self_->next_animation.empty()) {
            self_->change_animation_qued(pick_default());
        }
        return;
    }

    // Only intervene when the current animation has finished, no next animation
    // is pending, and there is no specific on_end directive.
    if (!self_->is_current_animation_last_frame() || !self_->next_animation.empty()) {
        return;
    }

    auto it = self_->info->animations.find(cur);
    if (it != self_->info->animations.end()) {
        const Animation& anim = it->second;
        if (!anim.on_end_animation.empty()) {
            return; // AnimationUpdate will handle its on_end logic
        }
    }

    std::string chosen = pick_default();
    if (!chosen.empty() && chosen != cur && self_->next_animation.empty()) {
        self_->change_animation_qued(chosen);
    }
}
