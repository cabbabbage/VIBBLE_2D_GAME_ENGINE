#include "exact_spawner.hpp"

#include "utils/spawn_context.hpp"
#include "check.hpp"
#include "asset_spawn_planner.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include "spawn_logger.hpp"

void ExactSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
    if (!item.info || item.ep_x < 0 || item.ep_y < 0 || !area) return;

    auto [minx, miny, maxx, maxy] = area->get_bounds();
    int width = maxx - minx;
    int height = maxy - miny;

    auto center = ctx.get_area_center(*area);
    double norm_x = (item.ep_x - 50.0) / 100.0;
    double norm_y = (item.ep_y - 50.0) / 100.0;

    int final_x = center.first + static_cast<int>(norm_x * width);
    int final_y = center.second + static_cast<int>(norm_y * height);

    if (ctx.checker().check(item.info, final_x, final_y, ctx.exclusion_zones(), ctx.all_assets(),
                            item.check_overlap, item.check_min_spacing, false, 5)) {
        ctx.logger().output_and_log(item.name, item.quantity, 0, 0, 0, "exact");
        return;
    }

    ctx.spawnAsset(item.name, item.info, *area, final_x, final_y, 0, nullptr);
    ctx.logger().progress(item.info, 1, item.quantity);
    ctx.logger().output_and_log(item.name, item.quantity, 1, 1, 1, "exact");
}
