#pragma once

#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>

#include "utils/map_grid_settings.hpp"

class Asset;
class Assets;
class Room;
class Area;
class AssetLibrary;
struct SDL_Rect;
struct SDL_Texture;
struct SDL_Renderer;
struct LayerSpec;

namespace devmode::core {
class ManifestStore;
}

namespace world {
class WorldGrid;
struct Chunk;
}

class AssetLoader {

        public:
    AssetLoader(const std::string& map_id,
                const nlohmann::json& map_manifest,
                SDL_Renderer* renderer,
                std::string content_root = {},
                devmode::core::ManifestStore* manifest_store = nullptr,
                AssetLibrary* shared_asset_library = nullptr);
    ~AssetLoader();
    std::vector<Asset*> collectDistantAssets(int lock_threshold, int remove_threshold);

    void createAssets(world::WorldGrid& grid);
    std::vector<const Area*> getAllRoomAndTrailAreas() const;
    AssetLibrary* getAssetLibrary() const { return asset_library_; }
    const std::vector<Room*>& getRooms() const { return rooms_; }
    double getMapRadius() const { return map_radius_; }
    const nlohmann::json& map_manifest() const { return map_manifest_json_; }
    const std::string& map_identifier() const { return map_id_; }
    const std::string& content_root() const { return map_path_; }

        private:
    std::string map_id_;
    std::string map_path_;
    SDL_Renderer* renderer_;
    std::vector<Room*> rooms_;
    std::vector<std::unique_ptr<Room>> all_rooms_;
    std::unique_ptr<AssetLibrary> owned_asset_library_;
    AssetLibrary* asset_library_ = nullptr;
    bool using_shared_asset_library_ = false;
    std::vector<LayerSpec>              map_layers_;
    std::vector<double>                 layer_radii_;
    double map_center_x_ = 0.0;
    double map_center_y_ = 0.0;
    double map_radius_   = 0.0;
    MapGridSettings map_grid_settings_{};
    nlohmann::json map_manifest_json_;
    nlohmann::json* map_assets_data_   = nullptr;
    nlohmann::json* map_boundary_data_ = nullptr;
    nlohmann::json* rooms_data_        = nullptr;
    nlohmann::json* trails_data_       = nullptr;
    devmode::core::ManifestStore* manifest_store_ = nullptr;
    void load_from_manifest(const nlohmann::json& map_manifest);
    void loadRooms();
    void finalizeAssets();
    std::vector<std::unique_ptr<Asset>> extract_all_assets();
};
