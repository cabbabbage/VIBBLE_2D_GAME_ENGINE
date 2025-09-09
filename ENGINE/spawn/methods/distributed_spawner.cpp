#include "distributed_spawner.hpp"
#include <algorithm>
#include <random>
#include "utils/spawn_context.hpp"
#include "check.hpp"
#include "asset_spawn_planner.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include "spawn_logger.hpp"
void DistributedSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
	if (!item.info || item.quantity <= 0 || !area) return;
	auto [minx, miny, maxx, maxy] = area->get_bounds();
	int w = maxx - minx;
	int h = maxy - miny;
	if (w <= 0 || h <= 0) return;
	int spacing = std::max(1, item.grid_spacing);
	int jitter  = std::max(0, item.jitter);
	int placed = 0, attempts = 0, max_attempts = item.quantity * 10;
	for (int x = minx; x <= maxx && placed < item.quantity && attempts < max_attempts; x += spacing) {
		for (int y = miny; y <= maxy && placed < item.quantity && attempts < max_attempts; y += spacing) {
			int cx = x + std::uniform_int_distribution<int>(-jitter, jitter)(ctx.rng());
			int cy = y + std::uniform_int_distribution<int>(-jitter, jitter)(ctx.rng());
			++attempts;
			if (std::uniform_int_distribution<int>(0, 99)(ctx.rng()) < item.empty_grid_spaces) continue;
                        if (!area->contains_point(SDL_Point{cx, cy})) continue;
                        if (ctx.checker().check(item.info, cx, cy, ctx.exclusion_zones(), ctx.all_assets(),
       true, false, true, 5)) continue;
                        ctx.spawnAsset(item.name, item.info, *area, SDL_Point{cx, cy}, 0, nullptr, item.spawn_id, item.position);
			++placed;
			ctx.logger().progress(item.info, placed, item.quantity);
		}
	}
	ctx.logger().output_and_log(item.name, item.quantity, placed, attempts, max_attempts, "distributed");
}
