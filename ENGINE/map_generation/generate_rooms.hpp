#pragma once

#include "room.hpp"
#include "utils/area.hpp"
#include "asset/asset_library.hpp"
#include "map_layers_geometry.hpp"
#include <vector>
#include <string>
#include <memory>
#include <random>
#include <unordered_map>
#include <SDL.h>
#include <nlohmann/json.hpp>

#include "utils/map_grid_settings.hpp"

namespace devmode::core {
class ManifestStore;
}

struct RoomSpec {
        std::string name;
        int max_instances;
        std::vector<std::string> required_children;
};

struct LayerSpec {
        int level = 0;
        int max_rooms = 0;
        std::vector<RoomSpec> rooms;
};

class GenerateRooms {

	public:
    using Point = SDL_Point;
    GenerateRooms(const std::vector<LayerSpec>& layers,
                  int map_cx,
                  int map_cy,
                  const std::string& map_id,
                  nlohmann::json& map_manifest,
                  double min_edge_distance,
                  devmode::core::ManifestStore* manifest_store = nullptr,
                  Room::ManifestWriter manifest_writer = {});
    std::vector<std::unique_ptr<Room>> build(AssetLibrary* asset_lib, double map_radius, const std::vector<double>& layer_radii, const nlohmann::json& boundary_data, nlohmann::json& rooms_data, nlohmann::json& trails_data, nlohmann::json& map_assets_data, const MapGridSettings& grid_settings);
    bool testing = false;

	private:
    struct Sector {
    Room* room;
    float start_angle;
    float span_angle;
};
    SDL_Point polar_to_cartesian(int cx, int cy, double radius, float angle_rad);
    std::vector<RoomSpec> get_children_from_layer(const LayerSpec& layer);
    std::vector<LayerSpec> map_layers_;
    int map_center_x_;
    int map_center_y_;
    std::string map_id_;
    nlohmann::json* map_manifest_ = nullptr;
    devmode::core::ManifestStore* manifest_store_ = nullptr;
    Room::ManifestWriter manifest_writer_{};
    std::mt19937 rng_;
    double min_edge_distance_ = static_cast<double>(map_layers::kDefaultMinEdgeDistance);
};
