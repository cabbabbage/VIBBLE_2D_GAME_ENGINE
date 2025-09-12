#include "distributed_spawner.hpp"
#include <algorithm>
#include <random>
#include "spawn_context.hpp"
#include "check.hpp"
#include "asset_spawn_planner.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include "spawn_logger.hpp"
void DistributedSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
    if (!item.info || item.quantity <= 0 || !area) return;
    if (!ctx.grid()) return;
    auto points = ctx.grid()->get_all_points_in_area(*area);
    // Shuffle grid points for distribution
    std::shuffle(points.begin(), points.end(), ctx.rng());
    int placed = 0, attempts = 0, max_attempts = item.quantity * 10;
    for (auto* gp : points) {
        if (!gp) continue;
        if (placed >= item.quantity || attempts >= max_attempts) break;
        ++attempts;
        // Respect empty_grid_spaces by skipping some points
        if (std::uniform_int_distribution<int>(0, 99)(ctx.rng()) < item.empty_grid_spaces) continue;
        const SDL_Point pos = gp->pos;
        if (!area->contains_point(pos)) continue;
        if (ctx.checker().check(item.info, pos, ctx.exclusion_zones(), ctx.all_assets(), true, false, true, 5)) continue;
        auto* result = ctx.spawnAsset(item.name, item.info, *area, pos, 0, nullptr, item.spawn_id, item.position);
        if (result) {
            ctx.grid()->set_occupied(gp, true);
            ++placed;
            ctx.logger().progress(item.info, placed, item.quantity);
        }
    }
    ctx.logger().output_and_log(item.name, item.quantity, placed, attempts, max_attempts, "distributed");
}
