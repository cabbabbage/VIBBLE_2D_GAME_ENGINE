#pragma once

#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <filesystem>
#include "asset/asset_info.hpp"
#include "asset/asset_library.hpp"

constexpr double REPRESENTATIVE_SPAWN_AREA = 4096.0 * 4096.0;

struct SpawnInfo {
    std::string name;
    std::string position;
    int quantity = 0;
    int grid_spacing = 0;
    int jitter = 0;
    int empty_grid_spaces = 0;
    int ep_x = -1;
    int ep_y = -1;
    int exact_x = -1;
    int exact_y = -1;
    // Exact spawn via scaled displacement
    int orig_room_width = -1;
    int orig_room_height = -1;
    int disp_x = 0;  // displacement from original center (pixels)
    int disp_y = 0;  // displacement from original center (pixels)
    int border_shift = 0;
    int sector_center = 0;
    int sector_range = 0;
    int perimeter_x_offset = 0;
    int perimeter_y_offset = 0;
    bool check_overlap = false;
    bool check_min_spacing = false;
    std::shared_ptr<AssetInfo> info;
    std::string asset_id;
};

struct BatchSpawnInfo {
    std::string name;
    int percent = 0;
    std::string asset_id;
};

class AssetSpawnPlanner {
public:
    AssetSpawnPlanner(const std::vector<nlohmann::json>& json_sources,
                      double area,
                      AssetLibrary& asset_library);

    const std::vector<SpawnInfo>& get_spawn_queue() const;
    const std::vector<BatchSpawnInfo>& get_batch_spawn_assets() const;

    int get_batch_grid_spacing() const;
    int get_batch_jitter() const;

private:
    void parse_asset_spawns(double area);
    void parse_batch_assets();
    void sort_spawn_queue();
    nlohmann::json resolve_asset_from_tag(const nlohmann::json& tag_entry);

    nlohmann::json root_json_;
    AssetLibrary* asset_library_;
    std::vector<SpawnInfo> spawn_queue_;
    std::vector<BatchSpawnInfo> batch_spawn_assets_;
    int batch_grid_spacing_ = 100;
    int batch_jitter_ = 0;
};
