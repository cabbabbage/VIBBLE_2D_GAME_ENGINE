#include "path_sanitizer.hpp"

#include <algorithm>
#include <cmath>

#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "animation_update.hpp"
#include "core/asset_list.hpp"
#include "utils/area.hpp"

struct CollisionArea {
    const Asset* asset = nullptr;
    Area         area{ "impassable" };
};

static std::vector<CollisionArea> gather_collision_areas(const Asset& self) {
    std::vector<CollisionArea> result;
    const AssetList* list = self.get_impassable_naighbors();
    if (!list) {
        return result;
    }

    std::vector<Asset*> neighbors;
    list->full_list(neighbors);
    result.reserve(neighbors.size());

    for (Asset* neighbor : neighbors) {
        if (!neighbor || neighbor == &self || !neighbor->info) {
            continue;
        }
        Area collision = neighbor->get_area("impassable");
        if (collision.get_points().empty()) {
            collision = neighbor->get_area("collision_area");
        }
        if (collision.get_points().empty()) {
            continue;
        }
        result.push_back(CollisionArea{ neighbor, std::move(collision) });
    }
    return result;
}

static bool segment_hits_any(SDL_Point from, SDL_Point to, const std::vector<CollisionArea>& areas) {
    for (const auto& entry : areas) {
        if (animation_update::detail::segment_hits_area(from, to, entry.area)) {
            return true;
        }
    }
    return false;
}

static SDL_Point nudge_outside(SDL_Point pt, const Area& area) {
    SDL_Point center = area.get_center();
    int dx = pt.x - center.x;
    int dy = pt.y - center.y;
    if (dx == 0 && dy == 0) {
        dx = 1;
    }
    const double length = std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy);
    const double step_x = (length == 0.0) ? 1.0 : dx / length;
    const double step_y = (length == 0.0) ? 0.0 : dy / length;

    SDL_Point result = pt;
    int       guard  = 0;
    while (area.contains_point(result) && guard < 512) {
        result.x += static_cast<int>(std::round(step_x));
        result.y += static_cast<int>(std::round(step_y));
        ++guard;
    }
    return result;
}

static SDL_Point walk_back_to_perimeter(SDL_Point start,
                                 SDL_Point target,
                                 const std::vector<CollisionArea>& areas) {
    const int steps = std::max(std::abs(target.x - start.x), std::abs(target.y - start.y));
    if (steps == 0) {
        return target;
    }

    const double step_x = (target.x - start.x) / static_cast<double>(steps);
    const double step_y = (target.y - start.y) / static_cast<double>(steps);

    SDL_Point best = target;
    for (int i = steps; i >= 0; --i) {
        SDL_Point candidate{ static_cast<int>(std::round(start.x + step_x * i)),
                             static_cast<int>(std::round(start.y + step_y * i)) };

        bool inside = false;
        for (const auto& entry : areas) {
            if (entry.area.contains_point(candidate)) {
                inside = true;
                break;
            }
        }

        if (!inside) {
            best = candidate;
            break;
        }
    }

    return best;
}

std::vector<SDL_Point> PathSanitizer::sanitize(const Asset& self,
                                               const std::vector<SDL_Point>& absolute_checkpoints,
                                               int visited_thresh_px) const {
    std::vector<SDL_Point> sanitized;
    if (absolute_checkpoints.empty()) {
        return sanitized;
    }

    const auto collision_areas = gather_collision_areas(self);
    const SDL_Point origin     = self.pos;
    const int       thresh_sq  = visited_thresh_px * visited_thresh_px;
    const Assets*   assets     = self.get_assets();

    for (const SDL_Point& checkpoint : absolute_checkpoints) {
        SDL_Point anchor = sanitized.empty() ? origin : sanitized.back();
        if (thresh_sq > 0 && animation_update::detail::distance_sq(anchor, checkpoint) <= thresh_sq) {
            continue;
        }

        SDL_Point candidate = checkpoint;
        for (const auto& entry : collision_areas) {
            if (entry.area.contains_point(candidate)) {
                candidate = nudge_outside(candidate, entry.area);
            }
        }

        if (segment_hits_any(anchor, candidate, collision_areas)) {
            candidate = walk_back_to_perimeter(anchor, candidate, collision_areas);
        }

        const SDL_Point anchor_bottom    = animation_update::detail::bottom_middle_for(self, anchor);
        const SDL_Point candidate_bottom = animation_update::detail::bottom_middle_for(self, candidate);
        if (!animation_update::detail::bottom_point_inside_playable_area(assets, candidate_bottom)) {
            continue;
        }
        if (animation_update::detail::segment_leaves_playable_area(assets, anchor_bottom, candidate_bottom)) {
            continue;
        }

        if (thresh_sq > 0 && animation_update::detail::distance_sq(anchor, candidate) <= thresh_sq) {
            continue;
        }

        sanitized.push_back(candidate);
    }

    return sanitized;
}
