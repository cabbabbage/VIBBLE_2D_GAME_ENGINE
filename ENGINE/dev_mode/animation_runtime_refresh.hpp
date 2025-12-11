#pragma once

#include <memory>

class Assets;
class AssetInfo;

namespace devmode {

void refresh_loaded_animation_instances(Assets* assets, const std::shared_ptr<AssetInfo>& info);

}
