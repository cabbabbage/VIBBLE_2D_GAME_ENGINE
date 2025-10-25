#include "center_spawner.hpp"

#include <SDL.h>

#include "spawn_context.hpp"
#include "check.hpp"
#include "asset_spawn_planner.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"

void CenterSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
    if (!area || !item.has_candidates() || item.quantity <= 0) return;

    SDL_Point center = ctx.get_area_center(*area);

    if (auto* occupancy = ctx.occupancy()) {
        if (auto* vertex = occupancy->nearest_vertex(center)) {
            center = vertex->world;
        }
    }

    int attempts = 0;
    const int target_attempts = item.quantity;

    while (attempts < target_attempts) {
        ++attempts;

        const SpawnCandidate* candidate = item.select_candidate(ctx.rng());
        if (!candidate || candidate->is_null || !candidate->info) continue;

        auto& info = candidate->info;

        const bool enforce_spacing = item.check_min_spacing;
        if (ctx.checker().check(info,
                                center,
                                ctx.exclusion_zones(),
                                ctx.all_assets(),
                                false,
                                enforce_spacing,
                                false,
                                false,
                                5)) {
            continue;
        }

        if (auto* spawned = ctx.spawnAsset(candidate->name, info, *area, center, 0, nullptr, item.spawn_id, item.position)) {
            ctx.checker().register_asset(spawned, enforce_spacing, true);
        }
    }

}
