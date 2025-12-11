#include "map_wide_asset_spawner.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <limits>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "asset/Asset.hpp"
#include "asset/asset_library.hpp"
#include "map_generation/room.hpp"
#include "spawn/asset_spawn_planner.hpp"
#include "spawn/check.hpp"
#include "spawn/spawn_context.hpp"
#include "spawn/spacing_util.hpp"
#include "utils/grid.hpp"
#include "utils/grid_occupancy.hpp"
#include "utils/area.hpp"
#include "utils/map_grid_settings.hpp"
#include "utils/string_utils.hpp"

namespace {
constexpr std::uint64_t kGoldenRatio = 0x9e3779b97f4a7c15ULL;

std::uint64_t mix_value(std::uint64_t seed, std::uint64_t value) {
    seed ^= value + kGoldenRatio + (seed << 6) + (seed >> 2);
    return seed;
}

using vibble::strings::to_lower_copy;
}

MapWideAssetSpawner::MapWideAssetSpawner(AssetLibrary* asset_library,
                                         const MapGridSettings& grid_settings,
                                         std::string map_seed,
                                         nlohmann::json& map_assets_json)
    : asset_library_(asset_library),
      grid_settings_(grid_settings),
      base_seed_(std::hash<std::string>{}(std::move(map_seed))),
      map_assets_json_(map_assets_json) {
    grid_settings_.clamp();
}

