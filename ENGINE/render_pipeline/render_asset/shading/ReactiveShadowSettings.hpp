#pragma once

#include <algorithm>
#include <tuple>

namespace render_pipeline::shading {

struct ReactiveShadowSettings {
    struct VirtualLightMapSettings {
        float horizontal_falloff = 1.0f;
        float vertical_falloff   = 1.0f;
        float max_offset_x       = 0.0f;
        float max_offset_y       = 0.0f;
        float shadow_scale       = 1.0f;
        float size_scale_factor  = 1.0f;
        float map_light_factor   = 0.0f;

        bool operator==(const VirtualLightMapSettings& other) const {
            return horizontal_falloff == other.horizontal_falloff &&
                   vertical_falloff == other.vertical_falloff &&
                   max_offset_x == other.max_offset_x &&
                   max_offset_y == other.max_offset_y &&
                   shadow_scale == other.shadow_scale &&
                   size_scale_factor == other.size_scale_factor &&
                   map_light_factor == other.map_light_factor;
        }
        bool operator!=(const VirtualLightMapSettings& other) const { return !(*this == other); }
    } virtual_light_map;

    bool operator==(const ReactiveShadowSettings& other) const {
        return virtual_light_map == other.virtual_light_map;
    }
    bool operator!=(const ReactiveShadowSettings& other) const { return !(*this == other); }
};

inline float clampf(float value, float min_value, float max_value) {
    return std::max(min_value, std::min(value, max_value));
}

inline ReactiveShadowSettings sanitize_reactive_shadow_settings(const ReactiveShadowSettings& raw) {
    ReactiveShadowSettings out = raw;
    out.virtual_light_map.horizontal_falloff = clampf(out.virtual_light_map.horizontal_falloff, 0.0f, 10.0f);
    out.virtual_light_map.vertical_falloff   = clampf(out.virtual_light_map.vertical_falloff, 0.0f, 10.0f);
    out.virtual_light_map.max_offset_x       = clampf(out.virtual_light_map.max_offset_x, 0.0f, 500.0f);
    out.virtual_light_map.max_offset_y       = clampf(out.virtual_light_map.max_offset_y, 0.0f, 500.0f);
    out.virtual_light_map.shadow_scale       = clampf(out.virtual_light_map.shadow_scale, 0.0f, 10.0f);
    out.virtual_light_map.size_scale_factor  = clampf(out.virtual_light_map.size_scale_factor, 0.0f, 10.0f);
    out.virtual_light_map.map_light_factor   = clampf(out.virtual_light_map.map_light_factor, 0.0f, 1.0f);
    return out;
}

}  // namespace render_pipeline::shading

