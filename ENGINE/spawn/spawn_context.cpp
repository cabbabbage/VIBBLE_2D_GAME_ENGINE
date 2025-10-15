#include "spawn_context.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_library.hpp"
#include "spawn/asset_spawn_planner.hpp"
#include "spawn/asset_spawner.hpp"
#include "utils/area.hpp"
#include "utils/area_helpers.hpp"
namespace fs = std::filesystem;

SpawnContext::SpawnContext(std::mt19937& rng,
                           Check& checker,
                           std::vector<Area>& exclusion_zones,
                           std::unordered_map<std::string, std::shared_ptr<AssetInfo>>& asset_info_library,
                           std::vector<std::unique_ptr<Asset>>& all,
                           AssetLibrary* asset_library,
                           MapGrid* grid)
: rng_(rng),
checker_(checker),
exclusion_zones_(exclusion_zones),
asset_info_library_(asset_info_library),
all_(all),
asset_library_(asset_library),
grid_(grid)
{}

SpawnContext::Point SpawnContext::get_area_center(const Area& area) const {
	return area.get_center();
}

SpawnContext::Point SpawnContext::get_point_within_area(const Area& area) {
        auto [minx, miny, maxx, maxy] = area.get_bounds();
        for (int i = 0; i < 100; ++i) {
                int x = std::uniform_int_distribution<int>(minx, maxx)(rng_);
                int y = std::uniform_int_distribution<int>(miny, maxy)(rng_);
                if (area.contains_point(SDL_Point{ x, y })) return SDL_Point{ x, y };
        }
        return SDL_Point{0, 0};
}

Asset* SpawnContext::spawnAsset(const std::string& name,
                                const std::shared_ptr<AssetInfo>& info,
                                const Area& area,
                                SDL_Point pos,
                                int depth,
                                Asset* parent,
                                const std::string& spawn_id,
                                const std::string& spawn_method)
{

        if (clip_area_ && !clip_area_->contains_point(pos)) {
                return nullptr;
        }
        auto assetPtr = std::make_unique<Asset>(info, area, pos, depth, parent, spawn_id, spawn_method);
        Asset* raw = assetPtr.get();
        all_.push_back(std::move(assetPtr));
        if (raw->info && !raw->info->children.empty()) {
                std::vector<ChildInfo*> shuffled_children;
                for (auto& child : raw->info->children) {
                        shuffled_children.push_back(&child);
                }
                std::random_device rd;
                std::mt19937 g(rd());
                std::shuffle(shuffled_children.begin(), shuffled_children.end(), g);
                for (auto* childInfo : shuffled_children) {
                        Area* base_area = raw->info->find_area(childInfo->area_name);
                        if (!base_area) {
                                continue;
                        }
                        nlohmann::json j;
                        bool have_inline = (childInfo->inline_assets.is_array() && !childInfo->inline_assets.empty());
                        if (have_inline) {
                                j["spawn_groups"] = childInfo->inline_assets;
                        } else {
                                const auto& childJsonPath = childInfo->json_path;
                                if (childJsonPath.empty()) {
                                        continue;
                                }
                                if (!fs::exists(childJsonPath)) {
                                        std::cerr << "[Spawn]  Child JSON not found: " << childJsonPath << "\n";
                                        continue;
                                }
                                try {
                                        std::ifstream in(childJsonPath);
                                        in >> j;
                                } catch (const std::exception& e) {
                                        std::cerr << "[Spawn]  Failed to parse child JSON: "
                                                  << childJsonPath << " | " << e.what() << "\n";
                                        continue;
                                }
                        }
                        Area childArea = area_helpers::make_world_area(*raw->info, *base_area, raw->pos, raw->flipped);
                        AssetSpawnPlanner childPlanner(std::vector<nlohmann::json>{ j },
                                                       childArea,
                                                       *asset_library_,
                                                       std::vector<std::string>{});
                        AssetSpawner childSpawner(asset_library_, exclusion_zones_);
                        childSpawner.spawn_children(childArea, &childPlanner);
                        auto kids = childSpawner.extract_all_assets();
                        for (auto& uptr : kids) {
                                if (!uptr || !uptr->info) continue;
                                uptr->set_z_offset(childInfo->z_offset);
                                uptr->parent = raw;
                                uptr->set_hidden(false);

                                raw->children.push_back(uptr.get());
                                all_.push_back(std::move(uptr));
                        }
                }
        }
	return raw;
}
