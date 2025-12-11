#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "animation_update/custom_controllers/controller_path_utils.hpp"

class Asset;

namespace controller_utils {

inline int controller_visit_threshold(const Asset* asset, const std::vector<SDL_Point>& planned_path) {
    if (!asset) {
        return 0;
    }

    if (planned_path.empty()) {
        return 0;
    }

    const int base_threshold = std::max(0, controller_paths::default_visit_threshold(asset));

    long long max_step_sq = 0;
    for (const SDL_Point& step : planned_path) {
        const long long dx = static_cast<long long>(step.x);
        const long long dy = static_cast<long long>(step.y);
        const long long step_sq = dx * dx + dy * dy;
        if (step_sq > max_step_sq) {
            max_step_sq = step_sq;
        }
    }

    if (max_step_sq <= 1) {
        return 0;
    }

    const double step_length = std::sqrt(static_cast<double>(max_step_sq));
    const int    step_limit  = static_cast<int>(std::ceil(step_length));
    const int    desired     = std::max(0, step_limit - 1);

    return std::max(0, std::min(base_threshold, desired));
}

inline int controller_visit_threshold(const Asset* asset) {
    return controller_visit_threshold(asset, {});
}

}

