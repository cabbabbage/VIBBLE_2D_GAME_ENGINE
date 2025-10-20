#include "asset_loader_internal.hpp"

#include <algorithm>
#include <limits>
#include <vector>

namespace asset_loader_internal {

namespace {

double distance_sq_to_aabb(const SDL_Point& point,
                           double min_x,
                           double min_y,
                           double max_x,
                           double max_y) {
    const double px = static_cast<double>(point.x);
    const double py = static_cast<double>(point.y);
    double dx = 0.0;
    if (px < min_x) dx = min_x - px;
    else if (px > max_x) dx = px - max_x;
    double dy = 0.0;
    if (py < min_y) dy = min_y - py;
    else if (py > max_y) dy = py - max_y;
    return dx * dx + dy * dy;
}

} // namespace

std::vector<ZoneCacheEntry> build_zone_cache(const std::vector<const Area*>& zones) {
    std::vector<ZoneCacheEntry> cache;
    cache.reserve(zones.size());
    for (const Area* zone : zones) {
        if (!zone) continue;
        auto [min_x, min_y, max_x, max_y] = zone->get_bounds();
        const auto& pts = zone->get_points();
        cache.push_back(ZoneCacheEntry{zone, min_x, min_y, max_x, max_y, &pts});
    }
    return cache;
}

bool point_inside_any_zone(const SDL_Point& point, const std::vector<ZoneCacheEntry>& cache) {
    for (const auto& entry : cache) {
        if (point.x < entry.min_x || point.x > entry.max_x ||
            point.y < entry.min_y || point.y > entry.max_y) {
            continue;
        }
        if (entry.area && entry.area->contains_point(point)) {
            return true;
        }
    }
    return false;
}

double min_distance_sq_to_zones(const SDL_Point& point,
                                const std::vector<ZoneCacheEntry>& cache,
                                int remove_threshold) {
    double minDistSq = std::numeric_limits<double>::infinity();
    const double pad = static_cast<double>(remove_threshold);
    for (const auto& entry : cache) {
        const double paddedDistSq = distance_sq_to_aabb(point, static_cast<double>(entry.min_x) - pad, static_cast<double>(entry.min_y) - pad, static_cast<double>(entry.max_x) + pad, static_cast<double>(entry.max_y) + pad);
        if (paddedDistSq >= minDistSq) {
            continue;
        }
        const auto* pts = entry.points;
        if (!pts || pts->size() < 2) {
            const double bboxDistSq = distance_sq_to_aabb(point, static_cast<double>(entry.min_x), static_cast<double>(entry.min_y), static_cast<double>(entry.max_x), static_cast<double>(entry.max_y));
            if (bboxDistSq < minDistSq) {
                minDistSq = bboxDistSq;
            }
            continue;
        }
        const size_t count = pts->size();
        for (size_t i = 0; i + 1 < count; ++i) {
            const SDL_Point& p1 = (*pts)[i];
            const SDL_Point& p2 = (*pts)[(i + 1) % count];
            const double x1 = static_cast<double>(p1.x);
            const double y1 = static_cast<double>(p1.y);
            const double vx = static_cast<double>(p2.x - p1.x);
            const double vy = static_cast<double>(p2.y - p1.y);
            const double wx = static_cast<double>(point.x) - x1;
            const double wy = static_cast<double>(point.y) - y1;
            const double len2 = vx * vx + vy * vy;
            double t = len2 > 0.0 ? (vx * wx + vy * wy) / len2 : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            const double projx = x1 + t * vx;
            const double projy = y1 + t * vy;
            const double dx = projx - static_cast<double>(point.x);
            const double dy = projy - static_cast<double>(point.y);
            const double distSq = dx * dx + dy * dy;
            if (distSq < minDistSq) {
                minDistSq = distSq;
                if (minDistSq <= 0.0) {
                    return 0.0;
                }
            }
        }
    }
    return minDistSq;
}

} // namespace asset_loader_internal

