#include "asset_spawner.hpp"
#include "asset_spawn_planner.hpp"
#include "spacing_util.hpp"
#include "spawn_context.hpp"
#include "methods/exact_spawner.hpp"
#include "methods/center_spawner.hpp"
#include "methods/random_spawner.hpp"
#include "methods/perimeter_spawner.hpp"
#include "methods/edge_spawner.hpp"
#include "methods/percent_spawner.hpp"
#include "check.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <cctype>
#include <nlohmann/json.hpp>
#include "utils/grid.hpp"
#include "utils/grid_occupancy.hpp"
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

        try {
                nlohmann::json& root = room.assets_data();

                std::unordered_map<std::string, int> area_selection_counts;
                if (room.planner) {
                        const auto& queue = room.planner->get_spawn_queue();
                        for (const auto& item : queue) {

                                std::vector<std::string> names;
                                std::vector<double> weights;
                                for (const auto& cand : item.candidates) {
                                        if (!cand.name.empty() && room.find_area(cand.name) != nullptr) {
                                                names.push_back(cand.name);
                                                double w = cand.weight;
                                                if (w < 0.0) w = 0.0;
                                                weights.push_back(w);
                                        }
                                }
                                if (names.empty()) continue;

                                bool any_positive = false;
                                for (double w : weights) if (w > 0.0) { any_positive = true; break; }
                                if (!any_positive) {
                                        std::fill(weights.begin(), weights.end(), 1.0);
                                }
                                std::discrete_distribution<size_t> chooser(weights.begin(), weights.end());
                                for (int i = 0; i < std::max(0, item.quantity); ++i) {
                                        size_t idx = chooser(rng_);
                                        if (idx < names.size()) {
                                                area_selection_counts[names[idx]] += 1;
                                        }
                                }
                        }
                }

                if (root.is_object() && root.contains("areas") && root["areas"].is_array()) {

                        bool selective = !area_selection_counts.empty();
                        for (auto& area_entry : root["areas"]) {
                                if (!area_entry.is_object()) continue;
                                auto it = area_entry.find("spawn_groups");
                                if (it == area_entry.end() || !it->is_array() || it->empty()) continue;
                                std::string area_name = area_entry.value("name", std::string{});
                                if (area_name.empty()) continue;
                                Area* area_ptr = room.find_area(area_name);
                                if (!area_ptr) continue;

                                int times = 1;
                                if (selective) {
                                        auto ct = area_selection_counts.find(area_name);
                                        if (ct == area_selection_counts.end() || ct->second <= 0) {
                                                continue;
                                        }
                                        times = ct->second;
                                }

                                for (int pass = 0; pass < times; ++pass) {
                                        std::vector<nlohmann::json> sources;
                                        sources.push_back(nlohmann::json::object());
                                        sources.back()["spawn_groups"] = *it;
                                        std::vector<AssetSpawnPlanner::SourceContext> contexts;
                                        contexts.resize(1);
                                        contexts[0].json_ref = &sources.back();
                                        contexts[0].persist = [&area_entry](const nlohmann::json& src){
                                                if (src.is_object() && src.contains("spawn_groups") && src["spawn_groups"].is_array()) {
                                                        area_entry["spawn_groups"] = src["spawn_groups"];
                                                }
};

                                        AssetSpawnPlanner area_planner(sources, *area_ptr, *asset_library_, contexts);
                                        run_spawning(&area_planner, *area_ptr);
                                }
                        }
                }
        } catch (...) {

        }

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

        group_resolution_map_.clear();
        try {
                if (boundary_json.contains("spawn_groups") && boundary_json["spawn_groups"].is_array()) {
                        for (const auto& entry : boundary_json["spawn_groups"]) {
                                if (!entry.is_object()) continue;
                                const std::string sid = entry.value("spawn_id", std::string{});
                                if (sid.empty()) continue;
                                int r = std::max(5, entry.value("grid_resolution", 5));
                                r = vibble::grid::clamp_resolution(r);
                                group_resolution_map_.insert_or_assign(sid, r);
                        }
                }
        } catch (...) {

        }
    AssetSpawnPlanner planner(json_sources, spawn_area, *asset_library_);
        boundary_mode_ = true;
        run_spawning(&planner, spawn_area);
        boundary_mode_ = false;
        return extract_all_assets();
}

void AssetSpawner::spawn_children(const Area& spawn_area,
                                  const std::unordered_map<std::string, Area>& area_lookup,
                                  AssetSpawnPlanner* planner) {
        if (!planner) {
                std::cerr << "[AssetSpawner] Child planner is null — skipping.\n";
                return;
        }
        run_child_spawning(planner, spawn_area, area_lookup);
}

std::vector<std::unique_ptr<Asset>> AssetSpawner::extract_all_assets() {
	return std::move(all_);
}

