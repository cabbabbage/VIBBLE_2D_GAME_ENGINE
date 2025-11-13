#pragma once

#include <string_view>

#include "dev_mode/dev_ui_settings.hpp"

namespace devmode::camera_prefs {

inline constexpr std::string_view kDepthCueEnabledSettingKey = "dev_ui.camera.depthcue_enabled";
inline constexpr std::string_view kDepthCueBlurEnabledSettingKey = "dev_ui.camera.depthcue_blur_enabled";
inline constexpr std::string_view kDepthCueBrightnessEnabledSettingKey = "dev_ui.camera.depthcue_brightness_enabled";
inline constexpr std::string_view kDepthCueSaturationEnabledSettingKey = "dev_ui.camera.depthcue_saturation_enabled";
inline constexpr std::string_view kDepthCuePrimaryEnabledSettingKey = "dev_ui.camera.depthcue_primary_enabled";

inline bool load_depthcue_enabled() {
    return devmode::ui_settings::load_bool(kDepthCueEnabledSettingKey, false);
}

inline void save_depthcue_enabled(bool enabled) {
    devmode::ui_settings::save_bool(kDepthCueEnabledSettingKey, enabled);
}

inline bool load_depthcue_blur_enabled() {
    // Blur enabled by default
    return devmode::ui_settings::load_bool(kDepthCueBlurEnabledSettingKey, true);
}

inline void save_depthcue_blur_enabled(bool enabled) {
    devmode::ui_settings::save_bool(kDepthCueBlurEnabledSettingKey, enabled);
}

inline bool load_depthcue_brightness_enabled() {
    // Brightness adjustments enabled by default
    return devmode::ui_settings::load_bool(kDepthCueBrightnessEnabledSettingKey, true);
}

inline void save_depthcue_brightness_enabled(bool enabled) {
    devmode::ui_settings::save_bool(kDepthCueBrightnessEnabledSettingKey, enabled);
}

inline bool load_depthcue_saturation_enabled() {
    // Saturation adjustments enabled by default
    return devmode::ui_settings::load_bool(kDepthCueSaturationEnabledSettingKey, true);
}

inline void save_depthcue_saturation_enabled(bool enabled) {
    devmode::ui_settings::save_bool(kDepthCueSaturationEnabledSettingKey, enabled);
}

// Primary boost has been merged into the saturation effect.
// Alias the primary toggle to the saturation toggle for backwards compatibility.
inline bool load_depthcue_primary_enabled() {
    return load_depthcue_saturation_enabled();
}

inline void save_depthcue_primary_enabled(bool enabled) {
    save_depthcue_saturation_enabled(enabled);
}

}  // namespace devmode::camera_prefs
