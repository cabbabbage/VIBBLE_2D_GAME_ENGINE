#pragma once

#include <algorithm>
#include <tuple>
#include <vector>

namespace render_pipeline::shading {

struct ReactiveShadowSettings {
    struct VirtualLightMapSettings {
        float horizontal_falloff = 1.0f;
        float vertical_falloff   = 1.0f;
        float max_offset_x       = 0.0f;
        float max_offset_y       = 0.0f;
        float shadow_scale       = 1.0f;
        int   min_scale_percent  = 80;
        int   max_scale_percent  = 120;
        float map_light_dir_offset_strength = 0.5f;
        float parallax_percent              = 0.0f;
        int   search_radius      = 2;

        bool operator==(const VirtualLightMapSettings& other) const {
            return horizontal_falloff == other.horizontal_falloff &&
                   vertical_falloff == other.vertical_falloff &&
                   max_offset_x == other.max_offset_x &&
                   max_offset_y == other.max_offset_y &&
                   shadow_scale == other.shadow_scale &&
                   min_scale_percent == other.min_scale_percent &&
                   max_scale_percent == other.max_scale_percent &&
                   map_light_dir_offset_strength == other.map_light_dir_offset_strength &&
                   parallax_percent == other.parallax_percent &&
                   search_radius == other.search_radius;
        }
        bool operator!=(const VirtualLightMapSettings& other) const { return !(*this == other); }
    } virtual_light_map;

    float opacity_strength  = 1.0f;
    float opacity_sensitivity_percent = 50.0f;
    float parallax_strength = 1.0f;
    float scale_strength    = 1.0f;
    int   frame_blend_falloff_frames = 100;

    struct ShadowResponseLutEntry {
        float brightness = 0.0f;
        float opacity    = 1.0f;
        float offset     = 0.0f;
        float scale      = 1.0f;

        bool operator==(const ShadowResponseLutEntry& other) const {
            return brightness == other.brightness &&
                   opacity == other.opacity &&
                   offset == other.offset &&
                   scale == other.scale;
        }
        bool operator!=(const ShadowResponseLutEntry& other) const { return !(*this == other); }
    };

    struct ShadowResponseLut {
        std::vector<ShadowResponseLutEntry> entries{};

        bool operator==(const ShadowResponseLut& other) const { return entries == other.entries; }
        bool operator!=(const ShadowResponseLut& other) const { return !(*this == other); }
    } response_lut;

    struct SamplingWeights {
        float static_weight  = 0.0f;
        float dynamic_weight = 1.0f;

        bool operator==(const SamplingWeights& other) const {
            return static_weight == other.static_weight && dynamic_weight == other.dynamic_weight;
        }
        bool operator!=(const SamplingWeights& other) const { return !(*this == other); }
    } sampling_weights;

