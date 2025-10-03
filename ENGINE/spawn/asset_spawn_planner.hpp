#pragma once

#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <filesystem>
#include <unordered_set>
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
    std::string resolve_asset_from_tag( const std::string& tag, const std::unordered_set<std::string>* banned_tags, const std::unordered_set<std::string>* banned_assets, const std::unordered_set<std::string>* candidate_tags);
    nlohmann::json* get_source_entry(int source_index, int entry_index, const std::string& key);
    void persist_sources();
    nlohmann::json root_json_;
    std::vector<nlohmann::json> source_jsons_;
    std::vector<std::string> source_paths_;
    struct SourceRef {
        int source_index = -1;
        int entry_index = -1;
        std::string key;
};
    std::vector<SourceRef> assets_provenance_;
    std::vector<bool> source_changed_;
    AssetLibrary* asset_library_;
    std::vector<SpawnInfo> spawn_queue_;
};