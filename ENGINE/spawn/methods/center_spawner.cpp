#include "center_spawner.hpp"
#include "spawn_context.hpp"
#include "check.hpp"
#include "asset_spawn_planner.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include "spawn_logger.hpp"
void CenterSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
	if (!item.info || !area) return;
	const int Y_SHIFT = 200;
        SDL_Point center = ctx.get_area_center(*area);
        center.y -= Y_SHIFT;
        if (ctx.checker().check(item.info, center, ctx.exclusion_zones(), ctx.all_assets(),
     item.check_overlap, item.check_min_spacing, false, 5)) {
                ctx.logger().output_and_log(item.name, item.quantity, 0, 1, 1, "center");
                return;
        }
        auto* result = ctx.spawnAsset(item.name, item.info, *area, center, 0, nullptr, item.spawn_id, item.position);
	int spawned = result ? 1 : 0;
	ctx.logger().progress(item.info, spawned, item.quantity);
	ctx.logger().output_and_log(item.name, item.quantity, spawned, 1, 1, "center");
}
