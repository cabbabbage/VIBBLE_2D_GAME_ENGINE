#pragma once

#include <memory>

class Assets;
class AssetInfo;

namespace devmode {

// Refreshes all loaded Asset instances that share the provided AssetInfo so
// new animation textures take effect immediately.
void refresh_loaded_animation_instances(Assets* assets, const std::shared_ptr<AssetInfo>& info);

} // namespace devmode
