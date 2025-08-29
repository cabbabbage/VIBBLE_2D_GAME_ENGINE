#include "area_loader.hpp"

#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <limits>

using nlohmann::json;
namespace fs = std::filesystem;

void AreaLoader::load_collision_areas(AssetInfo& info,
                                      const json& data,
                                      const std::string& dir_path,
                                      int offset_x,
                                      int offset_y) {
    try_load_area(data, "impassable_area", dir_path, info.passability_area, info.has_passability_area, info.scale_factor, offset_x, offset_y, info.name);
    try_load_area(data, "collision_area", dir_path, info.collision_area, info.has_collision_area, info.scale_factor, offset_x, offset_y, info.name);
    try_load_area(data, "interaction_area", dir_path, info.interaction_area, info.has_interaction_area, info.scale_factor, offset_x, offset_y, info.name);
    try_load_area(data, "hit_area", dir_path, info.attack_area, info.has_attack_area, info.scale_factor, offset_x, offset_y, info.name);
}

void AreaLoader::try_load_area(const json& data,
                               const std::string& key,
                               const std::string& dir,
                               std::unique_ptr<Area>& area_ref,
                               bool& flag_ref,
                               float scale,
                               int offset_x,
                               int offset_y,
                               const std::string& name_hint) {
    bool area_loaded = false;

    if (data.contains(key) && data[key].is_string()) {
        try {
            std::string filename = data[key].get<std::string>();
            std::string path = dir + "/" + filename;
            std::string nm = fs::path(filename).stem().string();

            area_ref = std::make_unique<Area>(nm, path, scale);
            area_ref->apply_offset(offset_x, offset_y);
            flag_ref = true;
            area_loaded = true;
        } catch (const std::exception& e) {
            std::cerr << "[AreaLoader] warning: failed to load area '"
                      << key << "': " << e.what() << std::endl;
        }
    }

    // NEW: support embedded area object in info.json
    if (!area_loaded && data.contains(key) && data[key].is_object()) {
        try {
            const auto& obj = data[key];
            std::vector<Area::Point> pts;
            if (obj.contains("points") && obj["points"].is_array()) {
                for (const auto& p : obj["points"]) {
                    if (p.is_array() && p.size() >= 2) {
                        int x = static_cast<int>(std::round(p[0].get<double>()));
                        int y = static_cast<int>(std::round(p[1].get<double>()));
                        pts.emplace_back(x + offset_x, y + offset_y);
                    }
                }
            }
            if (!pts.empty()) {
                std::string nm = name_hint.empty() ? key : (name_hint + "_" + key);
                area_ref = std::make_unique<Area>(nm, pts);
                flag_ref = true;
                area_loaded = true;
            }
        } catch (const std::exception& e) {
            std::cerr << "[AreaLoader] failed embedded area for '" << key << "': " << e.what() << "\n";
        }
    }

    // No spacing_area fallback: feature removed
}
