#pragma once

#include <vector>
#include <string>
#include <memory>
#include <random>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "utils/area.hpp"
#include "asset/Asset.hpp"
#include "map_generation/room.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_library.hpp"
#include "asset_spawn_planner.hpp"
#include "check.hpp"
#include "spawn_info.hpp"
#include "utils/map_grid_settings.hpp"

class AssetSpawner {

	public:
    using Point = std::pair<int, int>;
    AssetSpawner(AssetLibrary* asset_library, std::vector<Area> exclusion_zones);
    void spawn(Room& room);
    void spawn_children(const Area& spawn_area, const std::unordered_map<std::string, Area>& area_lookup, AssetSpawnPlanner* planner);
    std::vector<std::unique_ptr<Asset>> spawn_boundary_from_json(const nlohmann::json& boundary_json, const Area& spawn_area, const std::string& source_name);
    std::vector<std::unique_ptr<Asset>> extract_all_assets();
    void set_map_grid_settings(const MapGridSettings& settings) { map_grid_settings_ = settings; }

        private:
    void run_spawning(AssetSpawnPlanner* planner, const Area& area);
    void run_edge_spawning(const Area& area);
    void run_child_spawning(AssetSpawnPlanner* planner, const Area& default_area, const std::unordered_map<std::string, Area>& area_lookup);
    void spawn_all_children();
    std::vector<Area> exclusion_zones;
    AssetLibrary* asset_library_;
    std::mt19937 rng_;
    Check checker_;
    std::vector<SpawnInfo> spawn_queue_;
    std::unordered_map<std::string, std::shared_ptr<AssetInfo>> asset_info_library_;
    std::vector<std::unique_ptr<Asset>> all_;
    bool boundary_mode_ = false;
    Room* current_room_ = nullptr;
    MapGridSettings map_grid_settings_ = MapGridSettings::defaults();

    std::unordered_map<std::string, int> group_resolution_map_;
};
