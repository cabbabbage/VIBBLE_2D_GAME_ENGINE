#pragma once

#include <string>
#include <nlohmann/json.hpp>
class AssetInfo;

class ChildLoader {

	public:
    static void load_children(AssetInfo& info, const nlohmann::json& data, const std::string& dir_path);
};
