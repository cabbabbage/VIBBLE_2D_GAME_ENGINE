#include "random_spawner.hpp"

#include "utils/spawn_context.hpp"
#include "check.hpp"
#include "asset_spawn_planner.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include "spawn_logger.hpp"

void RandomSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
    if (!item.info || item.quantity <= 0 || !area) return;

    int spawned = 0, attempts = 0;
    int max_attempts = item.quantity * 10;

    while (spawned < item.quantity && attempts < max_attempts) {
        auto pos = ctx.get_point_within_area(*area);
        ++attempts;

        if (!area->contains_point(pos)) continue;
        if (ctx.checker().check(item.info, pos.first, pos.second, ctx.exclusion_zones(), ctx.all_assets(),
                                true, true, 5)) continue;

        ctx.spawnAsset(item.name, item.info, *area, pos.first, pos.second, 0, nullptr);
        ++spawned;
        ctx.logger().progress(item.info, spawned, item.quantity);
    }

    ctx.logger().output_and_log(item.name, item.quantity, spawned, attempts, max_attempts, "random");
}
