#include "asset_children_spawner.hpp"
#include <SDL.h>
#include <vector>
#include "spawn_context.hpp"
#include "check.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"

void ChildrenSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
    if (!area || !item.has_candidates()) return;
    int quantity = item.quantity > 0 ? item.quantity : 1;

    int attempts = 0;
    int slots_used = 0;
    int max_attempts = quantity * 50;

    while (slots_used < quantity && attempts < max_attempts) {
        ++attempts;
        SDL_Point pos = ctx.get_point_within_area(*area);
        if (!ctx.position_allowed(*area, pos)) continue;

        const SpawnCandidate* candidate = item.select_candidate(ctx.rng());
        if (!candidate || candidate->is_null || !candidate->info) {
            ++slots_used;
            continue;
        }

        const bool enforce_spacing = item.check_min_spacing;
        if (ctx.checks_enabled() &&
            ctx.checker().check(candidate->info,
                                 pos,
                                 std::vector<Area>{},
                                 ctx.all_assets(),
                                 false,
                                 enforce_spacing,
                                 false,
                                 false,
                                 0)) continue;

        auto* result = ctx.spawnAsset(candidate->name, candidate->info, *area, pos, 0, nullptr, item.spawn_id, std::string("ChildRandom"));
        if (!result) {
            ++slots_used;
            continue;
        }

        if (ctx.checks_enabled()) {
            const bool track_spacing = ctx.track_spacing_for(result->info, enforce_spacing);
            ctx.checker().register_asset(result, enforce_spacing, track_spacing);
        }

        ++slots_used;
    }
}
