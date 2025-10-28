#include "spawn_context.hpp"
#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_library.hpp"
#include "spawn/asset_spawn_planner.hpp"
#include "spawn/asset_spawner.hpp"
#include "utils/area.hpp"
#include "utils/area_helpers.hpp"
#include "utils/log.hpp"
#include "utils/map_grid_settings.hpp"
SpawnContext::SpawnContext(std::mt19937& rng,
                           Check& checker,
                           std::vector<Area>& exclusion_zones,
                           std::unordered_map<std::string, std::shared_ptr<AssetInfo>>& asset_info_library,
                           std::vector<std::unique_ptr<Asset>>& all,
                           AssetLibrary* asset_library,
                           vibble::grid::Grid& grid,
                           vibble::grid::Occupancy* occupancy)
: rng_(rng),
checker_(checker),
exclusion_zones_(exclusion_zones),
asset_info_library_(asset_info_library),
all_(all),
asset_library_(asset_library),
grid_(grid),
occupancy_(occupancy),
spawn_resolution_(occupancy ? occupancy->resolution() : grid_.default_resolution()),
map_grid_settings_(MapGridSettings::defaults())
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

        if (clip_area_ && !position_allowed(*clip_area_, pos)) {
                return nullptr;
        }
        auto assetPtr = std::make_unique<Asset>(info, area, pos, depth, parent, spawn_id, spawn_method, spawn_resolution_);
        Asset* raw = assetPtr.get();
        all_.push_back(std::move(assetPtr));
        if (raw->info && !raw->info->asset_children.empty()) {
                const std::string parent_name = raw->info ? raw->info->name : std::string{"<null>"};
                vibble::log::debug(std::string{"[Spawn] Parent asset '"} + parent_name +
                                   "' has " + std::to_string(raw->info->asset_children.size()) +
                                   " child spawn group(s)");
                std::unordered_map<std::string, Area> resolved_child_areas;
                for (const auto& named : raw->info->areas) {
                        if (!named.area) {
                                continue;
                        }
                        try {
                                Area world_area = raw->get_area(named.name);
                                if (world_area.get_points().empty()) {
                                        continue;
                                }
                                resolved_child_areas.insert_or_assign(named.name, std::move(world_area));
                        } catch (...) {
                                continue;
                        }
                }
                std::vector<ChildInfo*> shuffled_asset_children;
                for (auto& asset_child_info : raw->info->asset_children) {
                        shuffled_asset_children.push_back(&asset_child_info);
                }
                std::random_device rd;
                std::mt19937 g(rd());
                std::shuffle(shuffled_asset_children.begin(), shuffled_asset_children.end(), g);
                for (auto* asset_child_info : shuffled_asset_children) {
                        Area childArea = raw->get_area(asset_child_info->area_name);
                        if (childArea.get_points().empty()) {
                                vibble::log::debug(std::string{"[Spawn] Skipping child area '"} +
                                                   asset_child_info->area_name + "' for parent '" + parent_name +
                                                   "': resolved area has no points");
                                continue;
                        }
                        nlohmann::json j;
                        if (asset_child_info->spawn_group.is_array()) {
                                const auto& arr = asset_child_info->spawn_group;
                                if (!arr.empty()) {
                                        j["spawn_groups"] = arr;
                                }
                        } else if (asset_child_info->spawn_group.is_object()) {
                                nlohmann::json group = asset_child_info->spawn_group;
                                if (!asset_child_info->area_name.empty()) {
                                        group["linked_area"] = asset_child_info->area_name;
                                        group["link_to_area"] = true;
                                }
                                group["placed_on_top_parent"] = asset_child_info->placed_on_top_parent;
                                group["z_offset"] = asset_child_info->z_offset;
                                j["spawn_groups"] = nlohmann::json::array();
                                j["spawn_groups"].push_back(std::move(group));
                        }

                        if (!j.contains("spawn_groups") || !j["spawn_groups"].is_array() || j["spawn_groups"].empty()) {
                                vibble::log::debug(std::string{"[Spawn] No spawn group data for child area '"} +
                                                   asset_child_info->area_name + "' on parent '" + parent_name +
                                                   "'; skipping" );
                                continue;
                        }

                        vibble::log::debug(std::string{"[Spawn] Using spawn groups for child area '"} +
                                           asset_child_info->area_name + "' on parent '" + parent_name + "'");
                        resolved_child_areas.insert_or_assign(asset_child_info->area_name, childArea);
                        AssetSpawnPlanner childPlanner(std::vector<nlohmann::json>{ j },
                                                       childArea,
                                                       *asset_library_);
                        AssetSpawner childSpawner(asset_library_, exclusion_zones_);
                        childSpawner.set_map_grid_settings(map_grid_settings_);
                        childSpawner.spawn_children(childArea, resolved_child_areas, &childPlanner);
                        auto kids = childSpawner.extract_all_assets();
                        vibble::log::debug(std::string{"[Spawn] Parent '"} + parent_name +
                                           "' child area '" + asset_child_info->area_name + "' produced " +
                                           std::to_string(kids.size()) + " asset(s)");
                        for (auto& uptr : kids) {
                                if (!uptr || !uptr->info) continue;
                                uptr->parent = raw;
                                int z_offset = asset_child_info->z_offset;
                                if (asset_child_info->placed_on_top_parent && z_offset <= 0) {
                                        z_offset = 1;
                                }
                                uptr->set_z_offset(z_offset);
                                uptr->set_hidden(false);
                                uptr->set_owning_room_name(raw->owning_room_name());

                                raw->asset_children.push_back(uptr.get());
                                all_.push_back(std::move(uptr));
                                if (!raw->asset_children.empty()) {
                                        Asset* asset_child_ptr = raw->asset_children.back();
                                        if (asset_child_ptr && asset_child_ptr->info) {
                                                std::ostringstream oss;
                                                oss << "[Spawn] -> Child '" << asset_child_ptr->info->name
                                                    << "' placed at (" << asset_child_ptr->pos.x << ", "
                                                    << asset_child_ptr->pos.y << ") with z_offset " << asset_child_ptr->z_offset;
                                                vibble::log::debug(oss.str());
                                        }
                                }
                        }
                }
        }
        return raw;
}

bool SpawnContext::point_overlaps_trail(SDL_Point pt, const Area* ignore) const {
        for (const Area* trail : trail_areas_) {
                if (!trail) {
                        continue;
                }
                if (ignore && trail == ignore) {
                        continue;
                }
                if (trail->contains_point(pt)) {
                        return true;
                }
        }
        return false;
}

void SpawnContext::set_map_grid_settings(const MapGridSettings& settings) {
        map_grid_settings_ = settings;
        map_grid_settings_.clamp();
        spawn_resolution_ = occupancy_ ? occupancy_->resolution() : vibble::grid::clamp_resolution(map_grid_settings_.resolution);
}

bool SpawnContext::position_allowed(const Area& area, SDL_Point pos) const {
        if (area.contains_point(pos)) {
                return true;
        }
        if (!allow_partial_clip_overlap_ || !occupancy_) {
                return false;
        }
        return occupancy_->cell_overlaps(area, pos);
}
