#include "asset_spawn_planner.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <random>
#include <algorithm>

namespace fs = std::filesystem;

AssetSpawnPlanner::AssetSpawnPlanner(const std::vector<nlohmann::json>& json_sources,
                                     double area,
                                     AssetLibrary& asset_library)
    : asset_library_(&asset_library) {

    nlohmann::json merged;
    merged["assets"] = nlohmann::json::array();
    merged["batch_assets"]["batch_assets"] = nlohmann::json::array();

    for (const auto& json : json_sources) {
        if (json.contains("assets") && json["assets"].is_array()) {
            for (const auto& asset : json["assets"]) {
                merged["assets"].push_back(asset);
            }
        }
        if (json.contains("batch_assets") && json["batch_assets"].contains("batch_assets")) {
            const auto& b = json["batch_assets"];
            merged["batch_assets"]["has_batch_assets"] = true;
            merged["batch_assets"]["grid_spacing_min"] = b.value("grid_spacing_min", 100);
            merged["batch_assets"]["grid_spacing_max"] = b.value("grid_spacing_max", 100);
            merged["batch_assets"]["jitter_min"] = b.value("jitter_min", 0);
            merged["batch_assets"]["jitter_max"] = b.value("jitter_max", 0);
            for (const auto& ba : b["batch_assets"]) {
                merged["batch_assets"]["batch_assets"].push_back(ba);
            }
        }
    }

    root_json_ = merged;
    parse_asset_spawns(area);
    parse_batch_assets();
    sort_spawn_queue();
}

const std::vector<SpawnInfo>& AssetSpawnPlanner::get_spawn_queue() const {
    return spawn_queue_;
}

const std::vector<BatchSpawnInfo>& AssetSpawnPlanner::get_batch_spawn_assets() const {
    return batch_spawn_assets_;
}

void AssetSpawnPlanner::parse_asset_spawns(double area) {
    std::mt19937 rng(std::random_device{}());

    if (!root_json_.contains("assets")) return;

    for (const auto& entry : root_json_["assets"]) {
        nlohmann::json asset = entry;

        if (!asset.contains("name") || !asset["name"].is_string()) {
            continue;
        }

        std::string name = asset["name"];
        auto info = asset_library_->get(name);

        if (!info) {
            try {
                asset["tag"] = name;
                asset = resolve_asset_from_tag(asset);
                name = asset["name"];
                info = asset_library_->get(name);
            } catch (const std::exception& e) {
                continue;
            }
        }

        if (!info) {
            continue;
        }

        int min_num = asset.value("min_number", 1);
        int max_num = asset.value("max_number", min_num);
        int quantity = std::uniform_int_distribution<int>(min_num, max_num)(rng);

        std::string position = asset.value("position", "Random");
        bool isSingleCenter = (min_num == 1 && max_num == 1 &&
                            (position == "Center" || position == "center"));
        bool isPerimeter    = (position == "Perimeter" || position == "perimeter");

        
         
        
        

        SpawnInfo s;
        s.name = name;
        s.position = position;
        s.quantity = quantity;
        s.check_overlap = asset.value("check_overlap", false);
        s.check_min_spacing = asset.value("check_min_spacing", false);

        auto get_val = [&](const std::string& kmin, const std::string& kmax, int def = 0) -> int {
            int vmin = asset.value(kmin, def);
            int vmax = asset.value(kmax, def);
            return (vmin + vmax) / 2;
        };

        s.grid_spacing = get_val("grid_spacing_min", "grid_spacing_max");
        s.jitter = get_val("jitter_min", "jitter_max");
        s.empty_grid_spaces = get_val("empty_grid_spaces_min", "empty_grid_spaces_max");
        s.ep_x = get_val("ep_x_min", "ep_x_max", -1);
        s.ep_y = get_val("ep_y_min", "ep_y_max", -1);
        s.border_shift = get_val("border_shift_min", "border_shift_max");
        s.sector_center = get_val("sector_center_min", "sector_center_max");
        s.sector_range = get_val("sector_range_min", "sector_range_max");
        s.perimeter_x_offset = get_val("perimeter_x_offset_min", "perimeter_x_offset_max");
        s.perimeter_y_offset = get_val("perimeter_y_offset_min", "perimeter_y_offset_max");

        s.info = info;
        spawn_queue_.push_back(s);
    }
}

void AssetSpawnPlanner::parse_batch_assets() {
    if (!root_json_.contains("batch_assets")) return;

    const auto& batch_data = root_json_["batch_assets"];
    if (!batch_data.value("has_batch_assets", false)) return;

    batch_grid_spacing_ = (batch_data.value("grid_spacing_min", 100) + batch_data.value("grid_spacing_max", 100)) / 2;
    batch_jitter_ = (batch_data.value("jitter_min", 0) + batch_data.value("jitter_max", 0)) / 2;

    for (const auto& entry : batch_data.value("batch_assets", std::vector<nlohmann::json>{})) {
        nlohmann::json asset = entry;

        if (asset.contains("tag") && asset["tag"].is_string()) {
            asset = resolve_asset_from_tag(asset);
        }

        if (!asset.contains("name") || !asset["name"].is_string()) {
            continue;
        }

        BatchSpawnInfo b;
        b.name = asset["name"];
        b.percent = asset.value("percent", 0);
        batch_spawn_assets_.push_back(b);
    }
}

void AssetSpawnPlanner::sort_spawn_queue() {
    const std::vector<std::string> priority_order = {
        "Center", "Entrance", "Exit", "Exact Position", "Perimeter", "Distributed", "DistributedBatch"
    };

    auto to_lower = [](const std::string& s) {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    };

    auto priority_index = [&](const std::string& pos) -> int {
        std::string lower_pos = to_lower(pos);
        for (size_t i = 0; i < priority_order.size(); ++i) {
            if (to_lower(priority_order[i]) == lower_pos) {
                return static_cast<int>(i);
            }
        }
        return static_cast<int>(priority_order.size());
    };

    std::sort(spawn_queue_.begin(), spawn_queue_.end(), [&](const SpawnInfo& a, const SpawnInfo& b) {
        return priority_index(a.position) < priority_index(b.position);
    });
}

nlohmann::json AssetSpawnPlanner::resolve_asset_from_tag(const nlohmann::json& tag_entry) {
    static std::mt19937 rng(std::random_device{}());
    std::string tag = tag_entry.value("tag", "");

    std::vector<std::string> matches;
    for (const auto& [name, info] : asset_library_->all()) {
        if (info && info->has_tag(tag)) {
            matches.push_back(name);
        }
    }

    if (matches.empty()) {
        throw std::runtime_error("No assets found for tag: " + tag);
    }

    std::uniform_int_distribution<size_t> dist(0, matches.size() - 1);
    std::string selected = matches[dist(rng)];

    nlohmann::json result = tag_entry;
    result["name"] = selected;
    result.erase("tag");
    return result;
}

int AssetSpawnPlanner::get_batch_grid_spacing() const {
    return batch_grid_spacing_;
}

int AssetSpawnPlanner::get_batch_jitter() const {
    return batch_jitter_;
}