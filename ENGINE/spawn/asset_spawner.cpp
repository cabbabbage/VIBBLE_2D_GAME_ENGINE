#include "asset_spawner.hpp"
#include "asset_spawn_planner.hpp"
#include "spawn_context.hpp"
#include "methods/exact_spawner.hpp"
#include "methods/center_spawner.hpp"
#include "methods/random_spawner.hpp"
#include "methods/perimeter_spawner.hpp"
#include "methods/children_spawner.hpp"
#include "methods/percent_spawner.hpp"
#include "check.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <nlohmann/json.hpp>
#include "utils/map_grid.hpp"
AssetSpawner::AssetSpawner(AssetLibrary* asset_library,
                           std::vector<Area> exclusion_zones)
: asset_library_(asset_library),
exclusion_zones(std::move(exclusion_zones)),
rng_(std::random_device{}()),
checker_(false) {}

void AssetSpawner::spawn(Room& room) {
	if (!room.planner) {
		std::cerr << "[AssetSpawner] Room planner is null — skipping room: " << room.room_name << "\n";
		return;
	}
	const Area& spawn_area = *room.room_area;
        current_room_ = &room;
        map_grid_settings_ = room.map_grid_settings();
        run_spawning(room.planner.get(), spawn_area);
        current_room_ = nullptr;
        room.add_room_assets(std::move(all_));
}

std::vector<std::unique_ptr<Asset>> AssetSpawner::spawn_boundary_from_json(const nlohmann::json& boundary_json,
                                                                          const Area& spawn_area,
                                                                          const std::string& source_name) {
        if (boundary_json.is_null()) {
                return {};
        }
    std::vector<nlohmann::json> json_sources{ boundary_json };
    AssetSpawnPlanner planner(json_sources, spawn_area, *asset_library_);
        boundary_mode_ = true;
        run_spawning(&planner, spawn_area);
        boundary_mode_ = false;
        return extract_all_assets();
}

void AssetSpawner::spawn_children(const Area& spawn_area, AssetSpawnPlanner* planner) {
        if (!planner) {
		std::cerr << "[AssetSpawner] Child planner is null — skipping.\n";
		return;
	}
        run_child_spawning(planner, spawn_area);
}

std::vector<std::unique_ptr<Asset>> AssetSpawner::extract_all_assets() {
	return std::move(all_);
}

void AssetSpawner::run_spawning(AssetSpawnPlanner* planner, const Area& area) {
        asset_info_library_ = asset_library_->all();
        spawn_queue_ = planner->get_spawn_queue();
        if (boundary_mode_) {
                run_boundary_spawning(area);
                return;
        }
    int spacing = map_grid_settings_.spacing;
    if (spacing <= 0) spacing = 100;

    auto [minx, miny, maxx, maxy] = area.get_bounds();
    int w = std::max(0, maxx - minx);
    int h = std::max(0, maxy - miny);
    MapGrid grid(w, h, spacing, SDL_Point{minx, miny});
    SpawnContext ctx(rng_, checker_, exclusion_zones, asset_info_library_, all_, asset_library_, &grid);
        ExactSpawner exact;
        CenterSpawner center;
        RandomSpawner random;
        PerimeterSpawner perimeter;
        PercentSpawner percent;
        for (auto& queue_item : spawn_queue_) {
                if (!queue_item.has_candidates()) continue;
                const std::string& pos = queue_item.position;

                if (current_room_ && !queue_item.link_area_name.empty()) {
                        Area* link = current_room_->find_area(queue_item.link_area_name);
                        ctx.set_clip_area(link);
                } else {
                        ctx.set_clip_area(nullptr);
                }

                if (queue_item.name == "batch_map_assets") {

                        std::vector<double> base_weights;
                        base_weights.reserve(queue_item.candidates.size());
                        double total_weight = 0.0;
                        for (const auto& cand : queue_item.candidates) {
                                double weight = cand.weight;
                                if (weight < 0.0) weight = 0.0;
                                if (weight > 0.0) total_weight += weight;
                                base_weights.push_back(weight);
                        }
                        if (total_weight <= 0.0 && !base_weights.empty()) {
                                std::fill(base_weights.begin(), base_weights.end(), 1.0);
                        }

                        auto grid_points = grid.get_all_points_in_area(area);
                        if (grid_points.empty()) {
                                continue;
                        }
                        std::shuffle(grid_points.begin(), grid_points.end(), ctx.rng());

                        for (auto* gp : grid_points) {
                                if (!gp) continue;
                                SDL_Point spawn_pos{ gp->pos.x, gp->pos.y };
                                spawn_pos = apply_map_grid_jitter(map_grid_settings_, spawn_pos, ctx.rng(), area);
                                bool placed = false;
                                std::vector<double> attempt_weights = base_weights;
                                const size_t max_candidate_attempts = queue_item.candidates.size();
                                for (size_t attempt = 0; attempt < max_candidate_attempts; ++attempt) {
                                        double total_weight = std::accumulate(attempt_weights.begin(), attempt_weights.end(), 0.0);
                                        if (total_weight <= 0.0) break;
                                        std::discrete_distribution<size_t> dist(attempt_weights.begin(), attempt_weights.end());
                                        size_t idx = dist(ctx.rng());
                                        if (idx >= queue_item.candidates.size()) break;
                                        if (attempt_weights[idx] <= 0.0) {
                                                attempt_weights[idx] = 0.0;
                                                continue;
                                        }
                                        const SpawnCandidate& candidate = queue_item.candidates[idx];

                                        if (candidate.is_null || !candidate.info) {
                                                grid.set_occupied(gp, true);
                                                placed = true;
                                                break;
                                        }
                                        if (ctx.checker().check(candidate.info, spawn_pos, ctx.exclusion_zones(), ctx.all_assets(), true, true, true, 5)) {
                                                attempt_weights[idx] = 0.0;
                                                continue;
                                        }
                                        auto* result = ctx.spawnAsset(candidate.name, candidate.info, area, spawn_pos, 0, nullptr, queue_item.spawn_id, queue_item.position);
                                        if (!result) {
                                                attempt_weights[idx] = 0.0;
                                                continue;
                                        }
                                        grid.set_occupied(gp, true);
                                        placed = true;
                                        break;
                                }
                                if (!placed) {
                                        grid.set_occupied(gp, true);
                                }
                        }
                        continue;
                }
                if (pos == "Exact" || pos == "Exact Position") {
                        exact.spawn(queue_item, &area, ctx);
                } else if (pos == "Center") {
                        center.spawn(queue_item, &area, ctx);
                } else if (pos == "Perimeter") {
                        perimeter.spawn(queue_item, &area, ctx);
                } else if (pos == "Percent") {
                        percent.spawn(queue_item, &area, ctx);
                } else {
                        random.spawn(queue_item, &area, ctx);
                }
        }
}

