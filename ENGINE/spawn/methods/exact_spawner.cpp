#include "exact_spawner.hpp"
#include <cmath>
#include <algorithm>

#include "spawn_context.hpp"
#include "check.hpp"
#include "asset_spawn_planner.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include "utils/relative_room_position.hpp"

void ExactSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
    if (!area || !item.has_candidates() || item.quantity <= 0) return;

    auto [minx, miny, maxx, maxy] = area->get_bounds();
    const int curr_w = std::max(1, maxx - minx);
    const int curr_h = std::max(1, maxy - miny);

    SDL_Point center = ctx.get_area_center(*area);
    RelativeRoomPosition relative(item.exact_offset, item.exact_origin_w, item.exact_origin_h);
    SDL_Point final_pos = relative.resolve(center, curr_w, curr_h);

    int attempts = 0;
    const int target_attempts = item.quantity;

    while (attempts < target_attempts) {
        ++attempts;

        const SpawnCandidate* candidate = item.select_candidate(ctx.rng());
        if (!candidate || candidate->is_null || !candidate->info) continue;
        auto& info = candidate->info;

        SDL_Point pos = final_pos;

        vibble::grid::Occupancy::Vertex* snapped = nullptr;
        if (auto* occupancy = ctx.occupancy()) {
            snapped = occupancy->nearest_vertex(pos);
            if (snapped) pos = snapped->world;
        }

        if (ctx.checker().check(info, pos, ctx.exclusion_zones(), ctx.all_assets(),
                                false, false, false, 5)) {
            continue;
        }

        auto* result = ctx.spawnAsset(candidate->name, info, *area, pos, 0, nullptr, item.spawn_id, item.position);
        if (!result) continue;

        if (snapped && ctx.occupancy()) {
            ctx.occupancy()->set_occupied(snapped, true);
        }

    }

}
