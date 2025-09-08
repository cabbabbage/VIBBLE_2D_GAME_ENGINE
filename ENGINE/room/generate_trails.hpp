#pragma once

#include "Room.hpp"
#include "utils/area.hpp"
#include "asset\Asset_library.hpp"
#include <string>
#include <vector>
#include <memory>
#include <random>

class GenerateTrails {

	public:
    explicit GenerateTrails(const std::string& trail_dir);
    void set_all_rooms_reference(const std::vector<Room*>& rooms);
    std::vector<std::unique_ptr<Room>> generate_trails(
                                                           const std::vector<std::pair<Room*, Room*>>& room_pairs,
                                                           const std::vector<Area>& existing_areas,
                                                           const std::string& map_dir,
                                                           AssetLibrary* asset_lib
    );
    void find_and_connect_isolated(
                                       const std::string& map_dir,
                                       AssetLibrary* asset_lib,
                                       std::vector<Area>& existing_areas,
                                       std::vector<std::unique_ptr<Room>>& trail_rooms
    );
    void remove_connection(Room* a,
                           Room* b,
                           std::vector<std::unique_ptr<Room>>& trail_rooms,
                           std::vector<Area>& existing_areas);
    void remove_random_connection(std::vector<std::unique_ptr<Room>>& trail_rooms);
    void remove_and_connect(std::vector<std::unique_ptr<Room>>& trail_rooms,
                            std::vector<std::pair<Room*, Room*>>& illegal_connections,
                            const std::string& map_dir,
                            AssetLibrary* asset_lib,
                            std::vector<Area>& existing_areas);
    void circular_connection(std::vector<std::unique_ptr<Room>>& trail_rooms,
                             const std::string& map_dir,
                             AssetLibrary* asset_lib,
                             std::vector<Area>& existing_areas);

	private:
    std::string pick_random_asset();
    std::vector<std::string> available_assets_;
    std::vector<Room*> all_rooms_reference;
    std::vector<Area> trail_areas_;
    std::mt19937 rng_;
    bool testing = false;
    std::vector<std::pair<Room*, Room*>> illegal_connections;
};