void AssetSpawner::run_spawning(AssetSpawnPlanner* planner, const Area& area) {
        asset_info_library_ = asset_library_->all();
        spawn_queue_ = planner->get_spawn_queue();
        if (boundary_mode_) {
                run_edge_spawning(area);
                return;
        }
        auto spacing_names = collect_spacing_asset_names(spawn_queue_);
    const int resolution = std::max(0, map_grid_settings_.resolution);
    vibble::grid::Grid& grid_service = vibble::grid::global_grid();
    checker_.begin_session(grid_service, resolution);
    vibble::grid::Occupancy occupancy(area, resolution, grid_service);
    SpawnContext ctx(rng_, checker_, exclusion_zones, asset_info_library_, all_, asset_library_, grid_service, &occupancy);
    ctx.set_spacing_filter(std::move(spacing_names));
    ctx.set_map_grid_settings(map_grid_settings_);
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
        struct ZoneSpawnRecord { Asset* asset = nullptr; const Area* region = nullptr; bool adjust = false; };
    std::vector<ZoneSpawnRecord> zone_spawns;
    zone_spawns.reserve(16);

    for (auto& queue_item : spawn_queue_) {
                if (!queue_item.has_candidates()) continue;
                if (current_room_) {
                        bool has_area = false;
                        bool has_asset = false;
                        for (const auto& c : queue_item.candidates) {
                                if (c.info) { has_asset = true; break; }
                                if (!c.name.empty() && current_room_->find_area(c.name) != nullptr) {
                                        has_area = true;
                                }
                        }
                        if (has_area && !has_asset) {
                                continue;
                        }
                }

                if (current_room_) {
                        bool has_area = false;
                        bool has_asset = false;
                        for (const auto& c : queue_item.candidates) {
                                if (c.info) { has_asset = true; break; }
                                if (!c.name.empty() && current_room_->find_area(c.name) != nullptr) {
                                        has_area = true;
                                }
                        }
                        if (has_area && !has_asset) {
                                continue;
                        }
                }
                const std::string& pos = queue_item.position;

                if (current_room_ && !queue_item.link_area_name.empty()) {
                        Area* link = current_room_->find_area(queue_item.link_area_name);
                        ctx.set_clip_area(link);
                } else {
                        ctx.set_clip_area(nullptr);
                }

                if (queue_item.name == "batch_map_assets") {

                        int batch_resolution = queue_item.grid_resolution > 0 ? queue_item.grid_resolution : resolution;
                        Check batch_checker(false);
                        batch_checker.begin_session(grid_service, batch_resolution);
                        vibble::grid::Occupancy batch_occupancy(area, batch_resolution, grid_service);
                        SpawnContext batch_ctx(rng_, batch_checker, exclusion_zones, asset_info_library_, all_, asset_library_, grid_service, &batch_occupancy);

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

                        auto vertices = batch_occupancy.vertices_in_area(area);
                        if (vertices.empty()) {
                                batch_checker.reset_session();
                                continue;
                        }
                        std::shuffle(vertices.begin(), vertices.end(), batch_ctx.rng());

                        for (auto* vertex : vertices) {
                                if (!vertex) continue;
                                SDL_Point spawn_pos{ vertex->world.x, vertex->world.y };
                                spawn_pos = apply_map_grid_jitter(map_grid_settings_, spawn_pos, batch_ctx.rng(), area);
                                bool placed = false;
                                std::vector<double> attempt_weights = base_weights;
                                const size_t max_candidate_attempts = queue_item.candidates.size();
                                const bool enforce_spacing = queue_item.check_min_spacing;
                                for (size_t attempt = 0; attempt < max_candidate_attempts; ++attempt) {
                                        double total_weight = std::accumulate(attempt_weights.begin(), attempt_weights.end(), 0.0);
                                        if (total_weight <= 0.0) break;
                                        std::discrete_distribution<size_t> dist(attempt_weights.begin(), attempt_weights.end());
                                        size_t idx = dist(batch_ctx.rng());
                                        if (idx >= queue_item.candidates.size()) break;
                                        if (attempt_weights[idx] <= 0.0) {
                                                attempt_weights[idx] = 0.0;
                                                continue;
                                        }
                                        const SpawnCandidate& candidate = queue_item.candidates[idx];

                                        if (candidate.is_null || !candidate.info) {
                                                batch_occupancy.set_occupied(vertex, true);
                                                placed = true;
                                                break;
                                        }
                                        if (batch_ctx.checker().check(candidate.info,
                                                                spawn_pos,
                                                                batch_ctx.exclusion_zones(),
                                                                batch_ctx.all_assets(),
                                                                true,
                                                                enforce_spacing,
                                                                false,
                                                                false,
                                                                5)) {
                                                attempt_weights[idx] = 0.0;
                                                continue;
                                        }
                                        auto* result = batch_ctx.spawnAsset(candidate.name, candidate.info, area, spawn_pos, 0, nullptr, queue_item.spawn_id, queue_item.position);
                                        if (!result) {
                                                attempt_weights[idx] = 0.0;
                                                continue;
                                        }
                                        const bool track_spacing = batch_ctx.track_spacing_for(result->info, enforce_spacing);
                                        batch_ctx.checker().register_asset(result, enforce_spacing, track_spacing);
                                        batch_occupancy.set_occupied(vertex, true);

                                        if (candidate.info && candidate.info->type == std::string("zone_asset")) {
                                                const Area* region_area = batch_ctx.clip_area() ? batch_ctx.clip_area() : &area;
                                                zone_spawns.push_back(ZoneSpawnRecord{ result, region_area, queue_item.adjust_geometry_to_room });
                                        }
                                        placed = true;
                                        break;
                                }
                                if (!placed) {
                                        batch_occupancy.set_occupied(vertex, true);
                                }
                        }
                        batch_checker.reset_session();
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

                if (!ctx.all_assets().empty()) {
                        Asset* last = ctx.all_assets().back().get();
                        if (last && last->info && last->info->type == std::string("zone_asset")) {
                                const Area* region_area = ctx.clip_area() ? ctx.clip_area() : &area;
                                zone_spawns.push_back(ZoneSpawnRecord{ last, region_area, queue_item.adjust_geometry_to_room });
                        }
                }
        }
        checker_.reset_session();

        if (!zone_spawns.empty()) {
                for (const auto& rec : zone_spawns) {
                        if (!rec.asset || !rec.asset->info) continue;
                        auto info = rec.asset->info;

                        Area zone_world = rec.asset->get_area("zone");
                        if (zone_world.get_points().size() < 3) {
                                continue;
                        }
                        const Area* region_area = rec.region ? rec.region : &area;

                        if (rec.adjust && region_area) {
                                auto b = region_area->get_bounds();
                                const int region_w = std::max(1, std::get<2>(b) - std::get<0>(b));
                                const int region_h = std::max(1, std::get<3>(b) - std::get<1>(b));
                                const int origin_w = std::max(1, info->original_canvas_width);
                                const int origin_h = std::max(1, info->original_canvas_height);
                                const double sx = static_cast<double>(region_w) / static_cast<double>(origin_w);
                                const double sy = static_cast<double>(region_h) / static_cast<double>(origin_h);
                                std::vector<SDL_Point> adjusted;
                                adjusted.reserve(zone_world.get_points().size());
                                const SDL_Point anchor = rec.asset->pos;
                                for (const auto& p : zone_world.get_points()) {
                                        const int dx = p.x - anchor.x;
                                        const int dy = p.y - anchor.y;
                                        const int nx = anchor.x + static_cast<int>(std::llround(static_cast<double>(dx) * sx));
                                        const int ny = anchor.y + static_cast<int>(std::llround(static_cast<double>(dy) * sy));
                                        adjusted.push_back(SDL_Point{nx, ny});
                                }
                                Area adjusted_world(zone_world.get_name(), adjusted, zone_world.resolution());
                                adjusted_world.set_type(zone_world.get_type());
                                zone_world = std::move(adjusted_world);
                        }

                        std::unordered_map<std::string, Area> area_lookup;
                        for (const auto& named : info->areas) {
                                if (!named.area) continue;
                                try {
                                        Area world_area = rec.asset->get_area(named.name);
                                        if (world_area.get_points().size() >= 3) {
                                                area_lookup.insert_or_assign(named.name, std::move(world_area));
                                        }
                                } catch (...) {
                                }
                        }

                        std::vector<nlohmann::json> sources;
                        sources.push_back(info->spawn_groups_payload());
                        AssetSpawnPlanner planner2(sources, zone_world, *asset_library_);
                        spawn_children(zone_world, area_lookup, &planner2);
                }
        }
}

void AssetSpawner::run_edge_spawning(const Area& area) {
        auto point_in_exclusion = [&](const SDL_Point& pt) {
                return std::any_of(exclusion_zones.begin(), exclusion_zones.end(),
                [&](const Area& zone) { return zone.contains_point(pt); });
};

        vibble::grid::Grid& grid_service = vibble::grid::global_grid();
        auto spacing_names = collect_spacing_asset_names(spawn_queue_);
        for (auto& queue_item : spawn_queue_) {
                if (!queue_item.has_candidates()) continue;

                int edge_resolution = 5;
                auto it_res = group_resolution_map_.find(queue_item.spawn_id);
                if (it_res != group_resolution_map_.end()) {
                        edge_resolution = it_res->second;
                }
                edge_resolution = vibble::grid::clamp_resolution(edge_resolution);

                checker_.begin_session(grid_service, edge_resolution);

                vibble::grid::Occupancy occupancy(area, edge_resolution, grid_service);
                SpawnContext ctx(rng_, checker_, exclusion_zones, asset_info_library_, all_, asset_library_, grid_service, &occupancy);
                ctx.set_spacing_filter(&spacing_names);
                ctx.set_map_grid_settings(map_grid_settings_);
                ctx.set_spawn_resolution(edge_resolution);
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

                        const bool enforce_spacing = queue_item.check_min_spacing;
                        const SpawnCandidate* candidate = queue_item.select_candidate(ctx.rng());

                        if (!candidate || candidate->is_null) {
                                occupancy.set_occupied(vertex, true);
                                continue;
                        }

                        if (ctx.checker().check(candidate->info,
                                                spawn_pos,
                                                ctx.exclusion_zones(),
                                                ctx.all_assets(),
                                                true,
                                                enforce_spacing,
                                                true,
                                                false,
                                                5)) {
                                occupancy.set_occupied(vertex, true);
                                continue;
                        }

                        auto* result = ctx.spawnAsset(candidate->name, candidate->info, area, spawn_pos, 0, nullptr, queue_item.spawn_id, queue_item.position);
                        if (result) {
                                ctx.checker().register_asset(result, enforce_spacing, false);
                        }

                        occupancy.set_occupied(vertex, true);
                }
                checker_.reset_session();
        }
}

