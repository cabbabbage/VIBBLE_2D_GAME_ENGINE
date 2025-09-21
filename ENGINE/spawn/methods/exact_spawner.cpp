#include "exact_spawner.hpp"
#include <cmath>
#include <algorithm>

#include "spawn_context.hpp"
#include "check.hpp"
#include "asset_spawn_planner.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include "spawn_logger.hpp"

void ExactSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
    if (!area || !item.has_candidates() || item.quantity <= 0) return;

    auto [minx, miny, maxx, maxy] = area->get_bounds();
    const int curr_w = std::max(1, maxx - minx);
    const int curr_h = std::max(1, maxy - miny);

    // Fallbacks: planner should have filled these, but guard just in case.
    const int orig_w = (item.exact_origin_w > 0) ? item.exact_origin_w : curr_w;
    const int orig_h = (item.exact_origin_h > 0) ? item.exact_origin_h : curr_h;

    const double rx = static_cast<double>(curr_w) / static_cast<double>(std::max(1, orig_w));
    const double ry = static_cast<double>(curr_h) / static_cast<double>(std::max(1, orig_h));

    SDL_Point center = ctx.get_area_center(*area);
    SDL_Point final_pos{
        center.x + static_cast<int>(std::lround(item.exact_offset.x * rx)),
        center.y + static_cast<int>(std::lround(item.exact_offset.y * ry))
    };

    int attempts = 0;
    int spawned  = 0;
    const int target_attempts = item.quantity;

    while (attempts < target_attempts) {
        ++attempts;

        const SpawnCandidate* candidate = item.select_candidate(ctx.rng());
        if (!candidate || candidate->is_null || !candidate->info) continue;
        auto& info = candidate->info;

        SDL_Point pos = final_pos;

        // Optional grid snap if available
        MapGrid::Point* snapped = nullptr;
        if (auto* g = ctx.grid()) {
            snapped = g->get_nearest_point(pos);
            if (snapped) pos = snapped->pos;
        }

        if (ctx.checker().check(info, pos, ctx.exclusion_zones(), ctx.all_assets(),
                                item.check_spacing, /*check_min_spacing*/ false,
                                /*unused*/ false, /*tries*/ 5)) {
            continue;
        }

        auto* result = ctx.spawnAsset(candidate->name, info, *area, pos, 0, nullptr, item.spawn_id, item.position);
        if (!result) continue;

        if (snapped && ctx.grid()) {
            ctx.grid()->set_occupied(snapped, true);
        }

        ++spawned;
        ctx.logger().progress(info, spawned, target_attempts);
    }

    ctx.logger().output_and_log(item.name, target_attempts, spawned, attempts, attempts, "exact");
}
