// light_utils.hpp
#pragma once
#include <algorithm>
#include "Asset.hpp" // or forward declare if possible

namespace LightUtils {

inline double calculate_static_alpha_percentage(const Asset* assetA, const Asset* assetB) {
    const int asset_y       = assetA ? assetA->z_index : 0;
    const int light_world_y = assetB ? assetB->z_index : 0;

    constexpr int FADE_ABOVE = 180;
    constexpr int FADE_BELOW = -30;
    constexpr double MIN_OPACITY = 0.15;
    constexpr double MAX_OPACITY = 0.7;

    int delta_y = light_world_y - asset_y;
    double factor;

    if (delta_y <= -FADE_ABOVE) {
        factor = MIN_OPACITY;
    } else if (delta_y >= FADE_BELOW) {
        factor = MAX_OPACITY;
    } else {
        factor = double(delta_y + FADE_ABOVE) / double(FADE_ABOVE + FADE_BELOW);
        factor = MIN_OPACITY + (MAX_OPACITY - MIN_OPACITY) * factor;
    }

    return std::clamp(factor, MIN_OPACITY, MAX_OPACITY);
}

} // namespace LightUtils
