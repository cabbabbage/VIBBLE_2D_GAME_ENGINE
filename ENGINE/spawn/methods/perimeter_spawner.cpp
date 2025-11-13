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
#include "utils/relative_room_position.hpp"

void PerimeterSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
    if (!area || item.quantity <= 0 || !item.has_candidates()) return;

    const int R = item.perimeter_radius;
    if (R <= 0) return;

    auto [minx, miny, maxx, maxy] = area->get_bounds();
    const int curr_w = std::max(1, maxx - minx);
    const int curr_h = std::max(1, maxy - miny);

    SDL_Point room_center = ctx.get_area_center(*area);
    RelativeRoomPosition relative(item.exact_offset, item.exact_origin_w, item.exact_origin_h);
    SDL_Point circle_center = relative.resolve(room_center, curr_w, curr_h);

    std::uniform_real_distribution<double> phase_dist(0.0, 2.0 * M_PI);
    const double start = phase_dist(ctx.rng());
    const double step  = (item.quantity > 0) ? (2.0 * M_PI / static_cast<double>(item.quantity)) : 0.0;

    for (int i = 0; i < item.quantity; ++i) {
        const double angle = start + step * static_cast<double>(i);
        const int x = circle_center.x + static_cast<int>(std::lround(R * std::cos(angle)));
        const int y = circle_center.y + static_cast<int>(std::lround(R * std::sin(angle)));

        SDL_Point pos{x, y};
        const SpawnCandidate* candidate = item.select_candidate(ctx.rng());
        if (!candidate || candidate->is_null || !candidate->info) continue;

        auto& info = candidate->info;

        if (!ctx.position_allowed(*area, pos)) {
            continue;
        }

        const bool enforce_spacing = item.check_min_spacing;
        if (ctx.checks_enabled() &&
            ctx.checker().check(info,
                                pos,
                                ctx.exclusion_zones(),
                                ctx.all_assets(),
                                false,
                                enforce_spacing,
                                false,
                                false,
                                5)) {
            continue;
        }

        if (auto* spawned = ctx.spawnAsset(candidate->name, info, *area, pos, 0, nullptr, item.spawn_id, item.position)) {
            if (ctx.checks_enabled()) {
                const bool track_spacing = ctx.track_spacing_for(spawned->info, enforce_spacing);
                ctx.checker().register_asset(spawned, enforce_spacing, track_spacing);
            }
        }
    }

}
