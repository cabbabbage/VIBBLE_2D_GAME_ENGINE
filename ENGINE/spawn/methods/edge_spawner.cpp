#include "edge_spawner.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include <SDL.h>

#include "spawn_context.hpp"
#include "spawn_info.hpp"
#include "check.hpp"
#include "asset_spawn_planner.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include "utils/grid.hpp"

namespace {

struct Edge {
    SDL_FPoint start{0.0f, 0.0f};
    SDL_FPoint delta{0.0f, 0.0f};
    double length = 0.0;
};

std::vector<Edge> build_edges(const Area& area, double& total_length) {
    std::vector<Edge> edges;
    total_length = 0.0;
    const auto& pts = area.get_points();
    const size_t n = pts.size();
    if (n < 2) {
        return edges;
    }
    edges.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const SDL_Point& a = pts[i];
        const SDL_Point& b = pts[(i + 1) % n];
        const double dx = static_cast<double>(b.x - a.x);
        const double dy = static_cast<double>(b.y - a.y);
        const double len = std::hypot(dx, dy);
        if (len <= 1e-6) {
            continue;
        }
        Edge edge;
        edge.start = SDL_FPoint{static_cast<float>(a.x), static_cast<float>(a.y)};
        edge.delta = SDL_FPoint{static_cast<float>(dx), static_cast<float>(dy)};
        edge.length = len;
        total_length += len;
        edges.push_back(edge);
    }
    return edges;
}

bool point_along_edges(const std::vector<Edge>& edges,
                       double perimeter,
                       double distance,
                       SDL_FPoint& out_point) {
    if (edges.empty() || perimeter <= 0.0) {
        return false;
    }
    double wrapped = std::fmod(distance, perimeter);
    if (wrapped < 0.0) {
        wrapped += perimeter;
    }
    for (const auto& edge : edges) {
        if (edge.length <= 0.0) {
            continue;
        }
        if (wrapped <= edge.length || std::fabs(wrapped - edge.length) < 1e-6) {
            const double t = std::clamp(wrapped / edge.length, 0.0, 1.0);
            out_point.x = edge.start.x + static_cast<float>(edge.delta.x * t);
            out_point.y = edge.start.y + static_cast<float>(edge.delta.y * t);
            return true;
        }
        wrapped -= edge.length;
    }
    const Edge& last = edges.back();
    out_point.x = last.start.x + last.delta.x;
    out_point.y = last.start.y + last.delta.y;
    return true;
}

SDL_Point apply_inset(SDL_Point center, const SDL_FPoint& edge_point, int inset_percent) {
    const double scale = std::clamp(static_cast<double>(inset_percent) / 100.0, 0.0, 2.0);
    const double vx = static_cast<double>(edge_point.x) - static_cast<double>(center.x);
    const double vy = static_cast<double>(edge_point.y) - static_cast<double>(center.y);
    const double target_x = static_cast<double>(center.x) + vx * scale;
    const double target_y = static_cast<double>(center.y) + vy * scale;
    return SDL_Point{static_cast<int>(std::lround(target_x)), static_cast<int>(std::lround(target_y))};
}

}

std::vector<SDL_Point> EdgeSpawner::plan_positions(const SpawnInfo& item,
                                                   const Area& area,
                                                   PlacementContext& placement) const {
    std::vector<SDL_Point> results;
    if (item.quantity <= 0) {
        return results;
    }

    double perimeter = 0.0;
    std::vector<Edge> edges = build_edges(area, perimeter);
    if (edges.empty() || perimeter <= 0.0) {
        return results;
    }

    const double step = perimeter / static_cast<double>(item.quantity);
    double start_offset = 0.0;
    if (step > 0.0) {
        std::uniform_real_distribution<double> dist(0.0, step);
        start_offset = dist(placement.rng);
    }

    const int resolution = placement.resolution;

    for (int i = 0; i < item.quantity; ++i) {
        const double distance = start_offset + step * static_cast<double>(i);
        SDL_FPoint edge_point{0.0f, 0.0f};
        if (!point_along_edges(edges, perimeter, distance, edge_point)) {
            continue;
        }

        SDL_Point spawn_point = apply_inset(placement.center, edge_point, item.edge_inset_percent);
        if (resolution > 0) {
            spawn_point = placement.grid.snap_to_vertex(spawn_point, resolution);
        }

        if (placement.overlaps_trail && placement.overlaps_trail(spawn_point)) {
            continue;
        }

        results.push_back(spawn_point);
    }

    return results;
}

#ifndef EDGE_SPAWNER_DISABLE_RUNTIME
void EdgeSpawner::spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx) {
    const Area* target_area = ctx.clip_area() ? ctx.clip_area() : area;
    if (!target_area || !item.has_candidates() || item.quantity <= 0) {
        return;
    }

    PlacementContext placement{
        ctx.rng(),
        ctx.grid(),
        ctx.spawn_resolution(),
        ctx.get_area_center(*target_area),
        [&](SDL_Point pt) { return ctx.point_overlaps_trail(pt, target_area); }
};

    std::vector<SDL_Point> positions = plan_positions(item, *target_area, placement);

    for (SDL_Point spawn_point : positions) {
        const SpawnCandidate* candidate = item.select_candidate(ctx.rng());
        if (!candidate || candidate->is_null || !candidate->info) {
            continue;
        }

        if (!ctx.position_allowed(*target_area, spawn_point)) {
            continue;
        }

        const bool enforce_spacing = item.check_min_spacing;
        if (ctx.checks_enabled() &&
            ctx.checker().check(candidate->info,
                                spawn_point,
                                ctx.exclusion_zones(),
                                ctx.all_assets(),
                                false,
                                enforce_spacing,
                                true,
                                false,
                                5)) {
            continue;
        }

        if (auto* spawned = ctx.spawnAsset(candidate->name,
                                           candidate->info,
                                           *target_area,
                                           spawn_point,
                                           0,
                                           nullptr,
                                           item.spawn_id,
                                           item.position)) {
            if (ctx.checks_enabled()) {
                ctx.checker().register_asset(spawned, enforce_spacing, false);
            }
        }
    }
}
#endif

