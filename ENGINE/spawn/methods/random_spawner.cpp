#include "random_spawner.hpp"
#include <algorithm>
#include "spawn_context.hpp"
#include "check.hpp"
#include "asset_spawn_planner.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
void RandomSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
    if (!area || !item.has_candidates() || item.quantity <= 0) return;
    int attempt_slots_used = 0;
    int attempts = 0;
    const int desired_attempts = item.quantity;
    const int max_attempts = std::max(1, desired_attempts * 20);

    auto* occupancy = ctx.occupancy();
    const Area* sample_area = ctx.clip_area();
    if (!sample_area) {
        sample_area = area;
    }
    const Area* spawn_area = sample_area ? sample_area : area;

    if (!spawn_area || spawn_area->get_points().empty()) {
        return;
    }

    while (attempt_slots_used < desired_attempts && attempts < max_attempts) {
        vibble::grid::Occupancy::Vertex* vertex = occupancy ? occupancy->random_vertex_in_area(*spawn_area, ctx.rng()) : nullptr;
        ++attempts;
        if (!vertex) break;
        SDL_Point pos = vertex->world;

        if (spawn_area && !spawn_area->get_points().empty()) {
            pos = ctx.get_point_within_area(*spawn_area);
        }
        if (!ctx.position_allowed(*spawn_area, pos)) continue;

        const SpawnCandidate* candidate = item.select_candidate(ctx.rng());
        if (!candidate) {
            ++attempt_slots_used;
            continue;
        }
        if (candidate->is_null || !candidate->info) {
            ++attempt_slots_used;
            continue;
        }

        auto& info = candidate->info;
        const bool enforce_spacing = item.check_min_spacing;
        if (ctx.checks_enabled() &&
            ctx.checker().check(info,
                                pos,
                                ctx.exclusion_zones(),
                                ctx.all_assets(),
                                true,
                                enforce_spacing,
                                false,
                                false,
                                5)) {
            continue;
        }

        auto* result = ctx.spawnAsset(candidate->name, info, *spawn_area, pos, 0, nullptr, item.spawn_id, item.position);
        if (!result) {
            ++attempt_slots_used;
            continue;
        }

        if (ctx.checks_enabled()) {
            const bool track_spacing = ctx.track_spacing_for(result->info, enforce_spacing);
            ctx.checker().register_asset(result, enforce_spacing, track_spacing);
        }

        if (occupancy) {
            occupancy->set_occupied(vertex, true);
        }
        ++attempt_slots_used;
    }
}
