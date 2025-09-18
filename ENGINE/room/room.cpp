#include "room.hpp"
#include "spawn/asset_spawner.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <random>
#include <algorithm>
#include <iostream>
#include <cmath>
using json = nlohmann::json;

Room::Room(Point origin,
           std::string type_,
           const std::string& room_def_name,
           Room* parent,
           const std::string& map_dir,
           const std::string& map_info_path,
           AssetLibrary* asset_lib,
           Area* precomputed_area,
           nlohmann::json* room_data,
           const nlohmann::json* map_assets_data,
           double map_radius,
           const std::string& data_section
)
: map_origin(origin),
parent(parent),
room_name(room_def_name),
room_directory(map_info_path + "::" + data_section),
map_path(map_dir),
json_path(map_info_path + "::" + data_section + "::" + room_def_name),
room_area(nullptr),
type(type_),
room_data_ptr_(room_data),
map_assets_data_ptr_(map_assets_data),
map_info_path_(map_info_path),
data_section_(data_section)
{
        if (testing) {
                std::cout << "[Room] Created room: " << room_name
                << " at (" << origin.first << ", " << origin.second << ")"
                << (parent ? " with parent\n" : " (no parent)\n");
        }
        if (room_data_ptr_) {
                if (room_data_ptr_->is_null()) {
                        *room_data_ptr_ = json::object();
                }
                if (room_data_ptr_->is_object()) {
                        assets_json = *room_data_ptr_;
                }
        }
        if (!assets_json.is_object()) {
                assets_json = json::object();
        }
        int map_radius_int = static_cast<int>(std::round(map_radius));
        if (map_radius_int < 0) map_radius_int = 0;
        int map_w = map_radius_int * 2;
        int map_h = map_radius_int * 2;
        if (precomputed_area) {
                if (testing) {
                        std::cout << "[Room] Using precomputed area for: " << room_name << "\n";
                }
                room_area = std::make_unique<Area>(room_name, precomputed_area->get_points());
        } else {
                int min_w = assets_json.value("min_width", 64);
                int max_w = assets_json.value("max_width", 64);
                int min_h = assets_json.value("min_height", 64);
                int max_h = assets_json.value("max_height", 64);
                int edge_smoothness = assets_json.value("edge_smoothness", 2);
                std::string geometry = assets_json.value("geometry", "square");
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
                room_area = std::make_unique<Area>(room_name, SDL_Point{map_origin.first, map_origin.second}, width, height, geometry, edge_smoothness, map_w, map_h);
	}
	std::vector<json> json_sources;
	std::vector<std::string> source_paths;
	json_sources.push_back(assets_json);
        source_paths.push_back(json_path);
        if (assets_json.value("inherits_map_assets", false) && map_assets_data_ptr_) {
                json_sources.push_back(*map_assets_data_ptr_);
                source_paths.push_back(map_info_path_ + "::map_assets_data");
        }
        planner = std::make_unique<AssetSpawnPlanner>( json_sources, *room_area, *asset_lib, source_paths );
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
        json spawn_groups = json::array();
        int cx = 0, cy = 0;
        if (room_area) {
                auto c = room_area->get_center();
                cx = c.x;
                cy = c.y;
	}
        for (const auto& uptr : assets) {
                const Asset* a = uptr.get();
                if (!a || !a->info) continue; // skip assets lacking runtime info

                const int ax = a->pos.x;
                const int ay = a->pos.y;
                json entry;
                entry["min_number"] = 1;
                entry["max_number"] = 1;
                entry["position"] = "Exact";
                entry["check_overlap"] = false;
                entry["enforce_spacing"] = false;
                entry["dx"] = ax - cx;
                entry["dy"] = ay - cy;
                if (width > 0) entry["origional_width"] = width;
                if (height > 0) entry["origional_height"] = height;
                entry["display_name"] = a->info->name;
                entry["candidates"] = json::array();
                entry["candidates"].push_back({{"name", "null"}, {"chance", 0}});
                entry["candidates"].push_back({{"name", a->info->name}, {"chance", 100}});
                spawn_groups.push_back(std::move(entry));
        }
        if (is_spawn) {
                json davey_entry;
                davey_entry["min_number"] = 1;
                davey_entry["max_number"] = 1;
                davey_entry["position"] = "Center";
                davey_entry["check_overlap"] = false;
                davey_entry["enforce_spacing"] = false;
                davey_entry["display_name"] = "Vibble";
                davey_entry["candidates"] = json::array();
                davey_entry["candidates"].push_back({{"name", "null"}, {"chance", 0}});
                davey_entry["candidates"].push_back({{"name", "Vibble"}, {"chance", 100}});
                spawn_groups.push_back(std::move(davey_entry));
        }
        out["spawn_groups"] = std::move(spawn_groups);
        return out;
}

