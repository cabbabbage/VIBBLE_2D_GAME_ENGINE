#include "random_spawner.hpp"
#include <algorithm>
#include "spawn_context.hpp"
#include "check.hpp"
#include "asset_spawn_planner.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include "spawn_logger.hpp"
void RandomSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
    if (!area || !item.has_candidates() || item.quantity <= 0) return;
    int spawned = 0;
    int attempt_slots_used = 0;
    int attempts = 0;
    const int desired_attempts = item.quantity;
    const int max_attempts = std::max(1, desired_attempts * 20);

    while (attempt_slots_used < desired_attempts && attempts < max_attempts) {
        auto* gp = ctx.grid() ? ctx.grid()->get_rnd_point_in_area(*area, ctx.rng()) : nullptr;
        ++attempts;
        if (!gp) break;
        const SDL_Point pos = gp->pos;
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
        if (ctx.checker().check(info, pos, ctx.exclusion_zones(), ctx.all_assets(), true, true, true, 5)) {
            continue;
        }

        auto* result = ctx.spawnAsset(candidate->name, info, *area, pos, 0, nullptr, item.spawn_id, item.position);
        if (!result) {
            ++attempt_slots_used;
            continue;
        }

        if (ctx.grid()) {
            ctx.grid()->set_occupied(gp, true);
        }
        ++spawned;
        ++attempt_slots_used;
        ctx.logger().progress(info, spawned, desired_attempts);
    }
    ctx.logger().output_and_log(item.name, desired_attempts, spawned, attempts, max_attempts, "random");
}
