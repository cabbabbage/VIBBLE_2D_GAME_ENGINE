#pragma once

#include <nlohmann/json.hpp>
class AssetInfo;

class LightingLoader {

        public:
    static void load(AssetInfo& info, const nlohmann::json& data);
};
