#pragma once

#include <nlohmann/json.hpp>
class AssetInfo;

class AreaLoader {

	public:
    static void load(AssetInfo& info,
                     const nlohmann::json& data,
                     float scale,
                     int offset_x,
                     int offset_y);
};
