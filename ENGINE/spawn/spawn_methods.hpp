#pragma once

#include "spawn_logger.hpp"
#include "asset/Asset.hpp"
#include "utils/area.hpp"
#include "asset/asset_info.hpp"
#include "asset_spawn_planner.hpp"
#include "check.hpp"
#include "asset/asset_library.hpp"

#include "asset_spawner.hpp"

#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

class SpawnMethods {
public:
    using Point = std::pair<int, int>;

    SpawnMethods(std::mt19937& rng,
                 Check& checker,
                 SpawnLogger& logger,
                 std::vector<Area>& exclusion_zones,
                 std::unordered_map<std::string, std::shared_ptr<AssetInfo>>& asset_info_library,
                 std::vector<std::unique_ptr<Asset>>& all_assets,
                 AssetLibrary* asset_library);

    void spawn_item_exact(const SpawnInfo& item, const Area* area);
    void spawn_item_center(const SpawnInfo& item, const Area* area);
    void spawn_item_perimeter(const SpawnInfo& item, const Area* area);
    void spawn_item_distributed(const SpawnInfo& item, const Area* area);
    void spawn_item_random(const SpawnInfo& item, const Area* area);
    void spawn_distributed_batch(const std::vector<BatchSpawnInfo>& items,
                                 const Area* area,
                                 int spacing,
                                 int jitter);

    Point get_area_center(const Area& area) const;
    Point get_point_within_area(const Area& area);

private:
    Asset* spawn_(const std::string& name,
                  const std::shared_ptr<AssetInfo>& info,
                  const Area& area,
                  int x,
                  int y,
                  int depth,
                  Asset* parent);

    std::mt19937& rng_;
    Check& checker_;
    SpawnLogger& logger_;
    std::vector<Area>& exclusion_zones_;
    std::unordered_map<std::string, std::shared_ptr<AssetInfo>>& asset_info_library_;
    std::vector<std::unique_ptr<Asset>>& all_;
    AssetLibrary* asset_library_;
};
