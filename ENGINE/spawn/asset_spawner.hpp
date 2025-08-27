
#pragma once

#include <vector>
#include <string>
#include <memory>
#include <random>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "utils/area.hpp"
#include "asset\Asset.hpp"
#include "room\Room.hpp"
#include "asset\asset_info.hpp"
#include "asset\asset_library.hpp"
#include "asset_spawn_planner.hpp"
#include "spawn_logger.hpp"
#include "check.hpp"

class AssetSpawner {
public:
    using Point = std::pair<int, int>;

    AssetSpawner(AssetLibrary* asset_library, std::vector<Area> exclusion_zones);

    void spawn(Room& room);
    void spawn_children(const Area& spawn_area, AssetSpawnPlanner* planner);
    std::vector<std::unique_ptr<Asset>> spawn_boundary_from_file(const std::string& json_path, const Area& spawn_area);
    std::vector<std::unique_ptr<Asset>> extract_all_assets();

private:
    void run_spawning(AssetSpawnPlanner* planner, const Area& area);
    void spawn_all_children();

    std::vector<Area> exclusion_zones;
    AssetLibrary* asset_library_;
    std::mt19937 rng_;
    Check checker_;
    SpawnLogger logger_;
    std::vector<SpawnInfo> spawn_queue_;
    std::unordered_map<std::string, std::shared_ptr<AssetInfo>> asset_info_library_;
    std::vector<std::unique_ptr<Asset>> all_;
};
