#include "percent_spawner.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include "spawn_context.hpp"
#include "check.hpp"
#include "asset_spawn_planner.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"

void PercentSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
    if (!area || item.quantity <= 0 || !item.has_candidates()) return;

    auto [minx, miny, maxx, maxy] = area->get_bounds();
    const int w = std::max(1, maxx - minx);
    const int h = std::max(1, maxy - miny);

    SDL_Point center = ctx.get_area_center(*area);

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
            center.x + static_cast<int>(std::lround(offset_x)), center.y + static_cast<int>(std::lround(offset_y)) };

        vibble::grid::Occupancy::Vertex* snapped = ctx.occupancy() ? ctx.occupancy()->nearest_vertex(final_pos) : nullptr;
        if (snapped) {
            final_pos = snapped->world;
        }

        if (!ctx.position_allowed(*area, final_pos)) {
            continue;
        }

        const SpawnCandidate* candidate = item.select_candidate(ctx.rng());
        if (!candidate || candidate->is_null || !candidate->info) {
            ++slots_used;
            continue;
        }

        auto& info = candidate->info;
        const bool enforce_spacing = item.check_min_spacing;
        if (ctx.checks_enabled() &&
            ctx.checker().check(info,
                                final_pos,
                                ctx.exclusion_zones(),
                                ctx.all_assets(),
                                true,
                                enforce_spacing,
                                false,
                                false,
                                5)) {
            continue;
        }

        auto* result = ctx.spawnAsset(candidate->name, info, *area, final_pos, 0, nullptr, item.spawn_id, item.position);
        if (!result) {
            ++slots_used;
            continue;
        }

        if (ctx.checks_enabled()) {
            const bool track_spacing = ctx.track_spacing_for(result->info, enforce_spacing);
            ctx.checker().register_asset(result, enforce_spacing, track_spacing);
        }

        if (snapped && ctx.occupancy()) {
            ctx.occupancy()->set_occupied(snapped, true);
        }

        ++slots_used;
    }

}

