#include "child_loader.hpp"

#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

using nlohmann::json;
namespace fs = std::filesystem;

void ChildLoader::load_children(AssetInfo& info, const json& data, const std::string& dir_path) {
    info.children.clear();
    if (!data.contains("child_assets") || !data["child_assets"].is_array())
        return;

    for (const auto& entry : data["child_assets"]) {
        std::string rel_path;
        if (entry.is_string()) {
            rel_path = entry.get<std::string>();
        }
        else if (entry.is_object() && entry.contains("json_path") && entry["json_path"].is_string()) {
            rel_path = entry["json_path"].get<std::string>();
        } else {
            continue;
        }

        fs::path full_path = fs::path(dir_path) / rel_path;
        if (!fs::exists(full_path)) {
            std::cerr << "[ChildLoader] child JSON not found: " << full_path << "\n";
            continue;
        }

        int z_offset_value = 0;
        try {
            std::ifstream in(full_path);
            json childJson;
            in >> childJson;
            if (childJson.contains("z_offset")) {
                auto& jz = childJson["z_offset"];
                if (jz.is_number()) {
                    z_offset_value = static_cast<int>(jz.get<double>());
                } else if (jz.is_string()) {
                    try { z_offset_value = std::stoi(jz.get<std::string>()); } catch (...) {}
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[ChildLoader] failed to parse z_offset from "
                      << full_path << " | " << e.what() << "\n";
        }

        ChildInfo ci;
        ci.json_path = full_path.string();
        ci.z_offset  = z_offset_value;

        try {
            std::string area_name = fs::path(rel_path).stem().string();
            ci.area_ptr = std::make_unique<Area>(area_name, full_path.string(), info.scale_factor);
            ci.area_ptr->apply_offset(0, 100);
            ci.has_area = true;
        } catch (const std::exception& e) {
            std::cerr << "[ChildLoader] failed to construct area from child JSON: "
                      << full_path << " | " << e.what() << "\n";
            ci.has_area = false;
        }

        info.children.emplace_back(std::move(ci));
    }
}