void AssetSpawner::run_boundary_spawning(const Area& area) {
        auto point_in_exclusion = [&](const SDL_Point& pt) {
                return std::any_of(exclusion_zones.begin(), exclusion_zones.end(),
                [&](const Area& zone) { return zone.contains_point(pt); });
};

        for (auto& queue_item : spawn_queue_) {
                if (!queue_item.has_candidates()) continue;

                constexpr int kBoundarySpacing = 100;
                MapGrid grid = MapGrid::from_area_bounds(area, kBoundarySpacing);
                SpawnContext ctx(rng_, checker_, exclusion_zones, asset_info_library_, all_, asset_library_, &grid);

                if (current_room_ && !queue_item.link_area_name.empty()) {
                        Area* link = current_room_->find_area(queue_item.link_area_name);
                        ctx.set_clip_area(link);
                } else {
                        ctx.set_clip_area(nullptr);
                }

                std::vector<double> base_weights;
                base_weights.reserve(queue_item.candidates.size());
                double total_weight = 0.0;
                for (const auto& cand : queue_item.candidates) {
                        double weight = cand.weight;
                        if (weight < 0.0) weight = 0.0;
                        if (weight > 0.0) total_weight += weight;
                        base_weights.push_back(weight);
                }
                if (total_weight <= 0.0 && !base_weights.empty()) {
                        std::fill(base_weights.begin(), base_weights.end(), 1.0);
                }

                auto grid_points = grid.get_all_points_in_area(area);
                std::vector<MapGrid::Point*> eligible;
                eligible.reserve(grid_points.size());
                for (auto* gp : grid_points) {
                        if (!gp) continue;
                        if (point_in_exclusion(gp->pos)) continue;
                        eligible.push_back(gp);
                }

                if (eligible.empty()) {
                        continue;
                }

                std::shuffle(eligible.begin(), eligible.end(), rng_);

                for (auto* gp : eligible) {
                        if (!gp) continue;
                        SDL_Point spawn_pos = gp->pos;

                        bool success = false;
                        std::vector<double> attempt_weights = base_weights;
                        const size_t max_candidate_attempts = queue_item.candidates.size();
                        for (size_t attempt = 0; attempt < max_candidate_attempts; ++attempt) {
                                double total_weight = std::accumulate(attempt_weights.begin(), attempt_weights.end(), 0.0);
                                if (total_weight <= 0.0) break;
                                std::discrete_distribution<size_t> dist(attempt_weights.begin(), attempt_weights.end());
                                size_t idx = dist(ctx.rng());
                                if (idx >= queue_item.candidates.size()) break;
                                if (attempt_weights[idx] <= 0.0) {
                                        attempt_weights[idx] = 0.0;
                                        continue;
                                }
                                const SpawnCandidate& candidate = queue_item.candidates[idx];

                                if (candidate.is_null || !candidate.info) {
                                        grid.set_occupied(gp, true);
                                        break;
                                }

                                if (ctx.checker().check(candidate.info, spawn_pos, ctx.exclusion_zones(), ctx.all_assets(), true, true, true, 5)) {
                                        attempt_weights[idx] = 0.0;
                                        continue;
                                }

                                auto* result = ctx.spawnAsset(candidate.name, candidate.info, area, spawn_pos, 0, nullptr, queue_item.spawn_id, queue_item.position);
                                if (!result) {
                                        attempt_weights[idx] = 0.0;
                                        continue;
                                }

                                grid.set_occupied(gp, true);
                                success = true;
                                break;
                        }

                        if (!success) {
                                grid.set_occupied(gp, true);
                        }
                }
        }
}

void AssetSpawner::run_child_spawning(AssetSpawnPlanner* planner, const Area& area) {
        asset_info_library_ = asset_library_->all();
        spawn_queue_ = planner->get_spawn_queue();

        SpawnContext ctx(rng_, checker_, exclusion_zones, asset_info_library_, all_, asset_library_, nullptr);
        ChildrenSpawner childMethod;
        for (auto& queue_item : spawn_queue_) {
                if (!queue_item.has_candidates()) continue;
                childMethod.spawn(queue_item, &area, ctx);
        }
}
