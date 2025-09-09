#pragma once

#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <filesystem>
#include <SDL.h>
#include "asset/asset_info.hpp"
#include "asset/asset_library.hpp"
#include "utils/area.hpp"

constexpr double REPRESENTATIVE_SPAWN_AREA = 4096.0 * 4096.0;

struct SpawnInfo {
	std::string name;
	std::string position;
	std::string spawn_id;
	SDL_Point exact_offset{0, 0};
	int exact_origin_w = 0;
	int exact_origin_h = 0;
	int quantity = 0;
	int grid_spacing = 0;
	int jitter = 0;
	int empty_grid_spaces = 0;
	SDL_Point exact_point{ -1, -1 };
	int border_shift = 0;
	int sector_center = 0;
	int sector_range = 0;
	SDL_Point perimeter_offset{0, 0};
	bool check_overlap = false;
	bool check_min_spacing = false;
	std::shared_ptr<AssetInfo> info;
};

struct BatchSpawnInfo {
	std::string name;
	int percent = 0;
};

class AssetSpawnPlanner {

	public:
    AssetSpawnPlanner(const std::vector<nlohmann::json>& json_sources,
                      const Area& area,
                      AssetLibrary& asset_library,
                      const std::vector<std::string>& source_paths = {});
    const std::vector<SpawnInfo>& get_spawn_queue() const;
    const std::vector<BatchSpawnInfo>& get_batch_spawn_assets() const;
    int get_batch_grid_spacing() const;
    int get_batch_jitter() const;

	private:
    void parse_asset_spawns(const Area& area);
    void parse_batch_assets();
    void sort_spawn_queue();
    nlohmann::json resolve_asset_from_tag(const nlohmann::json& tag_entry);
    void persist_sources();
    nlohmann::json root_json_;
    std::vector<nlohmann::json> source_jsons_;
    std::vector<std::string> source_paths_;
    std::vector<std::pair<int,int>> assets_provenance_;
    std::vector<bool> source_changed_;
    AssetLibrary* asset_library_;
    std::vector<SpawnInfo> spawn_queue_;
    std::vector<BatchSpawnInfo> batch_spawn_assets_;
    int batch_grid_spacing_ = 100;
    int batch_jitter_ = 0;
};
