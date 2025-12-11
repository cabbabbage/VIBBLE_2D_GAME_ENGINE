#include "room.hpp"
#include "spawn/asset_spawner.hpp"
#include "asset/asset_types.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/dev_controls_persistence.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <algorithm>
#include <functional>
#include <iostream>
#include <cmath>
#include <optional>
#include <string>
#include "utils/grid.hpp"
#include "utils/string_utils.hpp"
#include "utils/ranged_color.hpp"
using json = nlohmann::json;

namespace {

using vibble::strings::to_lower_copy;

RoomAreaSerialization::Kind parse_kind_value(const std::string& value) {
        if (value.empty()) return RoomAreaSerialization::Kind::Unknown;
        std::string lowered = to_lower_copy(value);
        if (lowered.find("spawn") != std::string::npos) {
                return RoomAreaSerialization::Kind::Spawn;
        }
        if (lowered.find("trigger") != std::string::npos) {
                return RoomAreaSerialization::Kind::Trigger;
        }
        return RoomAreaSerialization::Kind::Unknown;
}

SDL_Point min_corner_anchor(const std::vector<SDL_Point>& points) {
        if (points.empty()) return SDL_Point{0, 0};
        SDL_Point anchor{points.front().x, points.front().y};
        for (const auto& p : points) {
                anchor.x = std::min(anchor.x, p.x);
                anchor.y = std::min(anchor.y, p.y);
        }
        return anchor;
}

}

namespace RoomAreaSerialization {

Kind infer_kind_from_strings(const std::string& kind_value,
                             const std::string& type_hint,
                             const std::string& name_hint) {
        if (Kind parsed = parse_kind_value(kind_value); parsed != Kind::Unknown) {
                return parsed;
        }
        if (Kind parsed = parse_kind_value(type_hint); parsed != Kind::Unknown) {
                return parsed;
        }
        if (Kind parsed = parse_kind_value(name_hint); parsed != Kind::Unknown) {
                return parsed;
        }
        return Kind::Unknown;
}

Kind infer_kind_from_entry(const nlohmann::json& entry,
                           const std::string& type_hint,
                           const std::string& name_hint) {
        std::string provided;
        if (entry.contains("kind") && entry["kind"].is_string()) {
                provided = entry["kind"].get<std::string>();
        }
        return infer_kind_from_strings(provided, type_hint, name_hint);
}

std::string to_string(Kind kind) {
        switch (kind) {
        case Kind::Spawn:   return "Spawn";
        case Kind::Trigger: return "Trigger";
        case Kind::Unknown: default: return std::string{};
        }
}

bool is_supported_kind(Kind kind) {
        return kind == Kind::Spawn || kind == Kind::Trigger;
}

AnchorData resolve_anchor(const nlohmann::json& entry,
                          SDL_Point default_anchor,
                          Kind kind) {
        AnchorData data;
        data.world = default_anchor;
        data.relative_offset = SDL_Point{0, 0};
        data.relative_to_center = is_supported_kind(kind);

        SDL_Point stored{0, 0};
        bool has_anchor = false;
        if (entry.contains("anchor") && entry["anchor"].is_object()) {
                stored.x = entry["anchor"].value("x", 0);
                stored.y = entry["anchor"].value("y", 0);
                has_anchor = true;
        }

        bool has_flag = entry.contains("anchor_relative_to_center");
        bool wants_relative = data.relative_to_center;
        if (has_flag && entry["anchor_relative_to_center"].is_boolean()) {
                wants_relative = entry["anchor_relative_to_center"].get<bool>();
        } else if (!has_flag && data.relative_to_center) {

                stored = SDL_Point{0, 0};
                wants_relative = true;
        }

        if (wants_relative && data.relative_to_center) {
                data.relative_offset = stored;
                data.world.x = default_anchor.x + stored.x;
                data.world.y = default_anchor.y + stored.y;
                data.relative_to_center = true;
        } else if (has_anchor) {
                data.world = stored;
                data.relative_offset.x = data.world.x - default_anchor.x;
                data.relative_offset.y = data.world.y - default_anchor.y;
                data.relative_to_center = false;
        } else {
                data.relative_offset = SDL_Point{0, 0};
                data.world = default_anchor;
        }

        return data;
}

void write_anchor(nlohmann::json& entry,
                  const AnchorData& anchor,
                  Kind kind) {
        if (is_supported_kind(kind) && anchor.relative_to_center) {
                entry["anchor"] = nlohmann::json::object({
                        {"x", anchor.relative_offset.x},
                        {"y", anchor.relative_offset.y}
                });
                entry["anchor_relative_to_center"] = true;
        } else {
                entry["anchor"] = nlohmann::json::object({
                        {"x", anchor.world.x},
                        {"y", anchor.world.y}
                });
                entry.erase("anchor_relative_to_center");
        }
}

SDL_Point choose_anchor(Kind kind,
                        SDL_Point default_anchor,
                        const std::vector<SDL_Point>& world_points) {
        if (!world_points.empty() && !is_supported_kind(kind)) {
                return min_corner_anchor(world_points);
        }
        return default_anchor;
}

std::vector<SDL_Point> decode_relative_points(const nlohmann::json& entry) {
        std::vector<SDL_Point> pts;
        if (!entry.contains("points") || !entry["points"].is_array()) {
                return pts;
        }
        pts.reserve(entry["points"].size());
        for (const auto& point : entry["points"]) {
                if (!point.is_object()) continue;
                int x = point.value("x", 0);
                int y = point.value("y", 0);
                pts.push_back(SDL_Point{x, y});
        }
        return pts;
}

std::vector<SDL_Point> decode_points(const nlohmann::json& entry, SDL_Point anchor) {
        std::vector<SDL_Point> pts;
        auto rel = decode_relative_points(entry);
        pts.reserve(rel.size());
        for (const auto& point : rel) {
                pts.push_back(SDL_Point{anchor.x + point.x, anchor.y + point.y});
        }
        return pts;
}

nlohmann::json encode_points(const std::vector<SDL_Point>& points, SDL_Point anchor) {
        nlohmann::json arr = nlohmann::json::array();
        arr.get_ref<nlohmann::json::array_t&>().reserve(points.size());
        for (const auto& p : points) {
                arr.push_back({ {"x", p.x - anchor.x}, {"y", p.y - anchor.y} });
        }
        return arr;
}

}

