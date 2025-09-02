#include "perimeter_spawner.hpp"

#include <cmath>
#include <vector>

#include "utils/spawn_context.hpp"
#include "check.hpp"
#include "asset_spawn_planner.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include "spawn_logger.hpp"

void PerimeterSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
    if (!item.info || item.quantity <= 0 || !area) return;

    using Point = std::pair<int,int>;
    const int Y_SHIFT = 200;

    const auto& boundary = area->get_points();
    if (boundary.size() < 2) return;

    double cx = 0.0, cy = 0.0;
    for (const auto& pt : boundary) { cx += pt.first; cy += pt.second; }
    cx /= boundary.size();
    cy /= boundary.size();

    double shift_ratio = 1.0 - (item.border_shift / 100.0);
    std::vector<Point> contracted;
    contracted.reserve(boundary.size());
    for (const auto& pt : boundary) {
        double dx = pt.first - cx;
        double dy = pt.second - cy;
        int new_x = static_cast<int>(std::round(cx + dx * shift_ratio));
        int new_y = static_cast<int>(std::round(cy + dy * shift_ratio)) - Y_SHIFT;
        contracted.emplace_back(new_x, new_y);
    }

    std::vector<double> segment_lengths;
    double total_length = 0.0;
    for (size_t i = 0; i < contracted.size(); ++i) {
        const Point& a = contracted[i];
        const Point& b = contracted[(i + 1) % contracted.size()];
        double len = std::hypot(b.first - a.first, b.second - a.second);
        segment_lengths.push_back(len);
        total_length += len;
    }
    if (total_length <= 0.0) return;

    double spacing = total_length / item.quantity;
    double dist_accum = 0.0;
    size_t seg_index = 0;

    int angle_center = item.sector_center;
    int angle_range  = item.sector_range;

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
        int x = static_cast<int>(std::round(p1.first + t * (p2.first - p1.first)));
        int y = static_cast<int>(std::round(p1.second + t * (p2.second - p1.second)));

        double angle = std::atan2(y - (cy - Y_SHIFT), x - cx) * 180.0 / M_PI;
        if (angle < 0) angle += 360;

        int angle_start = angle_center - angle_range / 2;
        int angle_end   = angle_center + angle_range / 2;
        bool within_sector = false;
        if (angle_start < 0 || angle_end >= 360) {
            within_sector = (angle >= (angle_start + 360) % 360 || angle <= angle_end % 360);
        } else {
            within_sector = (angle >= angle_start && angle <= angle_end);
        }
        if (!within_sector) continue;

        x += item.perimeter_x_offset;
        y += item.perimeter_y_offset;

        ++attempts;
        if (ctx.checker().check(item.info, x, y, ctx.exclusion_zones(), ctx.all_assets(),
                                false, false, 5)) continue;

        ctx.spawnAsset(item.name, item.info, "perimeter", x, y, 0, nullptr, item.asset_id);
        ++placed;
        ctx.logger().progress(item.info, placed, item.quantity);
    }

    ctx.logger().output_and_log(item.name, item.quantity, placed, attempts, item.quantity, "perimeter");
}
