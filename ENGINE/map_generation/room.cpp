#include "room.hpp"
#include "spawn/asset_spawner.hpp"
#include "asset/asset_types.hpp"
#include "utils/relative_room_position.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <algorithm>
#include <initializer_list>
#include <iostream>
#include <cmath>
#include <optional>
#include <string>
using json = nlohmann::json;

namespace {

int positive_from_keys(const nlohmann::json& src, std::initializer_list<const char*> keys) {
        for (const char* key : keys) {
                auto it = src.find(key);
                if (it != src.end() && it->is_number_integer()) {
                        int value = it->get<int>();
                        if (value > 0) return value;
                }
        }
        return 0;
}

bool matches_spawn_trigger(const std::string& value) {
        if (value.empty()) return false;
        std::string lowered = value;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
        });
        return lowered == "trigger" || lowered == "spawning" || lowered == "spawn" ||
               lowered.find("trigger") != std::string::npos ||
               lowered.find("spawn") != std::string::npos;
}

bool should_use_room_center_anchor(const std::string& type, const std::string& name) {
        if (matches_spawn_trigger(type)) return true;
        return matches_spawn_trigger(name);
}

bool is_allowed_room_area_type(const std::string& type, const std::string& name) {
        if (matches_spawn_trigger(type)) {
                return true;
        }
        if (type.empty() && matches_spawn_trigger(name)) {
                return true;
        }
        return type.empty() && name.empty();
}

void update_anchor_and_points_json(nlohmann::json& entry,
                                  const std::vector<SDL_Point>& pts,
                                  std::optional<SDL_Point> forced_anchor = std::nullopt) {
        if (pts.empty()) {
                entry.erase("anchor");
                entry.erase("points");
                return;
        }
        SDL_Point anchor = forced_anchor.value_or(SDL_Point{pts.front().x, pts.front().y});
        if (!forced_anchor.has_value()) {
                for (const auto& p : pts) {
                        anchor.x = std::min(anchor.x, p.x);
                        anchor.y = std::min(anchor.y, p.y);
                }
        }
        entry["anchor"] = nlohmann::json::object({
                {"x", anchor.x},
                {"y", anchor.y}
        });
        nlohmann::json points = nlohmann::json::array();
        points.get_ref<nlohmann::json::array_t&>().reserve(pts.size());
        for (const auto& p : pts) {
                points.push_back({ {"x", p.x - anchor.x}, {"y", p.y - anchor.y} });
        }
        entry["points"] = std::move(points);
}

