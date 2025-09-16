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
        int curr_w = std::max(1, maxx - minx);
        int curr_h = std::max(1, maxy - miny);
        int attempts = 0;
        int spawned = 0;
        const int target_attempts = item.quantity;

        while (attempts < target_attempts) {
                const SpawnCandidate* candidate = item.select_candidate(ctx.rng());
                ++attempts;
                if (!candidate || candidate->is_null || !candidate->info) continue;

                auto& info = candidate->info;
                int canvas_w = (info->original_canvas_width > 0)
                                       ? static_cast<int>(std::lround(info->original_canvas_width * info->scale_factor))
                                       : curr_w;
                int canvas_h = (info->original_canvas_height > 0)
                                       ? static_cast<int>(std::lround(info->original_canvas_height * info->scale_factor))
                                       : curr_h;
                int orig_w = item.exact_origin_w > 0 ? item.exact_origin_w : canvas_w;
                int orig_h = item.exact_origin_h > 0 ? item.exact_origin_h : canvas_h;
                int dx = item.exact_offset.x;
                int dy = item.exact_offset.y;
                if (dx == 0 && dy == 0 && item.exact_point.x >= 0 && item.exact_point.y >= 0) {
                        double norm_x = (item.exact_point.x - 50.0) / 100.0;
                        double norm_y = (item.exact_point.y - 50.0) / 100.0;
                        dx = static_cast<int>(std::lround(norm_x * orig_w));
                        dy = static_cast<int>(std::lround(norm_y * orig_h));
                }
                double rx = orig_w != 0 ? static_cast<double>(curr_w) / orig_w : 1.0;
                double ry = orig_h != 0 ? static_cast<double>(curr_h) / orig_h : 1.0;
                SDL_Point center = ctx.get_area_center(*area);
                SDL_Point final_pos{
                        center.x + static_cast<int>(std::lround(dx * rx)),
                        center.y + static_cast<int>(std::lround(dy * ry))
                };
                MapGrid::Point* snapped = nullptr;
                if (auto* g = ctx.grid()) {
                        snapped = g->get_nearest_point(final_pos);
                        if (snapped) final_pos = snapped->pos;
                }
                if (ctx.checker().check(info, final_pos, ctx.exclusion_zones(), ctx.all_assets(),
                                        item.check_overlap, item.check_min_spacing, false, 5)) {
                        continue;
                }
                auto* result = ctx.spawnAsset(candidate->name, info, *area, final_pos, 0, nullptr, item.spawn_id, item.position);
                if (!result) continue;
                if (snapped && ctx.grid()) {
                        ctx.grid()->set_occupied(snapped, true);
                }
                ++spawned;
                ctx.logger().progress(info, spawned, target_attempts);
        }
        ctx.logger().output_and_log(item.name, target_attempts, spawned, attempts, attempts, "exact");
}
