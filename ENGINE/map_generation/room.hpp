#pragma once

#include "utils/area.hpp"
#include "asset/asset_library.hpp"
#include "spawn/asset_spawn_planner.hpp"
#include "asset/Asset.hpp"
#include "utils/map_grid_settings.hpp"

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <utility>
#include <tuple>
#include <nlohmann/json.hpp>

#include <SDL.h>

namespace RoomAreaSerialization {
enum class Kind { Spawn, Trigger, Unknown };

Kind infer_kind_from_entry(const nlohmann::json& entry, const std::string& type_hint, const std::string& name_hint);
Kind infer_kind_from_strings(const std::string& kind_value, const std::string& type_hint, const std::string& name_hint);
std::string to_string(Kind kind);
bool is_supported_kind(Kind kind);
struct AnchorData {
        SDL_Point world{0, 0};
        SDL_Point relative_offset{0, 0};
        bool      relative_to_center = false;
};
SDL_Point choose_anchor(Kind kind, SDL_Point default_anchor, const std::vector<SDL_Point>& world_points);
std::vector<SDL_Point> decode_points(const nlohmann::json& entry, SDL_Point anchor);
nlohmann::json encode_points(const std::vector<SDL_Point>& points, SDL_Point anchor);
AnchorData resolve_anchor(const nlohmann::json& entry, SDL_Point default_anchor, Kind kind);
void write_anchor(nlohmann::json& entry, const AnchorData& anchor, Kind kind);
}

class Room {

        public:
    typedef std::pair<int, int> Point;
    Room(Point origin, std::string type_, const std::string& room_def_name, Room* parent, const std::string& map_dir, const std::string& map_info_path, AssetLibrary* asset_lib, Area* precomputed_area, nlohmann::json* room_data, const nlohmann::json* map_assets_data, const MapGridSettings& grid_settings, double map_radius, const std::string& data_section);
    void set_sibling_left(Room* left_room);
    void set_sibling_right(Room* right_room);
    void add_connecting_room(Room* room);
    void remove_connecting_room(Room* room);
    void set_scale(double s);
    void add_room_assets(std::vector<std::unique_ptr<Asset>> new_assets);
    std::vector<std::unique_ptr<Asset>>&& get_room_assets();
    void set_layer(int value);
    const MapGridSettings& map_grid_settings() const { return map_grid_settings_; }
    Point map_origin;
    double scale_ = 1.0;
    std::string room_name;
    std::string room_directory;
    std::string map_path;
    std::string json_path;
    Room* parent = nullptr;
    Room* left_sibling = nullptr;
    Room* right_sibling = nullptr;
    int layer = -1;
    bool testing = false;
    std::vector<Room*> children;
    std::vector<Room*> connected_rooms;
    std::vector<std::unique_ptr<Asset>> assets;
    std::unique_ptr<Area> room_area;
    std::unique_ptr<AssetSpawnPlanner> planner;
    std::string type;
    nlohmann::json create_static_room_json(std::string name);
    nlohmann::json& assets_data();
    void save_assets_json() const;
    bool is_spawn_room() const;
    void rename(const std::string& new_name, nlohmann::json& map_info_json);

    struct NamedArea {
        std::string name;
        std::string type;
        std::string kind;
        std::unique_ptr<Area> area;
};

    std::vector<NamedArea> areas;

    Area* find_area(const std::string& name);
    bool remove_area(const std::string& name);
    bool rename_area(const std::string& old_name, const std::string& new_name);
    void upsert_named_area(const Area& area, const std::string& type);

        private:
    nlohmann::json assets_json;
    nlohmann::json* room_data_ptr_ = nullptr;
    const nlohmann::json* map_assets_data_ptr_ = nullptr;
    MapGridSettings map_grid_settings_{};
    std::string map_info_path_;
    std::string data_section_;
    int clamp_int(int v, int lo, int hi) const;
    void bounds_to_size(const std::tuple<int,int,int,int>& b, int& w, int& h) const;
    void load_named_areas_from_json();
};
