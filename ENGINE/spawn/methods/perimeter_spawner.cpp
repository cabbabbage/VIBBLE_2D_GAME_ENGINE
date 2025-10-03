#include "perimeter_spawner.hpp"
#include <cmath>
#include <vector>
#include <algorithm>
#include <random>
#include <SDL.h>

#include "spawn_context.hpp"
#include "check.hpp"
#include "asset_spawn_planner.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include "spawn_logger.hpp"

void PerimeterSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
    if (!area || item.quantity <= 0 || !item.has_candidates()) return;

    const int R = item.perimeter_radius;
    if (R <= 0) return;

    auto [minx, miny, maxx, maxy] = area->get_bounds();
    const int curr_w = std::max(1, maxx - minx);
    const int curr_h = std::max(1, maxy - miny);

    const int orig_w = std::max(1, item.exact_origin_w);
    const int orig_h = std::max(1, item.exact_origin_h);

    const double rx = static_cast<double>(curr_w) / static_cast<double>(orig_w);
    const double ry = static_cast<double>(curr_h) / static_cast<double>(orig_h);

    SDL_Point room_center = ctx.get_area_center(*area);
    SDL_Point circle_center{
        room_center.x + static_cast<int>(std::lround(item.exact_offset.x * rx)), room_center.y + static_cast<int>(std::lround(item.exact_offset.y * ry)) };

    std::uniform_real_distribution<double> phase_dist(0.0, 2.0 * M_PI);
    const double start = phase_dist(ctx.rng());
    const double step  = (item.quantity > 0) ? (2.0 * M_PI / static_cast<double>(item.quantity)) : 0.0;

    int placed = 0;
    int attempts = 0;

    for (int i = 0; i < item.quantity; ++i) {
        const double angle = start + step * static_cast<double>(i);
        const int x = circle_center.x + static_cast<int>(std::lround(R * std::cos(angle)));
        const int y = circle_center.y + static_cast<int>(std::lround(R * std::sin(angle)));

        ++attempts;

        SDL_Point pos{x, y};
        const SpawnCandidate* candidate = item.select_candidate(ctx.rng());
        if (!candidate || candidate->is_null || !candidate->info) continue;

        auto& info = candidate->info;

        if (ctx.checker().check(info, pos, ctx.exclusion_zones(), ctx.all_assets(),
                                item.check_spacing,  false,
                                 false,  5)) {
            continue;
        }

        ctx.spawnAsset(candidate->name, info, *area, pos, 0, nullptr, item.spawn_id, item.position);
        ++placed;
        ctx.logger().progress(info, placed, item.quantity);
    }

    ctx.logger().output_and_log(item.name, item.quantity, placed, attempts, item.quantity, "perimeter");
}
