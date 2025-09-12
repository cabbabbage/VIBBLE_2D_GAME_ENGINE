#include "random_spawner.hpp"
#include "spawn_context.hpp"
#include "check.hpp"
#include "asset_spawn_planner.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include "spawn_logger.hpp"
void RandomSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
    if (!item.info || item.quantity <= 0 || !area) return;
    int spawned = 0, attempts = 0;
    int max_attempts = item.quantity * 20;
    while (spawned < item.quantity && attempts < max_attempts) {
        auto* gp = ctx.grid() ? ctx.grid()->get_rnd_point_in_area(*area, ctx.rng()) : nullptr;
        ++attempts;
        if (!gp) break; // no free grid point available in area
        const SDL_Point pos = gp->pos;
        if (!area->contains_point(pos)) continue;
        if (ctx.checker().check(item.info, pos, ctx.exclusion_zones(), ctx.all_assets(), true, true, true, 5)) continue;
        auto* result = ctx.spawnAsset(item.name, item.info, *area, pos, 0, nullptr, item.spawn_id, item.position);
        if (result && ctx.grid()) {
            ctx.grid()->set_occupied(gp, true);
        }
        ++spawned;
        ctx.logger().progress(item.info, spawned, item.quantity);
    }
    ctx.logger().output_and_log(item.name, item.quantity, spawned, attempts, max_attempts, "random");
}
