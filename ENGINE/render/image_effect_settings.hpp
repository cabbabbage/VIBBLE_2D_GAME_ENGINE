#pragma once

#include <cstdint>
#include <optional>

#include <nlohmann/json_fwd.hpp>

namespace camera_effects {

struct ImageEffectSettings {
    float rgb_boost   = 0.0f;
    float contrast    = 0.0f;
    float brightness  = 0.0f;
    float blur        = 0.0f;
    float saturation_red   = 0.0f;
    float saturation_green = 0.0f;
    float saturation_blue  = 0.0f;
    float hue         = 0.0f; // degrees
};

void        ClampImageEffectSettings(ImageEffectSettings& settings);
bool        ImageEffectSettingsEqual(const ImageEffectSettings& lhs, const ImageEffectSettings& rhs, float epsilon = 1e-4f);
bool        ImageEffectSettingsIsIdentity(const ImageEffectSettings& settings, float epsilon = 1e-3f);
std::uint64_t HashImageEffectSettings(const ImageEffectSettings& settings);
bool        read_effect_settings(const nlohmann::json& json, ImageEffectSettings& out);

namespace image_effects {

struct GlobalState {
    ImageEffectSettings foreground;
    ImageEffectSettings background;
};

void              set_global_state(const GlobalState& state);
GlobalState       current_state();
ImageEffectSettings current_foreground();
ImageEffectSettings current_background();
std::uint64_t     current_foreground_hash();
std::uint64_t     current_background_hash();

} // namespace image_effects

std::optional<image_effects::GlobalState> manifest_effect_defaults();

} // namespace camera_effects