void write_relative_points_json(nlohmann::json& entry,
                                const std::vector<SDL_Point>& pts,
                                SDL_Point center,
                                int original_width,
                                int original_height) {
        original_width = std::max(1, original_width);
        original_height = std::max(1, original_height);
        entry["origional_width"] = original_width;
        entry["origional_height"] = original_height;
        nlohmann::json relative = nlohmann::json::array();
        relative.get_ref<nlohmann::json::array_t&>().reserve(pts.size());
        for (const auto& p : pts) {
                relative.push_back({ {"dx", p.x - center.x}, {"dy", p.y - center.y} });
        }
        entry["relative_points"] = std::move(relative);
}

}  // namespace

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
        // Parse any room-level named areas (trigger/spawning) into class members
        load_named_areas_from_json();
        int map_radius_int = static_cast<int>(std::round(map_radius));
        if (map_radius_int < 0) map_radius_int = 0;
        int map_w = map_radius_int * 2;
        int map_h = map_radius_int * 2;
        if (precomputed_area) {
                if (testing) {
                        std::cout << "[Room] Using precomputed area for: " << room_name << "\n";
                }
                room_area = std::make_unique<Area>(room_name, precomputed_area->get_points());
                if (room_area) room_area->set_type("room");
        } else {
                int min_w = assets_json.value("min_width", 64);
                int max_w = assets_json.value("max_width", min_w);
                int min_h = assets_json.value("min_height", 64);
                int max_h = assets_json.value("max_height", min_h);
                int edge_smoothness = assets_json.value("edge_smoothness", 2);
                std::string geometry = assets_json.value("geometry", "square");
                if (!geometry.empty()) geometry[0] = std::toupper(geometry[0]);
                auto infer_radius_from_dims = [](int w_min, int w_max, int h_min, int h_max) {
                        int diameter = 0;
                        diameter = std::max(diameter, std::max(w_min, w_max));
                        diameter = std::max(diameter, std::max(h_min, h_max));
                        if (diameter <= 0) return 0;
                        return std::max(1, diameter / 2);
                };
                std::string lowered_geometry = geometry;
                std::transform(lowered_geometry.begin(), lowered_geometry.end(), lowered_geometry.begin(), [](unsigned char ch) {
                        return static_cast<char>(std::tolower(ch));
                });
                if (lowered_geometry == "circle") {
                        int radius = assets_json.value("radius", -1);
                        if (radius <= 0) {
                                radius = infer_radius_from_dims(min_w, max_w, min_h, max_h);
                        }
                        if (radius <= 0) {
                                radius = 1;
                        }
                        min_w = max_w = min_h = max_h = radius * 2;
                        assets_json["radius"] = radius;
                }
                int width = std::max(min_w, max_w);
                int height = std::max(min_h, max_h);
                if (testing) {
                        std::cout << "[Room] Creating area from JSON: " << room_name
                        << " (" << width << "x" << height << ")"
                        << " at (" << map_origin.first << ", " << map_origin.second << ")"
                        << ", geometry: " << geometry
			<< ", map radius: " << map_radius << "\n";
		}
                room_area = std::make_unique<Area>(room_name, SDL_Point{map_origin.first, map_origin.second}, width, height, geometry, edge_smoothness, map_w, map_h);
                if (room_area) room_area->set_type("room");
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

void Room::load_named_areas_from_json() {
        areas.clear();
        try {
                if (!assets_json.is_object()) return;
                if (!assets_json.contains("areas") || !assets_json["areas"].is_array()) return;

                SDL_Point center = room_area ? room_area->get_center()
                                             : SDL_Point{map_origin.first, map_origin.second};
                int curr_w = 0;
                int curr_h = 0;
                if (room_area) {
                        auto [minx, miny, maxx, maxy] = room_area->get_bounds();
                        curr_w = std::max(1, maxx - minx);
                        curr_h = std::max(1, maxy - miny);
                } else {
                        curr_w = positive_from_keys(assets_json, {"max_width", "width_max", "min_width", "width_min"});
                        curr_h = positive_from_keys(assets_json, {"max_height", "height_max", "min_height", "height_min"});
                        int radius = assets_json.value("radius", 0);
                        if (radius > 0) {
                                curr_w = std::max(curr_w, radius * 2);
                                curr_h = std::max(curr_h, radius * 2);
                        }
                        curr_w = std::max(1, curr_w);
                        curr_h = std::max(1, curr_h);
                }

                for (auto& item : assets_json["areas"]) {
                        if (!item.is_object()) continue;
                        const std::string name = item.value("name", std::string{});
                        if (name.empty()) continue;
                        const std::string type = item.value("type", std::string{});

                        if (!is_allowed_room_area_type(type, name)) {
                                std::cerr << "[Room] Ignoring area '" << name << "' with unsupported type '"
                                          << type << "' (rooms support spawn/trigger areas only).\n";
                                continue;
                        }

                        int orig_w = item.value("origional_width", item.value("original_width", curr_w));
                        int orig_h = item.value("origional_height", item.value("original_height", curr_h));
                        if (orig_w <= 0) orig_w = curr_w;
                        if (orig_h <= 0) orig_h = curr_h;

                        std::vector<SDL_Point> pts;
                        bool used_relative = false;
                        auto rel_it = item.find("relative_points");
                        if (rel_it != item.end() && rel_it->is_array() && !rel_it->empty()) {
                                used_relative = true;
                                pts.reserve(rel_it->size());
                                for (const auto& rel : *rel_it) {
                                        if (!rel.is_object()) continue;
                                        int dx = rel.value("dx", rel.value("x", 0));
                                        int dy = rel.value("dy", rel.value("y", 0));
                                        RelativeRoomPosition rel_pos(SDL_Point{dx, dy}, orig_w, orig_h);
                                        SDL_Point resolved = rel_pos.resolve(center, curr_w, curr_h);
                                        pts.push_back(resolved);
                                }
                        }
                        if (!used_relative) {
                                int ax = 0;
                                int ay = 0;
                                if (item.contains("anchor") && item["anchor"].is_object()) {
                                        ax = item["anchor"].value("x", 0);
                                        ay = item["anchor"].value("y", 0);
                                }
                                if (item.contains("points") && item["points"].is_array()) {
                                        pts.reserve(item["points"].size());
                                        for (const auto& p : item["points"]) {
                                                if (!p.is_object()) continue;
                                                int rx = p.value("x", 0);
                                                int ry = p.value("y", 0);
                                                pts.push_back(SDL_Point{ ax + rx, ay + ry });
                                        }
                                }
                        }
                        if (pts.size() < 3) continue;

                        item["origional_width"] = orig_w;
                        item["origional_height"] = orig_h;
                        std::optional<SDL_Point> forced_anchor;
                        if (should_use_room_center_anchor(type, name)) {
                                forced_anchor = center;
                        }
                        if (used_relative) {
                                update_anchor_and_points_json(item, pts, forced_anchor);
                        } else {
                                write_relative_points_json(item, pts, center, orig_w, orig_h);
                                update_anchor_and_points_json(item, pts, forced_anchor);
                        }

                        NamedArea na;
                        na.name = name;
                        na.type = type;
                        na.area = std::make_unique<Area>(name, pts);
                        if (na.area) na.area->set_type(type);
                        areas.push_back(std::move(na));
                }
        } catch (...) {
                // Silently ignore malformed area entries
        }
}

Area* Room::find_area(const std::string& name) {
        if (name.empty()) return nullptr;
        for (auto& na : areas) {
                if (na.name == name && na.area) return na.area.get();
        }
        return nullptr;
}

bool Room::remove_area(const std::string& name) {
        if (name.empty()) {
                return false;
        }
        bool removed = false;
        try {
                if (assets_json.is_object() && assets_json.contains("areas") && assets_json["areas"].is_array()) {
                        auto& arr = assets_json["areas"];
                        for (auto it = arr.begin(); it != arr.end();) {
                                if (it->is_object() && it->value("name", std::string{}) == name) {
                                        it = arr.erase(it);
                                        removed = true;
                                } else {
                                        ++it;
                                }
                        }
                }
        } catch (...) {
                removed = false;
        }
        if (removed) {
                load_named_areas_from_json();
        }
        return removed;
}

void Room::upsert_named_area(const Area& area, const std::string& type) {
        const std::string area_name = area.get_name();
        if (area_name.empty()) {
                return;
        }

        if (!assets_json.is_object()) {
                assets_json = nlohmann::json::object();
        }
        if (!assets_json.contains("areas") || !assets_json["areas"].is_array()) {
                assets_json["areas"] = nlohmann::json::array();
        }

        SDL_Point center = room_area ? room_area->get_center()
                                     : SDL_Point{map_origin.first, map_origin.second};

        nlohmann::json entry = nlohmann::json::object();
        entry["name"] = area_name;
        if (!type.empty()) {
                entry["type"] = type;
        } else if (!area.get_type().empty()) {
                entry["type"] = area.get_type();
        }

        const auto& pts = area.get_points();
        if (!pts.empty()) {
                std::string effective_type = type.empty() ? area.get_type() : type;
                std::optional<SDL_Point> forced_anchor;
                if (should_use_room_center_anchor(effective_type, area_name)) {
                        forced_anchor = center;
                }

                update_anchor_and_points_json(entry, pts, forced_anchor);
                int orig_w = 0;
                int orig_h = 0;
                if (room_area) {
                        auto [minx, miny, maxx, maxy] = room_area->get_bounds();
                        orig_w = std::max(1, maxx - minx);
                        orig_h = std::max(1, maxy - miny);
                } else {
                        orig_w = positive_from_keys(assets_json, {"origional_width", "original_width", "max_width", "width_max", "min_width", "width_min"});
                        orig_h = positive_from_keys(assets_json, {"origional_height", "original_height", "max_height", "height_max", "min_height", "height_min"});
                        int radius = assets_json.value("radius", 0);
                        if (radius > 0) {
                                orig_w = std::max(orig_w, radius * 2);
                                orig_h = std::max(orig_h, radius * 2);
                        }
                        if (orig_w <= 0) orig_w = 1;
                        if (orig_h <= 0) orig_h = 1;
                }

                write_relative_points_json(entry, pts, center, orig_w, orig_h);
        }

        auto& arr = assets_json["areas"];
        bool replaced = false;
        for (auto& item : arr) {
                if (!item.is_object()) continue;
                if (item.value("name", std::string{}) == area_name) {
                        item = entry;
                        replaced = true;
                        break;
                }
        }
        if (!replaced) {
                arr.push_back(entry);
        }

        load_named_areas_from_json();
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
        std::string lowered_geom = geometry;
        std::transform(lowered_geom.begin(), lowered_geom.end(), lowered_geom.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
        });
        if (lowered_geom == "circle") {
                out["radius"] = std::max(0, width / 2);
        } else {
                out.erase("radius");
        }
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
        bool has_player_asset = false;
        for (const auto& uptr : assets) {
                const Asset* a = uptr.get();
                if (!a || !a->info) continue;

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
                if (a->info->type == asset_types::player) {
                        has_player_asset = true;
                }
        }
        if (is_spawn && !has_player_asset) {
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
        // Refresh cached named areas from current JSON before saving
        const_cast<Room*>(this)->load_named_areas_from_json();
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
