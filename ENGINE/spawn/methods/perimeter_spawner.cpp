#include "perimeter_spawner.hpp"
#include <cmath>
#include <vector>
#include <algorithm>
#include <SDL.h>
#include "spawn_context.hpp"
#include "check.hpp"
#include "asset_spawn_planner.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include "spawn_logger.hpp"

void PerimeterSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
        if (!area || item.quantity <= 0 || !item.has_candidates()) return;
        using Point = SDL_Point;
        const int Y_SHIFT = 200;
        const auto& boundary = area->get_points();
        if (boundary.size() < 2) return;
        double cx = 0.0, cy = 0.0;
        for (const auto& pt : boundary) { cx += pt.x; cy += pt.y; }
	cx /= boundary.size();
	cy /= boundary.size();
	double shift_ratio = 1.0 - (item.border_shift / 100.0);
	std::vector<Point> contracted;
	contracted.reserve(boundary.size());
        for (const auto& pt : boundary) {
                double dx = pt.x - cx;
                double dy = pt.y - cy;
                int new_x = static_cast<int>(std::round(cx + dx * shift_ratio));
                int new_y = static_cast<int>(std::round(cy + dy * shift_ratio)) - Y_SHIFT;
                contracted.push_back(Point{new_x, new_y});
        }
	std::vector<double> segment_lengths;
	double total_length = 0.0;
	for (size_t i = 0; i < contracted.size(); ++i) {
                const Point& a = contracted[i];
                const Point& b = contracted[(i + 1) % contracted.size()];
                double len = std::hypot(b.x - a.x, b.y - a.y);
		segment_lengths.push_back(len);
		total_length += len;
	}
	if (total_length <= 0.0) return;
	double spacing = total_length / item.quantity;
	double dist_accum = 0.0;
	size_t seg_index = 0;
        int placed = 0, attempts = 0;
        for (int i = 0; i < item.quantity; ++i) {
                double target = i * spacing;
                while (seg_index < segment_lengths.size() &&
         dist_accum + segment_lengths[seg_index] < target) {
			dist_accum += segment_lengths[seg_index++];
		}
		if (seg_index >= segment_lengths.size()) break;
                const Point& p1 = contracted[seg_index];
                const Point& p2 = contracted[(seg_index + 1) % contracted.size()];
                double t = (target - dist_accum) / segment_lengths[seg_index];
                int x = static_cast<int>(std::round(p1.x + t * (p2.x - p1.x)));
                int y = static_cast<int>(std::round(p1.y + t * (p2.y - p1.y)));
                x += item.perimeter_offset.x;
                y += item.perimeter_offset.y;
                ++attempts;
                SDL_Point pos{x, y};
                const SpawnCandidate* candidate = item.select_candidate(ctx.rng());
                if (!candidate || candidate->is_null || !candidate->info) continue;
                auto& info = candidate->info;
                if (ctx.checker().check(info, pos, ctx.exclusion_zones(), ctx.all_assets(),
                                        item.check_overlap, false, false, 5)) continue;
                ctx.spawnAsset(candidate->name, info, *area, pos, 0, nullptr, item.spawn_id, item.position);
                ++placed;
                ctx.logger().progress(info, placed, item.quantity);
        }
        ctx.logger().output_and_log(item.name, item.quantity, placed, attempts, item.quantity, "perimeter");
}
