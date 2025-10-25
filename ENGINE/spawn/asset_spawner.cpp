#include "asset_spawner.hpp"
#include "asset_spawn_planner.hpp"
#include "spawn_context.hpp"
#include "methods/exact_spawner.hpp"
#include "methods/center_spawner.hpp"
#include "methods/random_spawner.hpp"
#include "methods/perimeter_spawner.hpp"
#include "methods/edge_spawner.hpp"
#include "methods/children_spawner.hpp"
#include "methods/percent_spawner.hpp"
#include "check.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <cctype>
#include <nlohmann/json.hpp>
#include "util/grid.hpp"
#include "util/grid_occupancy.hpp"
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

std::vector<std::unique_ptr<Asset>> AssetSpawner::spawn_edge_from_json(const nlohmann::json& edge_json,
                                                                          const Area& spawn_area,
                                                                          const std::string& source_name) {
        if (edge_json.is_null()) {
                return {};
        }
    std::vector<nlohmann::json> json_sources{ edge_json };
    AssetSpawnPlanner planner(json_sources, spawn_area, *asset_library_);
        edge_mode_ = true;
        run_spawning(&planner, spawn_area);
        edge_mode_ = false;
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
        if (edge_mode_) {
                run_edge_spawning(area);
                return;
        }
    const int resolution = std::max(0, map_grid_settings_.resolution);
    vibble::grid::Grid& grid_service = vibble::grid::global_grid();
    checker_.begin_session(grid_service, resolution);
    vibble::grid::Occupancy occupancy(area, resolution, grid_service);
    SpawnContext ctx(rng_, checker_, exclusion_zones, asset_info_library_, all_, asset_library_, grid_service, &occupancy);
    ctx.set_spawn_resolution(resolution);
        std::vector<const Area*> trail_areas;
        auto add_trail_area = [&trail_areas](const Area* candidate, const std::string& type) {
                if (!candidate) {
                        return;
                }
                std::string lowered = type;
                std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
                        return static_cast<char>(std::tolower(ch));
                });
                if (lowered == "trail") {
                        trail_areas.push_back(candidate);
                }
        };
        if (current_room_) {
                if (current_room_->room_area) {
                        add_trail_area(current_room_->room_area.get(), current_room_->room_area->get_type());
                }
                for (const auto& named : current_room_->areas) {
                        add_trail_area(named.area.get(), named.type);
                }
        }
        ctx.set_trail_areas(std::move(trail_areas));
        ExactSpawner exact;
        CenterSpawner center;
        RandomSpawner random;
        PerimeterSpawner perimeter;
        EdgeSpawner edge;
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

                        auto vertices = occupancy.vertices_in_area(area);
                        if (vertices.empty()) {
                                continue;
                        }
                        std::shuffle(vertices.begin(), vertices.end(), ctx.rng());

                        for (auto* vertex : vertices) {
                                if (!vertex) continue;
                                SDL_Point spawn_pos{ vertex->world.x, vertex->world.y };
                                spawn_pos = apply_map_grid_jitter(map_grid_settings_, spawn_pos, ctx.rng(), area);
                                bool placed = false;
                                std::vector<double> attempt_weights = base_weights;
                                const size_t max_candidate_attempts = queue_item.candidates.size();
                                const bool enforce_spacing = queue_item.check_min_spacing;
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
                                                occupancy.set_occupied(vertex, true);
                                                placed = true;
                                                break;
                                        }
                                        if (ctx.checker().check(candidate.info,
                                                                spawn_pos,
                                                                ctx.exclusion_zones(),
                                                                ctx.all_assets(),
                                                                true,
                                                                enforce_spacing,
                                                                false,
                                                                false,
                                                                5)) {
                                                attempt_weights[idx] = 0.0;
                                                continue;
                                        }
                                        auto* result = ctx.spawnAsset(candidate.name,
                                                                      candidate.info,
                                                                      area,
                                                                      spawn_pos,
                                                                      0,
                                                                      nullptr,
                                                                      queue_item.spawn_id,
                                                                      queue_item.position);
                                        if (!result) {
                                                attempt_weights[idx] = 0.0;
                                                continue;
                                        }
                                        ctx.checker().register_asset(result, enforce_spacing, true);
                                        occupancy.set_occupied(vertex, true);
                                        placed = true;
                                        break;
                                }
                                if (!placed) {
                                        occupancy.set_occupied(vertex, true);
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
                } else if (pos == "Edge") {
                        edge.spawn(queue_item, &area, ctx);
                } else if (pos == "Percent") {
                        percent.spawn(queue_item, &area, ctx);
                } else {
                        random.spawn(queue_item, &area, ctx);
                }
        }
        checker_.reset_session();
}

