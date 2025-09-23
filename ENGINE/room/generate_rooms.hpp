#pragma once

#include "room.hpp"
#include "utils/area.hpp"
#include "asset/asset_library.hpp"
#include <vector>
#include <string>
#include <memory>
#include <random>
#include <unordered_map>
#include <SDL.h>
#include <nlohmann/json.hpp>

struct RoomSpec {
        std::string name;
        int max_instances;
        std::vector<std::string> required_children;
};

struct LayerSpec {
        int level;
        int radius;
        int max_rooms;
        std::vector<RoomSpec> rooms;
};

class GenerateRooms {

	public:
    using Point = SDL_Point;
    GenerateRooms(const std::vector<LayerSpec>& layers,
                  int map_cx,
                  int map_cy,
                  const std::string& map_dir,
                  const std::string& map_info_path);
    std::vector<std::unique_ptr<Room>> build(AssetLibrary* asset_lib,
                                             double map_radius,
                                             const nlohmann::json& boundary_data,
                                             nlohmann::json& rooms_data,
                                             nlohmann::json& trails_data,
                                             const nlohmann::json& map_assets_data);
    bool testing = false;

	private:
    struct Sector {
    Room* room;
    float start_angle;
    float span_angle;
	};
    SDL_Point polar_to_cartesian(int cx, int cy, int radius, float angle_rad);
    std::vector<RoomSpec> get_children_from_layer(const LayerSpec& layer);
    std::vector<LayerSpec> map_layers_;
    int map_center_x_;
    int map_center_y_;
    std::string map_path_;
    std::string map_info_path_;
    std::mt19937 rng_;
};