nlohmann::json& Room::assets_data() {
        if (assets_json.contains("assets") && assets_json["assets"].is_array() &&
            (!assets_json.contains("spawn_groups") || !assets_json["spawn_groups"].is_array())) {
                assets_json["spawn_groups"] = assets_json["assets"];
                assets_json.erase("assets");
        }
        if (!assets_json.contains("spawn_groups") || !assets_json["spawn_groups"].is_array()) {
                assets_json["spawn_groups"] = nlohmann::json::array();
        }
        auto& groups = assets_json["spawn_groups"];
        for (auto& entry : groups) {
                if (!entry.is_object()) continue;
                if (entry.contains("position") && entry["position"].is_string() &&
                    entry["position"].get<std::string>() == "Exact Position") {
                        entry["position"] = "Exact";
                }
                if (entry.contains("check_min_spacing") && !entry.contains("enforce_spacing")) {
                        entry["enforce_spacing"] = entry["check_min_spacing"];
                        entry.erase("check_min_spacing");
                }
                if (entry.contains("exact_dx") && entry["exact_dx"].is_number()) {
                        if (!entry.contains("dx")) entry["dx"] = entry["exact_dx"];
                        entry.erase("exact_dx");
                }
                if (entry.contains("exact_dy") && entry["exact_dy"].is_number()) {
                        if (!entry.contains("dy")) entry["dy"] = entry["exact_dy"];
                        entry.erase("exact_dy");
                }
                if (entry.contains("exact_origin_width") && entry["exact_origin_width"].is_number_integer()) {
                        if (!entry.contains("origional_width")) entry["origional_width"] = entry["exact_origin_width"];
                        entry.erase("exact_origin_width");
                }
                if (entry.contains("exact_origin_height") && entry["exact_origin_height"].is_number_integer()) {
                        if (!entry.contains("origional_height")) entry["origional_height"] = entry["exact_origin_height"];
                        entry.erase("exact_origin_height");
                }
                if (entry.contains("percent_x_min") && entry["percent_x_min"].is_number()) {
                        if (!entry.contains("p_x_min")) entry["p_x_min"] = entry["percent_x_min"];
                        entry.erase("percent_x_min");
                }
                if (entry.contains("percent_x_max") && entry["percent_x_max"].is_number()) {
                        if (!entry.contains("p_x_max")) entry["p_x_max"] = entry["percent_x_max"];
                        entry.erase("percent_x_max");
                }
                if (entry.contains("percent_y_min") && entry["percent_y_min"].is_number()) {
                        if (!entry.contains("p_y_min")) entry["p_y_min"] = entry["percent_y_min"];
                        entry.erase("percent_y_min");
                }
                if (entry.contains("percent_y_max") && entry["percent_y_max"].is_number()) {
                        if (!entry.contains("p_y_max")) entry["p_y_max"] = entry["percent_y_max"];
                        entry.erase("percent_y_max");
                }
                if (entry.contains("border_shift") && entry["border_shift"].is_number()) {
                        if (!entry.contains("percentage_shift_from_center")) entry["percentage_shift_from_center"] = entry["border_shift"];
                        entry.erase("border_shift");
                }
                if (entry.contains("border_shift_min")) entry.erase("border_shift_min");
                if (entry.contains("border_shift_max")) entry.erase("border_shift_max");
                if (entry.contains("ep_x_min")) entry.erase("ep_x_min");
                if (entry.contains("ep_x_max")) entry.erase("ep_x_max");
                if (entry.contains("ep_y_min")) entry.erase("ep_y_min");
                if (entry.contains("ep_y_max")) entry.erase("ep_y_max");
                if (!entry.contains("candidates") || !entry["candidates"].is_array()) {
                        entry["candidates"] = nlohmann::json::array();
                }
                auto& cand_arr = entry["candidates"];
                bool has_null = false;
                for (auto& cand : cand_arr) {
                        if (cand.is_null()) {
                                has_null = true;
                                cand = nlohmann::json{{"name", "null"}, {"chance", 0}};
                                continue;
                        }
                        if (cand.is_string()) {
                                std::string value = cand.get<std::string>();
                                nlohmann::json converted;
                                converted["name"] = value;
                                converted["chance"] = (value == "null") ? 0 : 100;
                                cand = std::move(converted);
                        }
                        if (!cand.is_object()) continue;
                        if (cand.contains("name") && cand["name"].is_string() && cand["name"].get<std::string>() == "null") {
                                has_null = true;
                        }
                        if (!cand.contains("chance")) {
                                cand["chance"] = cand.contains("name") && cand["name"].is_string() && cand["name"].get<std::string>() == "null" ? 0 : 100;
                        }
                        if (cand.contains("tag")) cand.erase("tag");
                        if (cand.contains("tag_name")) cand.erase("tag_name");
                }
                if (!has_null) {
                        nlohmann::json null_cand;
                        null_cand["name"] = "null";
                        null_cand["chance"] = 0;
                        cand_arr.insert(cand_arr.begin(), std::move(null_cand));
                }
        }
        return assets_json;
}

bool Room::is_spawn_room() const {
        return assets_json.value("is_spawn", false);
}

void Room::save_assets_json() const {
        if (room_data_ptr_) {
                *room_data_ptr_ = assets_json;
        }
        if (map_info_path_.empty() || data_section_.empty()) {
                return;
        }
        nlohmann::json map_info_json;
        std::ifstream in(map_info_path_);
        if (in.is_open()) {
                in >> map_info_json;
        }
        if (!map_info_json.is_object()) {
                map_info_json = nlohmann::json::object();
        }
        nlohmann::json& section = map_info_json[data_section_];
        if (!section.is_object()) {
                section = nlohmann::json::object();
        }
        section[room_name] = assets_json;
        std::ofstream out(map_info_path_);
        if (out.is_open()) {
                out << map_info_json.dump(2);
        }
}