void AssetSpawner::run_edge_spawning(const Area& area) {
        auto point_in_exclusion = [&](const SDL_Point& pt) {
                return std::any_of(exclusion_zones.begin(), exclusion_zones.end(),
                [&](const Area& zone) { return zone.contains_point(pt); });
};

        vibble::grid::Grid& grid_service = vibble::grid::global_grid();
        constexpr int kEdgeResolution = 7;
        checker_.begin_session(grid_service, kEdgeResolution);

        for (auto& queue_item : spawn_queue_) {
                if (!queue_item.has_candidates()) continue;

                vibble::grid::Occupancy occupancy(area, kEdgeResolution, grid_service);
                SpawnContext ctx(rng_, checker_, exclusion_zones, asset_info_library_, all_, asset_library_, grid_service, &occupancy);
                ctx.set_spawn_resolution(kEdgeResolution);
                ctx.set_trail_areas({});

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

                auto vertices = occupancy.vertices_in_area(area);
                std::vector<vibble::grid::Occupancy::Vertex*> eligible;
                eligible.reserve(vertices.size());
                for (auto* vertex : vertices) {
                        if (!vertex) continue;
                        if (point_in_exclusion(vertex->world)) continue;
                        eligible.push_back(vertex);
                }

                if (eligible.empty()) {
                        continue;
                }

                std::shuffle(eligible.begin(), eligible.end(), rng_);

                for (auto* vertex : eligible) {
                        if (!vertex) continue;
                        SDL_Point spawn_pos = vertex->world;

                        bool success = false;
                        std::vector<double> attempt_weights = base_weights;
                        const size_t max_candidate_attempts = queue_item.candidates.size();
                        const bool enforce_spacing = queue_item.check_min_spacing;
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
                                        occupancy.set_occupied(vertex, true);
                                        break;
                                }

                                if (ctx.checker().check(candidate.info,
                                                        spawn_pos,
                                                        ctx.exclusion_zones(),
                                                        ctx.all_assets(),
                                                        true,
                                                        enforce_spacing,
                                                        true,
                                                        false,
                                                        5)) {
                                        attempt_weights[idx] = 0.0;
                                        continue;
                                }

                                auto* result = ctx.spawnAsset(candidate.name,
                                                             candidate.info,
                                                             area,
                                                             spawn_pos,
                                                             0,
                                                             nullptr,
                                                             queue_item.spawn_id,
                                                             queue_item.position);
                                if (!result) {
                                        attempt_weights[idx] = 0.0;
                                        continue;
                                }

                                ctx.checker().register_asset(result, enforce_spacing, false);

                                occupancy.set_occupied(vertex, true);
                                success = true;
                                break;
                        }

                        if (!success) {
                                occupancy.set_occupied(vertex, true);
                        }
                }
        }
        checker_.reset_session();
}

void AssetSpawner::run_child_spawning(AssetSpawnPlanner* planner, const Area& area) {
        asset_info_library_ = asset_library_->all();
        spawn_queue_ = planner->get_spawn_queue();

        vibble::grid::Grid& grid_service = vibble::grid::global_grid();
        const int resolution = std::max(0, map_grid_settings_.resolution);
        checker_.begin_session(grid_service, resolution);
        SpawnContext ctx(rng_, checker_, exclusion_zones, asset_info_library_, all_, asset_library_, grid_service, nullptr);
        ctx.set_spawn_resolution(resolution);
        ChildrenSpawner childMethod;
        for (auto& queue_item : spawn_queue_) {
                if (!queue_item.has_candidates()) continue;
                childMethod.spawn(queue_item, &area, ctx);
        }
        checker_.reset_session();
}