void MapWideAssetSpawner::spawn(std::vector<std::unique_ptr<Room>>& rooms) {
    if (!asset_library_) {
        return;
    }
    if (rooms.empty()) {
        return;
    }

    if (!map_assets_json_.is_object()) {
        map_assets_json_ = nlohmann::json::object();
    }
    auto groups_it = map_assets_json_.find("spawn_groups");
    if (groups_it == map_assets_json_.end() || !groups_it->is_array() || groups_it->empty()) {
        return;
    }

    int min_x = std::numeric_limits<int>::max();
    int min_y = std::numeric_limits<int>::max();
    int max_x = std::numeric_limits<int>::min();
    int max_y = std::numeric_limits<int>::min();
    bool have_area = false;

    for (const auto& room_ptr : rooms) {
        if (!room_ptr || !room_ptr->room_area) {
            continue;
        }
        auto [rminx, rminy, rmaxx, rmaxy] = room_ptr->room_area->get_bounds();
        min_x = std::min(min_x, rminx);
        min_y = std::min(min_y, rminy);
        max_x = std::max(max_x, rmaxx);
        max_y = std::max(max_y, rmaxy);
        have_area = true;
    }

    if (!have_area) {
        return;
    }
    if (min_x >= max_x || min_y >= max_y) {
        return;
    }

    std::vector<SDL_Point> polygon{
        SDL_Point{min_x, min_y},
        SDL_Point{max_x, min_y},
        SDL_Point{max_x, max_y},
        SDL_Point{min_x, max_y},
};
    Area sweep_area("map_wide_sweep", polygon);
    sweep_area.set_type("map_wide");

    std::vector<nlohmann::json> sources{map_assets_json_};
    AssetSpawnPlanner::SourceContext ctx;
    ctx.json_ref = &map_assets_json_;
    ctx.persist = [this](const nlohmann::json& updated) { map_assets_json_ = updated; };
    AssetSpawnPlanner planner(sources, sweep_area, *asset_library_, std::vector<AssetSpawnPlanner::SourceContext>{ctx});
    const auto& queue = planner.get_spawn_queue();
    if (queue.empty()) {
        return;
    }

    const SpawnInfo* spawn_info = nullptr;
    for (const auto& info : queue) {
        if (!info.has_candidates()) {
            continue;
        }
        if (info.name == "batch_map_assets") {
            spawn_info = &info;
            break;
        }
    }
    if (!spawn_info) {
        for (const auto& info : queue) {
            if (info.has_candidates()) {
                spawn_info = &info;
                break;
            }
        }
    }
    if (!spawn_info) {
        return;
    }

    std::unordered_set<std::string> spacing_names;
    if (spawn_info->check_min_spacing) {
        for (const auto& cand : spawn_info->candidates) {
            if (!cand.info || cand.info->name.empty()) {
                continue;
            }
            spacing_names.insert(cand.info->name);
        }
    }

    std::size_t total_existing = 0;
    for (const auto& room_ptr : rooms) {
        if (!room_ptr) {
            continue;
        }
        total_existing += room_ptr->assets.size();
    }

    std::vector<std::unique_ptr<Asset>> global_assets;
    global_assets.reserve(total_existing);
    std::unordered_map<Asset*, Room*> owner_map;
    owner_map.reserve(total_existing);

    for (auto& room_ptr : rooms) {
        if (!room_ptr) {
            continue;
        }
        auto& assets = room_ptr->assets;
        for (auto& asset_uptr : assets) {
            if (Asset* raw = asset_uptr.get()) {
                owner_map[raw] = room_ptr.get();
            }
            global_assets.push_back(std::move(asset_uptr));
        }
        assets.clear();
    }

    int resolution = 5;
    try {
        if (spawn_info) {
            const std::string& sid = spawn_info->spawn_id;
            if (!sid.empty() && map_assets_json_.contains("spawn_groups") && map_assets_json_["spawn_groups"].is_array()) {
                for (const auto& entry : map_assets_json_["spawn_groups"]) {
                    if (!entry.is_object()) continue;
                    if (entry.value("spawn_id", std::string{}) == sid) {
                        resolution = std::max(5, entry.value("grid_resolution", resolution));
                        break;
                    }
                }
            }
        }
    } catch (...) {
        resolution = 5;
    }
    resolution = vibble::grid::clamp_resolution(resolution);
    vibble::grid::Grid& grid_service = vibble::grid::global_grid();
    vibble::grid::Occupancy occupancy(sweep_area, resolution, grid_service);

    for (const auto& asset_uptr : global_assets) {
        if (!asset_uptr) {
            continue;
        }
        occupancy.set_occupied_at(asset_uptr->pos, true);
    }

    std::vector<const Area*> trail_areas;
    trail_areas.reserve(rooms.size());
    for (const auto& room_ptr : rooms) {
        if (!room_ptr || !room_ptr->room_area) {
            continue;
        }
        if (to_lower_copy(room_ptr->type) == "trail") {
            trail_areas.push_back(room_ptr->room_area.get());
        }
    }

    auto vertices = occupancy.vertices_in_area(sweep_area);
    struct Cell {
        vibble::grid::Occupancy::Vertex* vertex = nullptr;
        Room* owner = nullptr;
};
    std::vector<Cell> cells;
    cells.reserve(vertices.size());
    for (auto* vertex : vertices) {
        if (!vertex) {
            continue;
        }
        Room* owner = resolve_owner(vertex->world, rooms);
        if (!owner) {
            continue;
        }
        cells.push_back(Cell{vertex, owner});
    }

    if (cells.empty()) {
        for (auto& asset_uptr : global_assets) {
            if (!asset_uptr) {
                continue;
            }
            Room* owner = nullptr;
            auto it = owner_map.find(asset_uptr.get());
            if (it != owner_map.end()) {
                owner = it->second;
            }
            if (!owner) {
                owner = resolve_owner(asset_uptr->pos, rooms);
            }
            if (!owner) {
                continue;
            }
            if (asset_uptr->owning_room_name().empty()) {
                asset_uptr->set_owning_room_name(owner->room_name);
            }
            owner->assets.push_back(std::move(asset_uptr));
        }
        return;
    }

    std::sort(cells.begin(), cells.end(), [](const Cell& lhs, const Cell& rhs) {
        if (lhs.vertex->index.y != rhs.vertex->index.y) {
            return lhs.vertex->index.y < rhs.vertex->index.y;
        }
        return lhs.vertex->index.x < rhs.vertex->index.x;
    });

    Check checker(false);
    checker.begin_session(grid_service, resolution);
    std::vector<Area> exclusion_zones;
    auto asset_info_library = asset_library_->all();
    std::mt19937 rng;
    SpawnContext context(rng, checker, exclusion_zones, asset_info_library, global_assets, asset_library_, grid_service, &occupancy);
    context.set_map_grid_settings(grid_settings_);
    context.set_spawn_resolution(resolution);
    context.set_trail_areas(trail_areas);
    context.set_spacing_filter(std::move(spacing_names));

    for (const auto& cell : cells) {
        auto* vertex = cell.vertex;
        Room* owner = cell.owner;
        if (!vertex || !owner || !owner->room_area) {
            continue;
        }
        if (vertex->occupied) {
            continue;
        }
        if (!owner->inherits_map_assets()) {
            occupancy.set_occupied(vertex, true);
            continue;
        }
        const auto& candidates = spawn_info->candidates;
        if (candidates.empty()) {
            occupancy.set_occupied(vertex, true);
            continue;
        }

        std::uint64_t seed_value = seed_for_index(vertex->index);
        std::uint32_t low = static_cast<std::uint32_t>(seed_value & 0xFFFFFFFFULL);
        std::uint32_t high = static_cast<std::uint32_t>((seed_value >> 32) & 0xFFFFFFFFULL);
        std::seed_seq seq{low, high};
        rng.seed(seq);

        const SpawnCandidate* candidate = spawn_info->select_candidate(rng);
        if (!candidate || candidate->is_null || !candidate->info) {
            occupancy.set_occupied(vertex, true);
            continue;
        }

        SDL_Point spawn_pos = vertex->world;
        spawn_pos = apply_map_grid_jitter(grid_settings_, spawn_pos, rng, *owner->room_area);
        context.set_clip_area(owner->room_area.get());

        const bool enforce_spacing = spawn_info->check_min_spacing;
        if (context.checker().check(candidate->info,
                                    spawn_pos,
                                    context.exclusion_zones(),
                                    context.all_assets(),
                                    true,
                                    enforce_spacing,
                                    false,
                                    true,
                                    5)) {
            occupancy.set_occupied(vertex, true);
            continue;
        }

        Asset* spawned = context.spawnAsset(candidate->name, candidate->info, *owner->room_area, spawn_pos, 0, nullptr, spawn_info->spawn_id, "MapWide");
        if (spawned) {
            spawned->set_owning_room_name(owner->room_name);
            owner_map[spawned] = owner;
            context.checker().register_asset(spawned, enforce_spacing, false);
        }
        occupancy.set_occupied(vertex, true);
    }

    checker.reset_session();

    for (auto& asset_uptr : global_assets) {
        if (!asset_uptr) {
            continue;
        }
        Room* owner = nullptr;
        auto it = owner_map.find(asset_uptr.get());
        if (it != owner_map.end()) {
            owner = it->second;
        }
        if (!owner) {
            owner = resolve_owner(asset_uptr->pos, rooms);
        }
        if (!owner) {
            continue;
        }
        if (asset_uptr->owning_room_name().empty()) {
            asset_uptr->set_owning_room_name(owner->room_name);
        }
        owner->assets.push_back(std::move(asset_uptr));
    }
}

std::uint64_t MapWideAssetSpawner::seed_for_index(SDL_Point index) const {
    std::uint64_t seed = base_seed_;
    seed = mix_value(seed, static_cast<std::uint64_t>(static_cast<std::int64_t>(index.x)));
    seed = mix_value(seed, static_cast<std::uint64_t>(static_cast<std::int64_t>(index.y)));
    return seed;
}

Room* MapWideAssetSpawner::resolve_owner(SDL_Point world_point,
                                         const std::vector<std::unique_ptr<Room>>& rooms) const {
    Room* fallback = nullptr;
    for (const auto& room_ptr : rooms) {
        Room* room = room_ptr.get();
        if (!room || !room->room_area) {
            continue;
        }
        if (!room->room_area->contains_point(world_point)) {
            continue;
        }
        if (room->inherits_map_assets()) {
            return room;
        }
        if (!fallback) {
            fallback = room;
        }
    }
    return fallback;
}