Room::Room(Point origin,
           std::string type_,
           const std::string& room_def_name,
           Room* parent,
           const std::string& manifest_context,
           AssetLibrary* asset_lib,
           Area* precomputed_area,
           nlohmann::json* room_data,
           const nlohmann::json* map_assets_data,
           const MapGridSettings& grid_settings,
           double map_radius,
           const std::string& data_section,
           nlohmann::json* map_info_root,
           devmode::core::ManifestStore* manifest_store,
           std::string manifest_map_id,
           Room::ManifestWriter manifest_writer)
    : map_origin(origin),
parent(parent),
room_name(room_def_name),
room_directory(manifest_context.empty() ? data_section : manifest_context + "::" + data_section),
json_path((manifest_context.empty() ? data_section : manifest_context + "::" + data_section) + "::" + room_def_name),
room_area(nullptr),
type(type_),
room_data_ptr_(room_data),
map_grid_settings_(grid_settings),
manifest_context_(manifest_context),
data_section_(data_section),
manifest_writer_(std::move(manifest_writer))
{
        (void)map_assets_data;
        if (testing) {
                std::cout << "[Room] Created room: " << room_name
                << " at (" << origin.first << ", " << origin.second << ")"
                << (parent ? " with parent\n" : " (no parent)\n");
        }
        if (!manifest_map_id.empty()) {
                manifest_map_id_ = std::move(manifest_map_id);
        }
        manifest_store_ = manifest_store;
        map_info_root_ = map_info_root;
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

        inherits_map_assets_ = assets_json.value("inherits_map_assets", false);

        load_named_areas_from_json();
        int map_radius_int = static_cast<int>(std::round(map_radius));
        if (map_radius_int < 0) map_radius_int = 0;
        int map_w = map_radius_int * 2;
        int map_h = map_radius_int * 2;
        if (precomputed_area) {
                if (testing) {
                        std::cout << "[Room] Using precomputed area for: " << room_name << "\n";
                }
                room_area = std::make_unique<Area>(room_name, precomputed_area->get_points(), 3);
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
                room_area = std::make_unique<Area>(room_name, SDL_Point{map_origin.first, map_origin.second}, width, height, geometry, edge_smoothness, map_w, map_h, 3);
                if (room_area) room_area->set_type("room");
	}
        std::vector<json> json_sources;
        std::vector<AssetSpawnPlanner::SourceContext> source_contexts;

        auto push_payload = [this](const std::function<void(nlohmann::json&)>& mutate) {
                if (!mutate) {
                        return;
                }
                if (map_info_root_) {
                        if (!map_info_root_->is_object()) {
                                *map_info_root_ = nlohmann::json::object();
                        }
                        mutate(*map_info_root_);
                }
                auto apply_mutation = [&](nlohmann::json payload) {
                        if (!payload.is_object()) {
                                payload = nlohmann::json::object();
                        }
                        mutate(payload);
                        return payload;
};
                if (manifest_store_ && !manifest_map_id_.empty()) {
                        nlohmann::json payload;
                        if (map_info_root_) {
                                payload = *map_info_root_;
                        } else if (const nlohmann::json* entry = manifest_store_->find_map_entry(manifest_map_id_)) {
                                payload = *entry;
                        }
                        payload = apply_mutation(std::move(payload));
                        if (devmode::persist_map_manifest_entry(*manifest_store_, manifest_map_id_, payload, std::cerr)) {
                                manifest_store_->flush();
                        }
                } else if (manifest_writer_ && !manifest_map_id_.empty()) {
                        nlohmann::json payload = map_info_root_ ? *map_info_root_ : nlohmann::json::object();
                        payload = apply_mutation(std::move(payload));
                        manifest_writer_(manifest_map_id_, payload);
                }
};

        json_sources.push_back(assets_json);
        AssetSpawnPlanner::SourceContext room_context;
        room_context.persist = [this, push_payload](const nlohmann::json& updated) {
                assets_json = updated;
                if (room_data_ptr_) {
                        *room_data_ptr_ = assets_json;
                }
                push_payload([&](nlohmann::json& payload) {
                        nlohmann::json& section = payload[data_section_];
                        if (!section.is_object()) {
                                section = nlohmann::json::object();
                        }
                        section[room_name] = assets_json;
                });
};
        source_contexts.push_back(room_context);

        planner = std::make_unique<AssetSpawnPlanner>( json_sources, *room_area, *asset_lib, source_contexts );
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

std::pair<int, int> Room::current_room_dimensions() const {
        if (room_area) {
                int w = 0;
                int h = 0;
                bounds_to_size(room_area->get_bounds(), w, h);
                return {w, h};
        }

        int min_w = assets_json.value("min_width", 0);
        int max_w = assets_json.value("max_width", min_w);
        int min_h = assets_json.value("min_height", 0);
        int max_h = assets_json.value("max_height", min_h);
        int width = std::max(min_w, max_w);
        int height = std::max(min_h, max_h);

        if ((width <= 0 || height <= 0) && assets_json.contains("radius")) {
                int radius = assets_json.value("radius", 0);
                if (radius > 0) {
                        int diameter = radius * 2;
                        if (width <= 0) width = diameter;
                        if (height <= 0) height = diameter;
                }
        }

        return {width, height};
}

void Room::load_named_areas_from_json() {
        areas.clear();
        try {
                if (!assets_json.is_object()) return;
                if (!assets_json.contains("areas") || !assets_json["areas"].is_array()) return;

                SDL_Point default_anchor = room_area ? room_area->get_center()
                                                     : SDL_Point{map_origin.first, map_origin.second};

                auto room_dims = current_room_dimensions();

                for (auto& item : assets_json["areas"]) {
                        if (!item.is_object()) continue;
                        const std::string name = item.value("name", std::string{});
                        if (name.empty()) continue;
                        const std::string type = item.value("type", std::string{});

                        RoomAreaSerialization::Kind kind =
                                RoomAreaSerialization::infer_kind_from_entry(item, type, name);
                        if (!RoomAreaSerialization::is_supported_kind(kind)) {
                                std::cerr << "[Room] Ignoring area '" << name << "' with unsupported kind '"
                                          << item.value("kind", std::string{}) << "'. Rooms support Spawn/Trigger only.\n";
                                continue;
                        }

                        auto anchor = RoomAreaSerialization::resolve_anchor(item, default_anchor, kind);

                        const int resolution = vibble::grid::clamp_resolution(item.value("resolution", 2));
                        bool scale_to_room = item.value("scale_to_room", false);
                        const int stored_width = item.value("origional_width", 0);
                        const int stored_height = item.value("origional_height", 0);

                        auto relative_points = RoomAreaSerialization::decode_relative_points(item);

                        std::vector<SDL_Point> pts;
                        const int current_width = room_dims.first;
                        const int current_height = room_dims.second;
                        const bool can_scale = scale_to_room && stored_width > 0 && stored_height > 0 &&
                                               current_width > 0 && current_height > 0;
                        int persisted_width = stored_width;
                        int persisted_height = stored_height;

                        auto scale_component = [](int value, double factor) {
                                return static_cast<int>(std::llround(static_cast<double>(value) * factor));
};

                        if (can_scale) {
                                const double sx = static_cast<double>(current_width) / static_cast<double>(stored_width);
                                const double sy = static_cast<double>(current_height) / static_cast<double>(stored_height);

                                if (anchor.relative_to_center) {
                                        anchor.relative_offset.x = scale_component(anchor.relative_offset.x, sx);
                                        anchor.relative_offset.y = scale_component(anchor.relative_offset.y, sy);
                                        anchor.world.x = default_anchor.x + anchor.relative_offset.x;
                                        anchor.world.y = default_anchor.y + anchor.relative_offset.y;
                                }

                                pts.reserve(relative_points.size());
                                for (const auto& rel : relative_points) {
                                        const int dx = scale_component(rel.x, sx);
                                        const int dy = scale_component(rel.y, sy);
                                        pts.push_back(SDL_Point{anchor.world.x + dx, anchor.world.y + dy});
                                }
                                persisted_width = current_width;
                                persisted_height = current_height;
                        } else {
                                pts = RoomAreaSerialization::decode_points(item, anchor.world);
                        }

                        if (pts.size() < 3) continue;

                        RoomAreaSerialization::write_anchor(item, anchor, kind);
                        item["points"] = RoomAreaSerialization::encode_points(pts, anchor.world);
                        item["resolution"] = resolution;
                        item.erase("relative_points");
                        item.erase("original_width");
                        item.erase("original_height");
                        if (scale_to_room) {
                                item["scale_to_room"] = true;
                                if (persisted_width > 0) item["origional_width"] = persisted_width;
                                if (persisted_height > 0) item["origional_height"] = persisted_height;
                        } else {
                                item.erase("scale_to_room");
                        }

                        NamedArea na;
                        na.name = name;
                        na.type = type;
                        na.kind = RoomAreaSerialization::to_string(kind);
                        na.area = std::make_unique<Area>(name, pts, resolution);
                        if (na.area) {
                                na.area->set_resolution(resolution);
                        }
                        if (na.area) na.area->set_type(type);
                        na.scale_to_room = scale_to_room;
                        na.original_room_width = persisted_width;
                        na.original_room_height = persisted_height;

                        try {
                                if (item.contains("origin_room") && item["origin_room"].is_object()) {
                                        const auto& orj = item["origin_room"];
                                        NamedArea::OriginRoomMeta meta;
                                        meta.name = orj.value("name", std::string{});
                                        meta.width = orj.value("width", 0);
                                        meta.height = orj.value("height", 0);
                                        if (orj.contains("anchor") && orj["anchor"].is_object()) {
                                                meta.anchor.x = orj["anchor"].value("x", 0);
                                                meta.anchor.y = orj["anchor"].value("y", 0);
                                        }
                                        meta.anchor_relative_to_center = orj.value("anchor_relative_to_center", false);
                                        na.origin_room = meta;
                                } else {

                                        nlohmann::json meta = nlohmann::json::object();
                                        meta["name"] = room_name;
                                        meta["width"] = room_dims.first;
                                        meta["height"] = room_dims.second;
                                        meta["anchor"] = nlohmann::json::object({ {"x", anchor.world.x}, {"y", anchor.world.y} });
                                        meta["anchor_relative_to_center"] = anchor.relative_to_center;
                                        item["origin_room"] = meta;
                                        NamedArea::OriginRoomMeta store;
                                        store.name = room_name;
                                        store.width = room_dims.first;
                                        store.height = room_dims.second;
                                        store.anchor = anchor.world;
                                        store.anchor_relative_to_center = anchor.relative_to_center;
                                        na.origin_room = store;
                                }
                        } catch (...) {

                        }
                        areas.push_back(std::move(na));
                }
        } catch (...) {

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

bool Room::rename_area(const std::string& old_name, const std::string& new_name) {
        if (old_name.empty() || new_name.empty()) {
                return false;
        }
        if (old_name == new_name) {
                return true;
        }
        for (const auto& na : areas) {
                if (na.name == new_name) {
                        return false;
                }
        }
        bool renamed = false;
        try {
                if (assets_json.is_object() && assets_json.contains("areas") && assets_json["areas"].is_array()) {
                        for (auto& entry : assets_json["areas"]) {
                                if (entry.is_object() && entry.value("name", std::string{}) == old_name) {
                                        entry["name"] = new_name;
                                        renamed = true;
                                }
                        }
                }
        } catch (...) {
                return false;
        }
        if (!renamed) {
                return false;
        }
        load_named_areas_from_json();
        return true;
}

void Room::upsert_named_area(const Area& area,
                             bool scale_to_room,
                             int original_room_width,
                             int original_room_height) {
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

        const auto& pts = area.get_points();
        if (pts.size() < 3) {
                return;
        }

        std::string effective_type = area.get_type();
        nlohmann::json* existing_entry = nullptr;
        std::string existing_kind;
        for (auto& item : assets_json["areas"]) {
                if (!item.is_object()) continue;
                if (item.value("name", std::string{}) == area_name) {
                        existing_entry = &item;
                        if (effective_type.empty()) {
                                effective_type = item.value("type", std::string{});
                        }
                        existing_kind = item.value("kind", std::string{});
                        break;
                }
        }

        RoomAreaSerialization::Kind kind =
                RoomAreaSerialization::infer_kind_from_strings(existing_kind, effective_type, area_name);
        if (!RoomAreaSerialization::is_supported_kind(kind)) {
                std::cerr << "[Room] Refusing to store area '" << area_name
                          << "' with unsupported kind (" << existing_kind << ").\n";
                return;
        }

        SDL_Point default_anchor = room_area ? room_area->get_center()
                                             : SDL_Point{map_origin.first, map_origin.second};
        RoomAreaSerialization::AnchorData anchor;
        anchor.world = RoomAreaSerialization::choose_anchor(kind, default_anchor, pts);
        anchor.relative_offset = SDL_Point{ anchor.world.x - default_anchor.x,
                                            anchor.world.y - default_anchor.y };
        anchor.relative_to_center = RoomAreaSerialization::is_supported_kind(kind);
        if (existing_entry) {
                anchor = RoomAreaSerialization::resolve_anchor(*existing_entry, default_anchor, kind);
        }

        bool final_scale_flag = scale_to_room;

        int stored_width = original_room_width;
        int stored_height = original_room_height;
        if (existing_entry) {
                if (stored_width <= 0) stored_width = existing_entry->value("origional_width", 0);
                if (stored_height <= 0) stored_height = existing_entry->value("origional_height", 0);
        }
        if (final_scale_flag) {
                auto dims = current_room_dimensions();
                if (stored_width <= 0) stored_width = dims.first;
                if (stored_height <= 0) stored_height = dims.second;
        }

        nlohmann::json entry = nlohmann::json::object({
                {"name", area_name},
                {"points", RoomAreaSerialization::encode_points(pts, anchor.world)},
        });
        if (!effective_type.empty()) {
                entry["type"] = effective_type;
        }
        entry["kind"] = RoomAreaSerialization::to_string(kind);
        RoomAreaSerialization::write_anchor(entry, anchor, kind);

        entry["resolution"] = vibble::grid::clamp_resolution(area.resolution());

        if (final_scale_flag) {
                entry["scale_to_room"] = true;
                if (stored_width > 0) entry["origional_width"] = stored_width;
                if (stored_height > 0) entry["origional_height"] = stored_height;
        }

        try {
                auto dims = current_room_dimensions();
                nlohmann::json origin_meta = nlohmann::json::object();
                origin_meta["name"] = room_name;
                origin_meta["width"] = std::max(0, dims.first);
                origin_meta["height"] = std::max(0, dims.second);
                origin_meta["anchor"] = nlohmann::json::object({ {"x", anchor.world.x}, {"y", anchor.world.y} });
                origin_meta["anchor_relative_to_center"] = anchor.relative_to_center;
                entry["origin_room"] = std::move(origin_meta);
        } catch (...) {

        }

        if (existing_entry) {
                *existing_entry = entry;
        } else {
                assets_json["areas"].push_back(entry);
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
        if (!assets_json.is_object()) {
                assets_json = nlohmann::json::object();
        }
        if (!assets_json.contains("spawn_groups") || !assets_json["spawn_groups"].is_array()) {
                assets_json["spawn_groups"] = nlohmann::json::array();
        }
        return assets_json;
}

bool Room::is_spawn_room() const {
        return assets_json.value("is_spawn", false);
}

SDL_Color Room::display_color() const {
        static constexpr SDL_Color kFallback{120, 170, 235, 255};
        if (!assets_json.is_object()) {
                return kFallback;
        }
        auto it = assets_json.find("display_color");
        if (it == assets_json.end()) {
                return kFallback;
        }
        if (auto parsed = utils::color::color_from_json(*it)) {
                SDL_Color color = *parsed;
                color.a = 255;
                return color;
        }
        return kFallback;
}

void Room::rename(const std::string& new_name, nlohmann::json& map_info_json) {
        if (new_name.empty() || new_name == room_name) {
                if (!room_data_ptr_ && map_info_json.is_object()) {
                        nlohmann::json& section = map_info_json[data_section_];
                        if (section.is_object() && section.contains(room_name)) {
                                room_data_ptr_ = &section[room_name];
                        }
                }
                return;
        }

        if (!map_info_json.is_object()) {
                map_info_json = nlohmann::json::object();
        }

        nlohmann::json& section = map_info_json[data_section_];
        if (!section.is_object()) {
                section = nlohmann::json::object();
        }

        if (room_data_ptr_) {
                assets_json = *room_data_ptr_;
        } else {
                auto it = section.find(room_name);
                if (it != section.end()) {
                        assets_json = *it;
                }
        }

        assets_json["name"] = new_name;

        section[new_name] = assets_json;
        nlohmann::json* new_entry = &section[new_name];

        if (section.contains(room_name)) {
                section.erase(room_name);
        }

        room_name = new_name;
        room_data_ptr_ = new_entry;
        assets_json = *room_data_ptr_;

        if (!json_path.empty()) {
                size_t pos = json_path.rfind("::");
                if (pos != std::string::npos) {
                        json_path = json_path.substr(0, pos + 2) + room_name;
                } else {
                        json_path = room_name;
                }
        }

        if (room_area) {
                room_area->set_name(room_name);
        }

        for (auto& owned : assets) {
                if (owned) {
                        owned->set_owning_room_name(room_name);
                }
        }
}

void Room::set_manifest_store(devmode::core::ManifestStore* store,
                              std::string map_id,
                              nlohmann::json* map_info_root,
                              Room::ManifestWriter manifest_writer) {
        manifest_store_ = store;
        manifest_map_id_ = std::move(map_id);
        map_info_root_ = map_info_root;
        if (manifest_writer) {
                manifest_writer_ = std::move(manifest_writer);
        }
}

void Room::save_assets_json() const {

        const_cast<Room*>(this)->load_named_areas_from_json();
        if (room_data_ptr_) {
                *room_data_ptr_ = assets_json;
        }
        if (map_info_root_ && map_info_root_->is_object()) {
                nlohmann::json& section = (*map_info_root_)[data_section_];
                if (!section.is_object()) {
                        section = nlohmann::json::object();
                }
                section[room_name] = assets_json;
        }
        if (manifest_store_ && !manifest_map_id_.empty()) {
                nlohmann::json payload;
                if (map_info_root_) {
                        payload = *map_info_root_;
                } else if (const nlohmann::json* entry = manifest_store_->find_map_entry(manifest_map_id_)) {
                        payload = *entry;
                }
                if (!payload.is_object()) {
                        payload = nlohmann::json::object();
                }
                nlohmann::json& section = payload[data_section_];
                if (!section.is_object()) {
                        section = nlohmann::json::object();
                }
                section[room_name] = assets_json;
                if (devmode::persist_map_manifest_entry(*manifest_store_, manifest_map_id_, payload, std::cerr)) {
                        manifest_store_->flush();
                }
                return;
        }
        if (manifest_writer_ && !manifest_map_id_.empty()) {
                nlohmann::json payload = nlohmann::json::object();
                if (map_info_root_) {
                        payload = *map_info_root_;
                }
                if (!payload.is_object()) {
                        payload = nlohmann::json::object();
                }
                nlohmann::json& section = payload[data_section_];
                if (!section.is_object()) {
                        section = nlohmann::json::object();
                }
                section[room_name] = assets_json;
                manifest_writer_(manifest_map_id_, payload);
        }
        try {
                std::cout << "[Room] Autosaved assets for room: " << room_name << "\n";
        } catch (...) {
        }
}
