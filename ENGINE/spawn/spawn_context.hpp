#pragma once

#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <SDL.h>
#include "asset/Asset.hpp"
#include "utils/area.hpp"
#include "asset/asset_info.hpp"
#include "spawn/check.hpp"
#include "utils/grid.hpp"
#include "utils/grid_occupancy.hpp"
#include "utils/map_grid_settings.hpp"

class Asset;
class Area;
class AssetInfo;
class AssetLibrary;
class AssetSpawnPlanner;
class AssetSpawner;

class SpawnContext {

        public:
    using Point = SDL_Point;
    SpawnContext(std::mt19937& rng, Check& checker, std::vector<Area>& exclusion_zones, const std::unordered_map<std::string, std::shared_ptr<AssetInfo>>& asset_info_library, std::vector<std::unique_ptr<Asset>>& all, AssetLibrary* asset_library, vibble::grid::Grid& grid, vibble::grid::Occupancy* occupancy = nullptr);
    Asset* spawnAsset(const std::string& name,
                      const std::shared_ptr<AssetInfo>& info,
                      const Area& area,
                      SDL_Point pos,
                      int depth,
                      Asset* parent,
                      const std::string& spawn_id = std::string{},
                      const std::string& spawn_method = std::string{});
    Asset* spawnTiledAsset(const std::string& name,
                           const std::shared_ptr<AssetInfo>& info,
                           const Area& area,
                           SDL_Point pos,
                           int depth,
                           Asset* parent,
                           const std::string& spawn_id = std::string{},
                           const std::string& spawn_method = std::string{});
    Point get_area_center(const Area& area) const;
    Point get_point_within_area(const Area& area);
    std::mt19937& rng() { return rng_; }
    Check& checker() { return checker_; }
    std::vector<Area>& exclusion_zones() { return exclusion_zones_; }
    const std::unordered_map<std::string, std::shared_ptr<AssetInfo>>& info_library() const { return asset_info_library_; }
    std::vector<std::unique_ptr<Asset>>& all_assets() { return all_; }
    vibble::grid::Grid& grid() { return grid_; }
    vibble::grid::Occupancy* occupancy() { return occupancy_; }
    const vibble::grid::Occupancy* occupancy() const { return occupancy_; }
    int spawn_resolution() const { return spawn_resolution_; }
    void set_spawn_resolution(int resolution) { spawn_resolution_ = vibble::grid::clamp_resolution(resolution); }

    bool checks_enabled() const { return checks_enabled_; }
    void set_checks_enabled(bool enabled) { checks_enabled_ = enabled; }

    bool allow_partial_clip_overlap() const { return allow_partial_clip_overlap_; }
    void set_allow_partial_clip_overlap(bool allow) { allow_partial_clip_overlap_ = allow; }

    bool position_allowed(const Area& area, SDL_Point pos) const;

    void set_map_grid_settings(const MapGridSettings& settings);
    const MapGridSettings& map_grid_settings() const { return map_grid_settings_; }

    void set_spacing_filter(std::unordered_set<std::string> names);
    void set_spacing_filter(const std::unordered_set<std::string>* names);
    bool track_spacing_for(const std::shared_ptr<AssetInfo>& info, bool enforce_spacing, bool default_track = true) const;

    void set_clip_area(const Area* a) { clip_area_ = a; }
    const Area* clip_area() const { return clip_area_; }

    void set_trail_areas(std::vector<const Area*> areas) { trail_areas_ = std::move(areas); }
    const std::vector<const Area*>& trail_areas() const { return trail_areas_; }
    bool point_overlaps_trail(SDL_Point pt, const Area* ignore = nullptr) const;

        private:
    std::mt19937& rng_;
    Check& checker_;
    std::vector<Area>& exclusion_zones_;
    const std::unordered_map<std::string, std::shared_ptr<AssetInfo>>& asset_info_library_;
    std::vector<std::unique_ptr<Asset>>& all_;
    AssetLibrary* asset_library_;
    vibble::grid::Grid& grid_;
    vibble::grid::Occupancy* occupancy_ = nullptr;
    int spawn_resolution_ = 0;
    const Area* clip_area_ = nullptr;
    std::vector<const Area*> trail_areas_{};
    MapGridSettings map_grid_settings_ = MapGridSettings::defaults();
    bool checks_enabled_ = true;
    bool allow_partial_clip_overlap_ = false;
    std::unordered_set<std::string> spacing_filter_storage_;
    const std::unordered_set<std::string>* spacing_filter_ = nullptr;

    Asset* spawnAssetInternal(const std::string& name, const std::shared_ptr<AssetInfo>& info, const Area& area, SDL_Point pos, int depth, Asset* parent, const std::string& spawn_id, const std::string& spawn_method, const std::optional<Asset::TilingInfo>& tiling);
};
