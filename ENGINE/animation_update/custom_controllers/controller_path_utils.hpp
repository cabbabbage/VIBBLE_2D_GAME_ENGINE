#pragma once

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "asset/Asset.hpp"

namespace controller_paths {

inline int neighbor_radius(const Asset* asset) {
    if (asset && asset->info) {
        return std::max(0, asset->info->NeighborSearchRadius);
    }
    return 0;
}

inline SDL_Point clamp_to_radius(const SDL_Point& origin, SDL_Point desired, int radius) {
    if (radius <= 0) {
        return origin;
    }

    const int dx = desired.x - origin.x;
    const int dy = desired.y - origin.y;
    const double dist = std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy);
    if (dist <= static_cast<double>(radius)) {
        return desired;
    }
    if (dist <= 1e-6) {
        return origin;
    }

    const double scale = static_cast<double>(radius) / dist;
    SDL_Point result{ origin.x + static_cast<int>(std::round(dx * scale)),
                      origin.y + static_cast<int>(std::round(dy * scale)) };

    const double new_dx   = static_cast<double>(result.x - origin.x);
    const double new_dy   = static_cast<double>(result.y - origin.y);
    const double new_dist = std::sqrt(new_dx * new_dx + new_dy * new_dy);
    if (new_dist > static_cast<double>(radius) && new_dist > 0.0) {
        const double adjust = static_cast<double>(radius) / new_dist;
        result.x = origin.x + static_cast<int>(std::round(new_dx * adjust));
        result.y = origin.y + static_cast<int>(std::round(new_dy * adjust));
    }

    return result;
}

inline SDL_Point clamp_delta(SDL_Point delta, int radius) {
    SDL_Point origin{ 0, 0 };
    SDL_Point desired{ delta.x, delta.y };
    SDL_Point clamped = clamp_to_radius(origin, desired, radius);
    return SDL_Point{ clamped.x, clamped.y };
}

inline std::vector<SDL_Point> to_relative(const SDL_Point& origin, const std::vector<SDL_Point>& absolute_points) {
    std::vector<SDL_Point> result;
    result.reserve(absolute_points.size());
    SDL_Point cursor = origin;
    for (const SDL_Point& pt : absolute_points) {
        SDL_Point delta{ pt.x - cursor.x, pt.y - cursor.y };
        result.push_back(delta);
        cursor = pt;
    }
    return result;
}

inline std::vector<SDL_Point> idle_path(const Asset* asset, int rest_ratio) {
    std::vector<SDL_Point> relative;
    if (!asset) {
        return relative;
    }

    const SDL_Point origin = asset->pos;
    const int       radius = neighbor_radius(asset);
    if (radius <= 0) {
        relative.push_back(SDL_Point{ 0, 0 });
        return relative;
    }

    const int amplitude = std::clamp(rest_ratio / 3, 1, std::max(1, radius / 4));

    std::vector<SDL_Point> absolute;
    absolute.reserve(5);
    absolute.push_back(clamp_to_radius(origin, SDL_Point{ origin.x + amplitude, origin.y }, radius));
    absolute.push_back(clamp_to_radius(origin, SDL_Point{ origin.x, origin.y + amplitude }, radius));
    absolute.push_back(clamp_to_radius(origin, SDL_Point{ origin.x - amplitude, origin.y }, radius));
    absolute.push_back(clamp_to_radius(origin, SDL_Point{ origin.x, origin.y - amplitude }, radius));
    absolute.push_back(origin);

    return to_relative(origin, absolute);
}

inline std::vector<SDL_Point> pursue_path(const Asset* asset, const Asset* target) {
    std::vector<SDL_Point> relative;
    if (!asset || !target) {
        return relative;
    }

    const SDL_Point origin = asset->pos;
    const int       radius = neighbor_radius(asset);

    SDL_Point desired = clamp_to_radius(origin, target->pos, radius);
    relative.push_back(SDL_Point{ desired.x - origin.x, desired.y - origin.y });

    return relative;
}

inline std::vector<SDL_Point> flee_path(const Asset* asset, const Asset* threat) {
    std::vector<SDL_Point> relative;
    if (!asset) {
        return relative;
    }

    const SDL_Point origin = asset->pos;
    const int       radius = neighbor_radius(asset);
    if (radius <= 0) {
        relative.push_back(SDL_Point{ 0, 0 });
        return relative;
    }

    SDL_Point direction{ origin.x, origin.y };
    if (threat) {
        direction.x -= threat->pos.x;
        direction.y -= threat->pos.y;
    }
    if (direction.x == 0 && direction.y == 0) {
        direction.x = 1;
    }

    const double length = std::sqrt(static_cast<double>(direction.x) * direction.x + static_cast<double>(direction.y) * direction.y);
    const double scale = (length <= 1e-6) ? 0.0 : static_cast<double>(radius) / length;
    SDL_Point desired{ origin.x + static_cast<int>(std::round(direction.x * scale)),
                       origin.y + static_cast<int>(std::round(direction.y * scale)) };

    desired = clamp_to_radius(origin, desired, radius);
    relative.push_back(SDL_Point{ desired.x - origin.x, desired.y - origin.y });

    return relative;
}

inline std::vector<SDL_Point> orbit_path(const Asset* asset, const Asset* center, int radius, int steps = 8) {
    if (!asset || !center) {
        return {};
    }

    const int limit = std::clamp(radius, 0, neighbor_radius(asset));
    if (limit <= 0) {
        return pursue_path(asset, center);
    }

    const SDL_Point origin     = asset->pos;
    const SDL_Point center_pos = center->pos;

    const double base_dx = static_cast<double>(origin.x - center_pos.x);
    const double base_dy = static_cast<double>(origin.y - center_pos.y);
    double       angle   = std::atan2(base_dy, base_dx);
    if (std::isnan(angle)) {
        angle = 0.0;
    }

    const int clamped_steps = std::max(steps, 1);
    constexpr double kTau = 6.28318530717958647692;
    const double     step_angle = kTau / static_cast<double>(clamped_steps);

    std::vector<SDL_Point> absolute;
    absolute.reserve(clamped_steps);

    double current_angle = angle;
    for (int i = 0; i < clamped_steps; ++i) {
        current_angle += step_angle;
        SDL_Point desired{ center_pos.x + static_cast<int>(std::round(limit * std::cos(current_angle))),
                           center_pos.y + static_cast<int>(std::round(limit * std::sin(current_angle))) };
        SDL_Point clamped = clamp_to_radius(origin, desired, limit);
        if (absolute.empty() || absolute.back().x != clamped.x || absolute.back().y != clamped.y) {
            absolute.push_back(clamped);
        }
    }

    if (absolute.empty()) {
        return pursue_path(asset, center);
    }

    return to_relative(origin, absolute);
}

inline int default_visit_threshold(const Asset* asset) {
    const int radius = neighbor_radius(asset);
    if (radius <= 0) {
        return 1;
    }
    return std::max(1, radius / 8);
}

}

