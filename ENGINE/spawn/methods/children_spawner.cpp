#include "children_spawner.hpp"
#include <SDL.h>
#include <vector>
#include "spawn_context.hpp"
#include "check.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include "spawn_logger.hpp"

void ChildrenSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
    if (!area || !item.has_candidates()) return;
    int quantity = item.quantity > 0 ? item.quantity : 1;

    int spawned = 0;
    int attempts = 0;
    int slots_used = 0;
    int max_attempts = quantity * 50;

    while (slots_used < quantity && attempts < max_attempts) {
        ++attempts;
        SDL_Point pos = ctx.get_point_within_area(*area);
        if (!area->contains_point(pos)) continue;

        const SpawnCandidate* candidate = item.select_candidate(ctx.rng());
        if (!candidate || candidate->is_null || !candidate->info) {
            ++slots_used;
            continue;
        }

        bool violate = ctx.checker().check(candidate->info,
                                           pos,
                                            std::vector<Area>{},
                                           ctx.all_assets(),
                                            false,
                                            false,
                                            false,
                                            0);
        if (violate) continue;

        auto* result = ctx.spawnAsset(candidate->name, candidate->info, *area, pos, 0, nullptr, item.spawn_id, std::string("ChildRandom"));
        if (!result) {
            ++slots_used;
            continue;
        }

        ++spawned;
        ++slots_used;
        ctx.logger().progress(candidate->info, spawned, quantity);
    }
    ctx.logger().output_and_log(item.name, quantity, spawned, attempts, max_attempts, "children_random");
}
