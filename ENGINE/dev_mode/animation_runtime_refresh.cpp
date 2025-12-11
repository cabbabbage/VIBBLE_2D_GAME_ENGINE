#include "dev_mode/animation_runtime_refresh.hpp"

#include <unordered_set>
#include <string>

#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "core/AssetsManager.hpp"

namespace devmode {

void refresh_loaded_animation_instances(Assets* assets,
                                        const std::shared_ptr<AssetInfo>& info) {
    if (!assets || !info) {
        return;
    }

    std::unordered_set<Asset*> visited;
    auto refresh = [&](Asset* asset) {
        if (!asset || asset->info.get() != info.get()) {
            return;
        }
        if (!visited.insert(asset).second) {
            return;
        }

        asset->rebuild_animation_runtime();
        asset->deactivate();
        asset->current_frame = nullptr;
        asset->set_frame_progress(0.0f);
        asset->static_frame = false;

        std::string desired = asset->current_animation.empty()
                                   ? std::string{"default"}
                                   : asset->current_animation;
        auto it = info->animations.find(desired);
        if (it == info->animations.end()) {
            it = info->animations.find("default");
        }
        if (it == info->animations.end() && !info->animations.empty()) {
            it = info->animations.begin();
        }

        if (it != info->animations.end()) {
            auto& anim = it->second;
            asset->current_animation = it->first;
            asset->current_frame = anim.get_first_frame();
            asset->static_frame = anim.is_frozen() || anim.locked;
        } else {
            asset->current_animation.clear();
            asset->current_frame = nullptr;
        }

        asset->on_scale_factor_changed();
};

    for (Asset* asset : assets->all) {
        refresh(asset);
    }

    assets->mark_active_assets_dirty();
}

}
