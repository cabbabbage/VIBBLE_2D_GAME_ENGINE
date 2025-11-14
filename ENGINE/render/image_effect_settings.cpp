#include "render/image_effect_settings.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace image_effect_settings_detail {

constexpr float kMaxRgbBoost        = 1.0f;
constexpr float kMaxContrast        = 1.0f;
constexpr float kMaxBrightness      = 1.0f;
constexpr float kMaxBlur            = 1.0f;
constexpr float kMaxSaturationChannel = 1.0f;
constexpr float kMaxHue             = 180.0f;

inline bool nearly_equal(float a, float b, float eps) {
    return std::fabs(a - b) <= eps;
}

struct GlobalEffectState {
    camera_effects::image_effects::GlobalState state{};
    std::uint64_t foreground_hash = 0;
    std::uint64_t background_hash = 0;
};

GlobalEffectState g_effect_state{};
std::mutex        g_effect_mutex;

} // namespace image_effect_settings_detail

namespace camera_effects {
using namespace image_effect_settings_detail;

void ClampImageEffectSettings(ImageEffectSettings& settings) {
    auto sanitize = [](float value, float max_abs) -> float {
        if (!std::isfinite(value)) {
            return 0.0f;
        }
        return std::clamp(value, -max_abs, max_abs);
    };
    settings.rgb_boost       = sanitize(settings.rgb_boost, kMaxRgbBoost);
    settings.contrast        = sanitize(settings.contrast, kMaxContrast);
    settings.brightness      = sanitize(settings.brightness, kMaxBrightness);
    settings.blur            = sanitize(settings.blur, kMaxBlur);
    settings.saturation_red   = sanitize(settings.saturation_red, kMaxSaturationChannel);
    settings.saturation_green = sanitize(settings.saturation_green, kMaxSaturationChannel);
    settings.saturation_blue  = sanitize(settings.saturation_blue, kMaxSaturationChannel);
    settings.hue             = sanitize(settings.hue, kMaxHue);
}

bool ImageEffectSettingsEqual(const ImageEffectSettings& lhs,
                              const ImageEffectSettings& rhs,
                              float epsilon) {
    return nearly_equal(lhs.rgb_boost, rhs.rgb_boost, epsilon) &&
           nearly_equal(lhs.contrast, rhs.contrast, epsilon) &&
           nearly_equal(lhs.brightness, rhs.brightness, epsilon) &&
           nearly_equal(lhs.blur, rhs.blur, epsilon) &&
           nearly_equal(lhs.saturation_red, rhs.saturation_red, epsilon) &&
           nearly_equal(lhs.saturation_green, rhs.saturation_green, epsilon) &&
           nearly_equal(lhs.saturation_blue, rhs.saturation_blue, epsilon) &&
           nearly_equal(lhs.hue, rhs.hue, epsilon * 30.0f);
}

bool ImageEffectSettingsIsIdentity(const ImageEffectSettings& settings, float epsilon) {
    ImageEffectSettings identity{};
    return ImageEffectSettingsEqual(settings, identity, epsilon);
}

std::uint64_t HashImageEffectSettings(const ImageEffectSettings& settings) {
    auto quantize = [](float value, float scale) -> std::uint64_t {
        return static_cast<std::uint64_t>(std::llround(value * scale));
    };
    std::uint64_t hash = 1469598103934665603ull;
    const auto mix = [&](std::uint64_t v) {
        hash ^= v;
        hash *= 1099511628211ull;
    };
    mix(quantize(settings.rgb_boost, 1000.0f));
    mix(quantize(settings.contrast, 1000.0f));
    mix(quantize(settings.brightness, 1000.0f));
    mix(quantize(settings.blur, 1000.0f));
    mix(quantize(settings.saturation_red, 1000.0f));
    mix(quantize(settings.saturation_green, 1000.0f));
    mix(quantize(settings.saturation_blue, 1000.0f));
    mix(quantize(settings.hue, 10.0f));
    return hash;
}

namespace image_effects {

void set_global_state(const GlobalState& state) {
    GlobalState clamped = state;
    ClampImageEffectSettings(clamped.foreground);
    ClampImageEffectSettings(clamped.background);
    std::lock_guard<std::mutex> lock(g_effect_mutex);
    g_effect_state.state = clamped;
    g_effect_state.foreground_hash = HashImageEffectSettings(clamped.foreground);
    g_effect_state.background_hash = HashImageEffectSettings(clamped.background);
}

GlobalState current_state() {
    std::lock_guard<std::mutex> lock(g_effect_mutex);
    return g_effect_state.state;
}

ImageEffectSettings current_foreground() {
    std::lock_guard<std::mutex> lock(g_effect_mutex);
    return g_effect_state.state.foreground;
}

ImageEffectSettings current_background() {
    std::lock_guard<std::mutex> lock(g_effect_mutex);
    return g_effect_state.state.background;
}

std::uint64_t current_foreground_hash() {
    std::lock_guard<std::mutex> lock(g_effect_mutex);
    return g_effect_state.foreground_hash;
}

std::uint64_t current_background_hash() {
    std::lock_guard<std::mutex> lock(g_effect_mutex);
    return g_effect_state.background_hash;
}

} // namespace image_effects

} // namespace camera_effects
