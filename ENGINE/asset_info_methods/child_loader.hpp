#pragma once

#include <string>
class AssetInfo;
class nlohmann_json_fwd;

class ChildLoader {
public:
    static void load_children(AssetInfo& info,
                              const nlohmann::json& data,
                              const std::string& dir_path);
};

