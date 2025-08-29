#pragma once

#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include "asset/Asset.hpp"
#include "utils/area.hpp"
#include "asset/asset_info.hpp"

class Asset;
class Area;
class AssetInfo;
class AssetLibrary;
class AssetSpawnPlanner;
class AssetSpawner;
class Check;
class SpawnLogger;

class SpawnContext {
public:
    using Point = std::pair<int, int>;

    SpawnContext(std::mt19937& rng,
                 Check& checker,
                 SpawnLogger& logger,
                 std::vector<Area>& exclusion_zones,
                 std::unordered_map<std::string, std::shared_ptr<AssetInfo>>& asset_info_library,
                 std::vector<std::unique_ptr<Asset>>& all,
                 AssetLibrary* asset_library);

    Asset* spawnAsset(const std::string& name,
                      const std::shared_ptr<AssetInfo>& info,
                      const Area& area,
                      int x,
                      int y,
                      int depth,
                      Asset* parent);

    Point get_area_center(const Area& area) const;
    Point get_point_within_area(const Area& area);

    std::mt19937& rng() { return rng_; }
    Check& checker() { return checker_; }
    SpawnLogger& logger() { return logger_; }
    std::vector<Area>& exclusion_zones() { return exclusion_zones_; }
    std::unordered_map<std::string, std::shared_ptr<AssetInfo>>& info_library() { return asset_info_library_; }
    std::vector<std::unique_ptr<Asset>>& all_assets() { return all_; }

private:
    std::mt19937& rng_;
    Check& checker_;
    SpawnLogger& logger_;
    std::vector<Area>& exclusion_zones_;
    std::unordered_map<std::string, std::shared_ptr<AssetInfo>>& asset_info_library_;
    std::vector<std::unique_ptr<Asset>>& all_;
    AssetLibrary* asset_library_;
};

