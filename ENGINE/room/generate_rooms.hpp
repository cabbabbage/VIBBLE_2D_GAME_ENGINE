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

struct RoomSpec {
	std::string name;
	int min_instances;
	int max_instances;
	std::vector<std::string> required_children;
};

struct LayerSpec {
	int level;
	int radius;
	int min_rooms;
	int max_rooms;
	std::vector<RoomSpec> rooms;
};

class GenerateRooms {

	public:
    using Point = std::pair<int, int>;
    GenerateRooms(const std::vector<LayerSpec>& layers,
    int map_cx,
    int map_cy,
    const std::string& map_dir);
    std::vector<std::unique_ptr<Room>> build(AssetLibrary* asset_lib,
    int map_radius,
    const std::string& boundary_json);
    bool testing = false;

	private:
    struct Sector {
    Room* room;
    float start_angle;
    float span_angle;
	};
    Point polar_to_cartesian(int cx, int cy, int radius, float angle_rad);
    std::vector<RoomSpec> get_children_from_layer(const LayerSpec& layer);
    std::vector<LayerSpec> map_layers_;
    int map_center_x_;
    int map_center_y_;
    std::string map_path_;
    std::mt19937 rng_;
};
