#pragma once

#include <memory>
#include <string>

class Assets;
class AssetInfo;

namespace devmode {

struct AnimationRegenerationResult {
    bool cache_cleared = false;
    bool python_launched = false;
    bool python_success = false;
    bool reloaded = false;
    bool refreshed_instances = false;
};

class AnimationRegenerator {
public:
    // Regenerate a single animation for the given asset and refresh any loaded instances.
    static AnimationRegenerationResult regenerate_animation(Assets* assets,
                                                            const std::shared_ptr<AssetInfo>& info,
                                                            const std::string& animation_id);

private:
    static void refresh_loaded_instances(Assets* assets,
                                         const std::shared_ptr<AssetInfo>& info);
};

} // namespace devmode
