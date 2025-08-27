#include "room.hpp"
#include "spawn\asset_spawner.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <random>
#include <algorithm>
#include <iostream>

using json = nlohmann::json;

Room::Room(Point origin,
            std::string type_,
           const std::string& room_def_name,
           Room* parent,
           const std::string& room_dir,
           const std::string& map_dir,
           AssetLibrary* asset_lib,
           Area* precomputed_area
           )
    : map_origin(origin),
      parent(parent),
      room_name(room_def_name),
      room_directory(room_dir),
      map_path(map_dir),
      json_path(room_dir + "/" + room_def_name + ".json"),
      room_area(nullptr), 
      type(type_)
{
    if (testing) {
        std::cout << "[Room] Created room: " << room_name
                  << " at (" << origin.first << ", " << origin.second << ")"
                  << (parent ? " with parent\n" : " (no parent)\n");
    }

    std::ifstream in(json_path);
    if (!in.is_open()) {
        throw std::runtime_error("[Room] Failed to open room JSON: " + json_path);
    }
    json J;
    in >> J;
    assets_json = J;

    int map_radius = 0;
    {
        std::ifstream minf(map_path + "/map_info.json");
        if (minf.is_open()) {
            json m;
            minf >> m;
            map_radius = m.value("map_radius", 0);
        } else if (testing) {
            std::cerr << "[Room] Warning: could not open map_info.json at " << map_path << "\n";
        }
    }
    int map_w = map_radius * 2;
    int map_h = map_radius * 2;

    if (precomputed_area) {
        if (testing) {
            std::cout << "[Room] Using precomputed area for: " << room_name << "\n";
        }
        room_area = std::make_unique<Area>(room_name, precomputed_area->get_points());
    } else {
        int min_w = J.value("min_width", 64);
        int max_w = J.value("max_width", 64);
        int min_h = J.value("min_height", 64);
        int max_h = J.value("max_height", 64);
        int edge_smoothness = J.value("edge_smoothness", 2);
        std::string geometry = J.value("geometry", "square");
        if (!geometry.empty()) geometry[0] = std::toupper(geometry[0]);

        static std::mt19937 rng(std::random_device{}());
        int width = std::uniform_int_distribution<>(min_w, max_w)(rng);
        int height = std::uniform_int_distribution<>(min_h, max_h)(rng);

        if (testing) {
            std::cout << "[Room] Creating area from JSON: " << room_name
                      << " (" << width << "x" << height << ")"
                      << " at (" << map_origin.first << ", " << map_origin.second << ")"
                      << ", geometry: " << geometry
                      << ", map radius: " << map_radius << "\n";
        }

        room_area = std::make_unique<Area>(room_name,
                                           map_origin.first,
                                           map_origin.second,
                                           width,
                                           height,
                                           geometry,
                                           edge_smoothness,
                                           map_w,
                                           map_h);
    }

    std::vector<json> json_sources;
    json_sources.push_back(assets_json);

    if (assets_json.value("inherits_map_assets", false)) {
        std::ifstream map_in(map_path + "/map_assets.json");
        if (map_in.is_open()) {
            json map_assets;
            map_in >> map_assets;
            json_sources.push_back(map_assets);
        } else if (testing) {
            std::cerr << "[Room] Warning: inherits_map_assets is true, but map_assets.json not found in " << map_path << "\n";
        }
    }

    planner = std::make_unique<AssetSpawnPlanner>(
        json_sources,
        room_area->get_area(),
        *asset_lib
    );

    std::vector<Area> exclusion;
    AssetSpawner spawner(asset_lib, exclusion);
    spawner.spawn(*this);
}

void Room::set_sibling_left(Room* left_room) {
    left_sibling = left_room;
}

void Room::set_sibling_right(Room* right_room) {
    right_sibling = right_room;
}

void Room::add_connecting_room(Room* room) {
    if (room && std::find(connected_rooms.begin(), connected_rooms.end(), room) == connected_rooms.end()) {
        connected_rooms.push_back(room);
    }
}

void Room::remove_connecting_room(Room* room) {
    auto it = std::find(connected_rooms.begin(), connected_rooms.end(), room);
    if (it != connected_rooms.end()) connected_rooms.erase(it);
}

void Room::add_room_assets(std::vector<std::unique_ptr<Asset>> new_assets) {
    for (auto& asset : new_assets)
        assets.push_back(std::move(asset));
}

std::vector<std::unique_ptr<Asset>>&& Room::get_room_assets() {
    return std::move(assets);
}


void Room::set_scale(double s) {
    if (s <= 0.0) s = 1.0;  
    scale_ = s;
}

int Room::clamp_int(int v, int lo, int hi) const {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void Room::bounds_to_size(const std::tuple<int,int,int,int>& b, int& w, int& h) const {
    int minx, miny, maxx, maxy;
    std::tie(minx, miny, maxx, maxy) = b;
    w = std::max(0, maxx - minx);
    h = std::max(0, maxy - miny);
}



nlohmann::json Room::create_static_room_json(std::string name) {
    json out;

    
    const std::string geometry = assets_json.value("geometry", "Square");
    const int edge_smoothness = assets_json.value("edge_smoothness", 2);

    int width = 0, height = 0;
    if (room_area) {
        bounds_to_size(room_area->get_bounds(), width, height);
    }

    out["name"] = std::move(name);
    out["min_width"] = width;
    out["max_width"] = width;
    out["min_height"] = height;
    out["max_height"] = height;
    out["edge_smoothness"] = edge_smoothness;
    out["geometry"] = geometry;

    
    bool is_spawn = assets_json.value("is_spawn", false);
    out["is_spawn"] = is_spawn;
    out["is_boss"] = assets_json.value("is_boss", false);
    out["inherits_map_assets"] = assets_json.value("inherits_map_assets", false);

    
    json assets_arr = json::array();

    int cx = 0, cy = 0;
    if (room_area) {
        auto c = room_area->get_center();
        cx = c.first;
        cy = c.second;
    }

    for (const auto& uptr : assets) {
        const Asset* a = uptr.get();
        

        const int ax = a->pos_X;
        const int ay = a->pos_Y;

        double norm_x = (width  != 0) ? (static_cast<double>(ax - cx) / static_cast<double>(width))  : 0.0;
        double norm_y = (height != 0) ? (static_cast<double>(ay - cy) / static_cast<double>(height)) : 0.0;

        int ep_x = clamp_int(static_cast<int>(std::lround(norm_x * 100.0 + 50.0)), 0, 100);
        int ep_y = clamp_int(static_cast<int>(std::lround(norm_y * 100.0 + 50.0)), 0, 100);

        json entry;
        entry["name"] = a->info->name;
        entry["min_number"] = 1;
        entry["max_number"] = 1;
        entry["position"] = "Exact Position";
        entry["exact_position"] = nullptr;
        entry["inherited"] = false;
        entry["check_overlap"] = false;
        entry["check_min_spacing"] = false;
        entry["tag"] = false;

        entry["ep_x_min"] = ep_x;
        entry["ep_x_max"] = ep_x;
        entry["ep_y_min"] = ep_y;
        entry["ep_y_max"] = ep_y;

        assets_arr.push_back(std::move(entry));
    }

    
    if (is_spawn) {
        json davey_entry = {
            {"name", "Davey"},
            {"min_number", 1},
            {"max_number", 1},
            {"position", "Center"},
            {"exact_position", nullptr},
            {"tag", false},
            {"check_overlap", false},
            {"check_min_spacing", false},
            {"inherited", false}
        };
        assets_arr.push_back(std::move(davey_entry));
    }

    out["assets"] = std::move(assets_arr);

    return out;
}