void AssetSpawner::run_child_spawning(AssetSpawnPlanner* planner,
                                      const Area& default_area,
                                      const std::unordered_map<std::string, Area>& area_lookup) {
        asset_info_library_ = asset_library_->all();
        spawn_queue_ = planner->get_spawn_queue();
        auto spacing_names = collect_spacing_asset_names(spawn_queue_);

        vibble::grid::Grid& grid_service = vibble::grid::global_grid();
        const int resolution = std::max(0, map_grid_settings_.resolution);
        checker_.begin_session(grid_service, resolution);
        ExactSpawner exact;
        CenterSpawner center;
        RandomSpawner random;
        PerimeterSpawner perimeter;
        EdgeSpawner edge;
        PercentSpawner percent;

        struct AreaOccupancy {
                const Area* area = nullptr;
                std::unique_ptr<vibble::grid::Occupancy> occupancy;
};

        std::vector<AreaOccupancy> occupancy_cache;
        occupancy_cache.reserve(area_lookup.size() + 1);

        auto get_or_create_occupancy = [&](const Area* area) -> vibble::grid::Occupancy* {
                if (!area) {
                        return nullptr;
                }
                auto it = std::find_if(occupancy_cache.begin(),
                                       occupancy_cache.end(),
                                       [&](const AreaOccupancy& entry) { return entry.area == area; });
                if (it == occupancy_cache.end()) {
                        AreaOccupancy entry;
                        entry.area = area;
                        entry.occupancy = std::make_unique<vibble::grid::Occupancy>(*area, resolution, grid_service, true);
                        occupancy_cache.push_back(std::move(entry));
                        return occupancy_cache.back().occupancy.get();
                }
                return it->occupancy.get();
};

        for (auto& queue_item : spawn_queue_) {
                if (!queue_item.has_candidates()) continue;

                const Area* target_area = &default_area;
                if (!queue_item.link_area_name.empty()) {
                        auto it = area_lookup.find(queue_item.link_area_name);
                        if (it != area_lookup.end()) {
                                target_area = &it->second;
                        }
                }
                if (!target_area) {
                        continue;
                }

                vibble::grid::Occupancy* occupancy = get_or_create_occupancy(target_area);
                if (!occupancy) {
                        continue;
                }

                SpawnContext ctx(rng_, checker_, exclusion_zones, asset_info_library_, all_, asset_library_, grid_service, occupancy);
                ctx.set_spacing_filter(&spacing_names);
                ctx.set_map_grid_settings(map_grid_settings_);
                ctx.set_spawn_resolution(resolution);
                ctx.set_trail_areas({});
                ctx.set_clip_area(target_area);
                ctx.set_checks_enabled(false);
                ctx.set_allow_partial_clip_overlap(true);

                const std::string& pos = queue_item.position;
                if (pos == "Exact" || pos == "Exact Position") {
                        exact.spawn(queue_item, target_area, ctx);
                } else if (pos == "Center") {
                        center.spawn(queue_item, target_area, ctx);
                } else if (pos == "Perimeter") {
                        perimeter.spawn(queue_item, target_area, ctx);
                } else if (pos == "Edge") {
                        edge.spawn(queue_item, target_area, ctx);
                } else if (pos == "Percent") {
                        percent.spawn(queue_item, target_area, ctx);
                } else {
                        random.spawn(queue_item, target_area, ctx);
                }
        }
        checker_.reset_session();
}
