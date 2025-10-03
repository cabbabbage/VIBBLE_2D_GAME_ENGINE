#pragma once

#include <vector>
#include <string>
#include <memory>
#include <random>
#include <utility>
#include <SDL.h>
#include <nlohmann/json.hpp>

class Room;
class Area;
class AssetLibrary;

class TrailGeometry {

	public:
    using Point = SDL_Point;
    static std::vector<SDL_Point> build_centerline(const SDL_Point& start, const SDL_Point& end, int curvyness, std::mt19937& rng);
    static std::vector<SDL_Point> extrude_centerline(const std::vector<SDL_Point>& centerline, double width);
    static SDL_Point compute_edge_point(const SDL_Point& center, const SDL_Point& toward, const Area* area);
    static bool attempt_trail_connection(Room* a, Room* b, std::vector<Area>& existing_areas, const std::string& map_dir, const std::string& map_info_path, AssetLibrary* asset_lib, std::vector<std::unique_ptr<Room>>& trail_rooms, int allowed_intersections, nlohmann::json* trail_config, const std::string& trail_name, const nlohmann::json* map_assets_data, double map_radius, bool testing, std::mt19937& rng);
};
