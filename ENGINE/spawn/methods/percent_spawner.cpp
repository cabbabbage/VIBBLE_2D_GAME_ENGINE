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
    if (!area || item.quantity <= 0 || !item.has_candidates()) return;

    auto [minx, miny, maxx, maxy] = area->get_bounds();
    const int w = std::max(1, maxx - minx);
    const int h = std::max(1, maxy - miny);

    SDL_Point center = ctx.get_area_center(*area);

    int spawned = 0;
    int attempts = 0;
    int slots_used = 0;
    const int target_attempts = item.quantity;
    const int max_attempts = std::max(1, target_attempts * 20);

    constexpr int kDefaultMin = -100;
    constexpr int kDefaultMax = 100;

    std::uniform_int_distribution<int> dist_x(kDefaultMin, kDefaultMax);
    std::uniform_int_distribution<int> dist_y(kDefaultMin, kDefaultMax);

    while (slots_used < target_attempts && attempts < max_attempts) {
        ++attempts;

        const int px = dist_x(ctx.rng());
        const int py = dist_y(ctx.rng());

        const double offset_x = (px / 100.0) * (w / 2.0);
        const double offset_y = (py / 100.0) * (h / 2.0);

        SDL_Point final_pos{
            center.x + static_cast<int>(std::lround(offset_x)),
            center.y + static_cast<int>(std::lround(offset_y))
        };

        MapGrid::Point* snapped = ctx.grid() ? ctx.grid()->get_nearest_point(final_pos) : nullptr;
        if (snapped) {
            final_pos = snapped->pos;
        }

        const SpawnCandidate* candidate = item.select_candidate(ctx.rng());
        if (!candidate || candidate->is_null || !candidate->info) {
            ++slots_used;
            continue;
        }

        auto& info = candidate->info;
        if (ctx.checker().check(info, final_pos, ctx.exclusion_zones(), ctx.all_assets(),
                                item.check_spacing, item.check_min_spacing,
                                /*allow_retry*/ false, /*tries*/ 5)) {
            continue;
        }

        auto* result = ctx.spawnAsset(candidate->name, info, *area, final_pos, 0, nullptr, item.spawn_id, item.position);
        if (!result) {
            ++slots_used;
            continue;
        }

        if (snapped && ctx.grid()) {
            ctx.grid()->set_occupied(snapped, true);
        }

        ++spawned;
        ++slots_used;
        ctx.logger().progress(info, spawned, target_attempts);
    }

    ctx.logger().output_and_log(item.name, target_attempts, spawned, attempts, max_attempts, "percent");
}

