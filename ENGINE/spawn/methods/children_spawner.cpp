#include "children_spawner.hpp"
#include <SDL.h>
#include <vector>
#include "spawn_context.hpp"
#include "check.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include "spawn_logger.hpp"

void ChildrenSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
    if (!item.info || !area) return;
    int quantity = item.quantity > 0 ? item.quantity : 1;

    int spawned = 0;
    int attempts = 0;
    int max_attempts = quantity * 50; // be generous within the child area

    while (spawned < quantity && attempts < max_attempts) {
        ++attempts;
        SDL_Point pos = ctx.get_point_within_area(*area);
        if (!area->contains_point(pos)) continue;

        // Children are not subject to grid or spacing/min-distance rules.
        // We still keep exclusion check disabled to allow any point in child area.
        bool violate = ctx.checker().check(item.info,
                                           pos,
                                           /*exclusion_areas*/ std::vector<Area>{},
                                           ctx.all_assets(),
                                           /*check_spacing*/ false,
                                           /*check_min_distance*/ false,
                                           /*check_min_distance_all*/ false,
                                           /*num_neighbors*/ 0);
        if (violate) continue;

        auto* result = ctx.spawnAsset(item.name, item.info, *area, pos, 0, nullptr, item.spawn_id, std::string("ChildRandom"));
        if (result) {
            ++spawned;
            ctx.logger().progress(item.info, spawned, quantity);
        }
    }
    ctx.logger().output_and_log(item.name, quantity, spawned, attempts, max_attempts, "children_random");
}
