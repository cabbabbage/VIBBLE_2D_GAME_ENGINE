#include "image_effect_settings.hpp"

namespace camera_effects {

bool ImageEffectSettingsIsIdentity(const ImageEffectSettings& s, float epsilon) {
    return std::abs(s.rgb_boost - 0.0f) < epsilon &&
           std::abs(s.contrast - 1.0f) < epsilon &&
           std::abs(s.brightness - 0.0f) < epsilon &&
           std::abs(s.blur - 0.0f) < epsilon &&
           std::abs(s.saturation_red - 0.0f) < epsilon &&
           std::abs(s.saturation_green - 0.0f) < epsilon &&
           std::abs(s.saturation_blue - 0.0f) < epsilon &&
           std::abs(s.hue - 0.0f) < epsilon;
}

bool ImageEffectSettingsIsIdentity(const ImageEffectSettings& s) {
    return ImageEffectSettingsIsIdentity(s, 0.0f);
}

void ClampImageEffectSettings(ImageEffectSettings& s) {
    s.rgb_boost = std::clamp(s.rgb_boost, -5.0f, 5.0f);
    s.contrast = std::clamp(s.contrast, 0.1f, 5.0f);
    s.brightness = std::clamp(s.brightness, -1.0f, 1.0f);
    s.blur = std::clamp(s.blur, -10.0f, 10.0f);
    s.saturation_red = std::clamp(s.saturation_red, -1.0f, 1.0f);
    s.saturation_green = std::clamp(s.saturation_green, -1.0f, 1.0f);
    s.saturation_blue = std::clamp(s.saturation_blue, -1.0f, 1.0f);
    s.hue = std::clamp(s.hue, -180.0f, 180.0f);
}

std::uint64_t HashImageEffectSettings(const ImageEffectSettings& s) {
    std::uint64_t hash = 0;
    hash = hash * 31 + std::hash<float>{}(s.rgb_boost);
    hash = hash * 31 + std::hash<float>{}(s.contrast);
    hash = hash * 31 + std::hash<float>{}(s.brightness);
    hash = hash * 31 + std::hash<float>{}(s.blur);
    hash = hash * 31 + std::hash<float>{}(s.saturation_red);
    hash = hash * 31 + std::hash<float>{}(s.saturation_green);
    hash = hash * 31 + std::hash<float>{}(s.saturation_blue);
    hash = hash * 31 + std::hash<float>{}(s.hue);
    return hash;
}

namespace image_effects {

void set_global_state(const GlobalState& state) {
    // TODO: apply to global image processor
    (void)state;
}

}  // namespace image_effects

}  // namespace camera_effects
