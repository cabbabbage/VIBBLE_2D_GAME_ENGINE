#include "center_spawner.hpp"

#include <SDL.h>

#include "spawn_context.hpp"
#include "check.hpp"
#include "asset_spawn_planner.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include "spawn_logger.hpp"

void CenterSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
    if (!area || !item.has_candidates() || item.quantity <= 0) return;

    SDL_Point center = ctx.get_area_center(*area);

    if (auto* g = ctx.grid()) {
        if (auto* np = g->get_nearest_point(center)) {
            center = np->pos;
        }
    }

    int attempts = 0;
    int spawned  = 0;
    const int target_attempts = item.quantity;

    while (attempts < target_attempts) {
        ++attempts;

        const SpawnCandidate* candidate = item.select_candidate(ctx.rng());
        if (!candidate || candidate->is_null || !candidate->info) continue;

        auto& info = candidate->info;

        if (ctx.checker().check(info, center, ctx.exclusion_zones(), ctx.all_assets(),
                                item.check_spacing,  false,
                                 false,  5)) {
            continue;
        }

        if (ctx.spawnAsset(candidate->name, info, *area, center, 0, nullptr, item.spawn_id, item.position)) {
            ++spawned;
            ctx.logger().progress(info, spawned, target_attempts);
        }
    }

    ctx.logger().output_and_log(item.name, target_attempts, spawned, attempts, target_attempts, "center");
}
