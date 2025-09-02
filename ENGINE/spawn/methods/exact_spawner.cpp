#include "exact_spawner.hpp"

#include "utils/spawn_context.hpp"
#include "check.hpp"
#include "asset_spawn_planner.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include "spawn_logger.hpp"
#include <algorithm>
#include <cmath>

void ExactSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
    if (!item.info || !area) return;

    // Determine room dimensions
    auto [minx, miny, maxx, maxy] = area->get_bounds();
    const int room_w = std::max(0, maxx - minx);
    const int room_h = std::max(0, maxy - miny);
    auto center = ctx.get_area_center(*area);

    int final_x = -1;
    int final_y = -1;

    // Preferred: displacement-based exact spawn scaled to current room size
    if (item.orig_room_width > 0 && item.orig_room_height > 0) {
        double sx = (room_w > 0) ? (static_cast<double>(room_w) / static_cast<double>(item.orig_room_width)) : 0.0;
        double sy = (room_h > 0) ? (static_cast<double>(room_h) / static_cast<double>(item.orig_room_height)) : 0.0;
        final_x = center.first  + static_cast<int>(std::lround(item.disp_x * sx));
        final_y = center.second + static_cast<int>(std::lround(item.disp_y * sy));
    }

    // Fallback: use absolute exact coordinates if supplied
    if (final_x < 0 || final_y < 0) {
        final_x = item.exact_x;
        final_y = item.exact_y;
    }

    if (final_x < 0 || final_y < 0) {
        // Missing required data
        ctx.logger().output_and_log(item.name, item.quantity, 0, 0, 0, "exact_missing_data");
        return;
    }

    // If the computed point is outside the area, skip
    if (!area->contains_point({final_x, final_y})) {
        ctx.logger().output_and_log(item.name, item.quantity, 0, 0, 0, "exact_out_of_bounds");
        return;
    }

    // Overlap/min-distance checks
    if (ctx.checker().check(item.info, final_x, final_y, ctx.exclusion_zones(), ctx.all_assets(),
                            item.check_min_spacing, false, 5)) {
        ctx.logger().output_and_log(item.name, item.quantity, 0, 0, 0, "exact_overlap");
        return;
    }

    ctx.spawnAsset(item.name, item.info, "exact", final_x, final_y, 0, nullptr, item.asset_id);
    ctx.logger().progress(item.info, 1, item.quantity);
    ctx.logger().output_and_log(item.name, item.quantity, 1, 1, 1, "exact");
}
