#pragma once

#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <filesystem>
#include <SDL.h>
#include "asset/asset_info.hpp"
#include "asset/asset_library.hpp"
#include "utils/area.hpp"
#include "spawn_info.hpp"

constexpr double REPRESENTATIVE_SPAWN_AREA = 4096.0 * 4096.0;

class AssetSpawnPlanner {

	public:
    AssetSpawnPlanner(const std::vector<nlohmann::json>& json_sources,
                      const Area& area,
                      AssetLibrary& asset_library,
                      const std::vector<std::string>& source_paths = {});
    const std::vector<SpawnInfo>& get_spawn_queue() const;

        private:
    void parse_asset_spawns(const Area& area);
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
};
