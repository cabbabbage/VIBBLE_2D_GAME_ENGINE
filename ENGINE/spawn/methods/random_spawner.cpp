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

    while (attempt_slots_used < desired_attempts && attempts < max_attempts) {
        vibble::grid::Occupancy::Vertex* vertex = occupancy ? occupancy->random_vertex_in_area(*area, ctx.rng()) : nullptr;
        ++attempts;
        if (!vertex) break;
        const SDL_Point pos = vertex->world;
        if (!area->contains_point(pos)) continue;

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
        if (ctx.checker().check(info,
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

        auto* result = ctx.spawnAsset(candidate->name, info, *area, pos, 0, nullptr, item.spawn_id, item.position);
        if (!result) {
            ++attempt_slots_used;
            continue;
        }

        ctx.checker().register_asset(result, enforce_spacing, true);

        if (occupancy) {
            occupancy->set_occupied(vertex, true);
        }
        ++attempt_slots_used;
    }
}
