#include "percent_spawner.hpp"
#include <algorithm>
#include <cmath>
#include <random>
#include "spawn_context.hpp"
#include "check.hpp"
#include "asset_spawn_planner.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include "spawn_logger.hpp"

void PercentSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
    if (!item.info || item.quantity <= 0 || !area) return;
    auto [minx, miny, maxx, maxy] = area->get_bounds();
    int w = std::max(1, maxx - minx);
    int h = std::max(1, maxy - miny);
    SDL_Point center = ctx.get_area_center(*area);
    int spawned = 0, attempts = 0;
    int max_attempts = item.quantity * 20;
    int xmin = std::min(item.percent_x_min, item.percent_x_max);
    int xmax = std::max(item.percent_x_min, item.percent_x_max);
    int ymin = std::min(item.percent_y_min, item.percent_y_max);
    int ymax = std::max(item.percent_y_min, item.percent_y_max);
    std::uniform_int_distribution<int> dist_x(xmin, xmax);
    std::uniform_int_distribution<int> dist_y(ymin, ymax);
    while (spawned < item.quantity && attempts < max_attempts) {
        ++attempts;
        int px = dist_x(ctx.rng());
        int py = dist_y(ctx.rng());
        double offset_x = (px / 100.0) * (w / 2.0);
        double offset_y = (py / 100.0) * (h / 2.0);
        SDL_Point final_pos{ center.x + static_cast<int>(std::lround(offset_x)),
                             center.y + static_cast<int>(std::lround(offset_y)) };
        MapGrid::Point* snapped = ctx.grid() ? ctx.grid()->get_nearest_point(final_pos) : nullptr;
        if (snapped) final_pos = snapped->pos;
        if (ctx.checker().check(item.info, final_pos, ctx.exclusion_zones(), ctx.all_assets(), true, true, false, 5)) continue;
        auto* result = ctx.spawnAsset(item.name, item.info, *area, final_pos, 0, nullptr, item.spawn_id, item.position);
        if (result && snapped && ctx.grid()) ctx.grid()->set_occupied(snapped, true);
        ++spawned;
        ctx.logger().progress(item.info, spawned, item.quantity);
    }
    ctx.logger().output_and_log(item.name, item.quantity, spawned, attempts, max_attempts, "percent");
}