    bool operator==(const ReactiveShadowSettings& other) const {
        return virtual_light_map == other.virtual_light_map &&
               opacity_strength == other.opacity_strength &&
               opacity_sensitivity_percent == other.opacity_sensitivity_percent &&
               parallax_strength == other.parallax_strength &&
               scale_strength == other.scale_strength &&
               frame_blend_falloff_frames == other.frame_blend_falloff_frames &&
               response_lut == other.response_lut &&
               sampling_weights == other.sampling_weights;
    }
    bool operator!=(const ReactiveShadowSettings& other) const { return !(*this == other); }
};

inline float clampf(float value, float min_value, float max_value) {
    return std::max(min_value, std::min(value, max_value));
}

inline int clampi(int value, int min_value, int max_value) {
    return std::max(min_value, std::min(value, max_value));
}

inline ReactiveShadowSettings sanitize_reactive_shadow_settings(const ReactiveShadowSettings& raw) {
    ReactiveShadowSettings out = raw;
    out.virtual_light_map.horizontal_falloff = clampf(out.virtual_light_map.horizontal_falloff, 0.0f, 10.0f);
    out.virtual_light_map.vertical_falloff   = clampf(out.virtual_light_map.vertical_falloff, 0.0f, 10.0f);
    out.virtual_light_map.max_offset_x       = clampf(out.virtual_light_map.max_offset_x, 0.0f, 500.0f);
    out.virtual_light_map.max_offset_y       = clampf(out.virtual_light_map.max_offset_y, 0.0f, 500.0f);
    out.virtual_light_map.shadow_scale       = clampf(out.virtual_light_map.shadow_scale, 0.0f, 10.0f);
    out.virtual_light_map.min_scale_percent = clampi(out.virtual_light_map.min_scale_percent, 10, 500);
    out.virtual_light_map.max_scale_percent = clampi(out.virtual_light_map.max_scale_percent, 10, 500);
    if (out.virtual_light_map.min_scale_percent > out.virtual_light_map.max_scale_percent) {
        std::swap(out.virtual_light_map.min_scale_percent, out.virtual_light_map.max_scale_percent);
    }
    out.virtual_light_map.map_light_dir_offset_strength =
        clampf(out.virtual_light_map.map_light_dir_offset_strength, 0.0f, 1.0f);
    out.virtual_light_map.parallax_percent = clampf(out.virtual_light_map.parallax_percent, 0.0f, 100.0f);
    out.virtual_light_map.search_radius      = clampi(out.virtual_light_map.search_radius, 0, 64);
    out.opacity_strength                     = clampf(out.opacity_strength, 0.0f, 10.0f);
    out.opacity_sensitivity_percent          = clampf(out.opacity_sensitivity_percent, 0.0f, 100.0f);
    out.parallax_strength                    = clampf(out.parallax_strength, 0.0f, 10.0f);
    out.scale_strength                       = clampf(out.scale_strength, 0.0f, 10.0f);
    out.frame_blend_falloff_frames = clampi(out.frame_blend_falloff_frames, 0, 200);

    auto sanitize_entry = [](ReactiveShadowSettings::ShadowResponseLutEntry entry) {
        entry.brightness = clampf(entry.brightness, 0.0f, 1.0f);
        entry.opacity    = clampf(entry.opacity, 0.0f, 10.0f);
        entry.offset     = clampf(entry.offset, -1000.0f, 1000.0f);
        entry.scale      = clampf(entry.scale, 0.0f, 10.0f);
        return entry;
    };

    std::vector<ReactiveShadowSettings::ShadowResponseLutEntry> sanitized_entries;
    sanitized_entries.reserve(out.response_lut.entries.size());
    for (const auto& entry : out.response_lut.entries) {
        sanitized_entries.push_back(sanitize_entry(entry));
    }

    if (sanitized_entries.empty()) {
        sanitized_entries.push_back({0.0f, 1.0f, 0.0f, 1.0f});
        sanitized_entries.push_back({1.0f, 0.0f, 0.0f, 1.0f});
    }

    std::sort(sanitized_entries.begin(), sanitized_entries.end(), [](const auto& a, const auto& b) {
        if (a.brightness == b.brightness) {
            if (a.opacity == b.opacity) {
                if (a.offset == b.offset) {
                    return a.scale < b.scale;
                }
                return a.offset < b.offset;
            }
            return a.opacity < b.opacity;
        }
        return a.brightness < b.brightness;
    });

    sanitized_entries.erase(std::unique(sanitized_entries.begin(), sanitized_entries.end(),
                                        [](const auto& a, const auto& b) {
                                            return a.brightness == b.brightness &&
                                                   a.opacity == b.opacity &&
                                                   a.offset == b.offset &&
                                                   a.scale == b.scale;
                                        }),
                            sanitized_entries.end());

    if (sanitized_entries.front().brightness > 0.0f) {
        sanitized_entries.insert(sanitized_entries.begin(),
                                 sanitize_entry({0.0f,
                                                sanitized_entries.front().opacity,
                                                sanitized_entries.front().offset,
                                                sanitized_entries.front().scale}));
    }
    if (sanitized_entries.back().brightness < 1.0f) {
        sanitized_entries.push_back(sanitize_entry({1.0f,
                                                   sanitized_entries.back().opacity,
                                                   sanitized_entries.back().offset,
                                                   sanitized_entries.back().scale}));
    }

    out.response_lut.entries = std::move(sanitized_entries);

    // Sampling is now fully dynamic; force a zero static weight and unit dynamic weight so runtime
    // data always drives the lighting blend regardless of serialized values.
    out.sampling_weights.static_weight  = 0.0f;
    out.sampling_weights.dynamic_weight = 1.0f;
    return out;
}

}  // namespace render_pipeline::shading

