#pragma once

#include <memory>
#include <string>
class AssetInfo;
class Area;
class nlohmann_json_fwd;

class AreaLoader {
public:
    static void load_collision_areas(AssetInfo& info,
                                     const nlohmann::json& data,
                                     const std::string& dir_path,
                                     int offset_x,
                                     int offset_y);
    static void try_load_area(const nlohmann::json& data,
                              const std::string& key,
                              const std::string& dir,
                              std::unique_ptr<Area>& area_ref,
                              bool& flag_ref,
                              float scale,
                              int offset_x = 0,
                              int offset_y = 0,
                              const std::string& name_hint = "");
};

