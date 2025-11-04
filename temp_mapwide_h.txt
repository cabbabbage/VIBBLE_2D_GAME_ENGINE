#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>
#include <nlohmann/json_fwd.hpp>

#include "utils/map_grid_settings.hpp"

class AssetLibrary;
class Room;
class MapWideAssetSpawner {
public:
    MapWideAssetSpawner(AssetLibrary* asset_library, const MapGridSettings& grid_settings, std::string map_seed, nlohmann::json& map_assets_json);

    void spawn(std::vector<std::unique_ptr<Room>>& rooms);

private:
    std::uint64_t seed_for_index(SDL_Point index) const;
    Room* resolve_owner(SDL_Point world_point, const std::vector<std::unique_ptr<Room>>& rooms) const;

    AssetLibrary* asset_library_ = nullptr;
    MapGridSettings grid_settings_{};
    std::uint64_t base_seed_ = 0;
    nlohmann::json& map_assets_json_;
};
