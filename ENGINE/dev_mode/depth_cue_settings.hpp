#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>

#include "dev_mode/dev_ui_settings.hpp"

namespace devmode::camera_prefs {

inline constexpr std::string_view kDepthCueEnabledSettingKey = "dev_ui.camera.depthcue_enabled";
inline constexpr std::string_view kForegroundTextureOpacitySettingKey = "dev_ui.camera.foreground_texture_max_opacity";
inline constexpr std::string_view kBackgroundTextureOpacitySettingKey = "dev_ui.camera.background_texture_max_opacity";

inline bool load_depthcue_enabled() {
    return devmode::ui_settings::load_bool(kDepthCueEnabledSettingKey, false);
}

inline void save_depthcue_enabled(bool enabled) {
    devmode::ui_settings::save_bool(kDepthCueEnabledSettingKey, enabled);
}

inline int load_foreground_texture_max_opacity() {
    const double stored = devmode::ui_settings::load_number(kForegroundTextureOpacitySettingKey, 0.0);
    const double clamped = std::clamp(stored, 0.0, 255.0);
    return static_cast<int>(std::round(clamped));
}

inline void save_foreground_texture_max_opacity(int value) {
    const double clamped = std::clamp(static_cast<double>(value), 0.0, 255.0);
    devmode::ui_settings::save_number(kForegroundTextureOpacitySettingKey, clamped);
}

inline int load_background_texture_max_opacity() {
    const double stored = devmode::ui_settings::load_number(kBackgroundTextureOpacitySettingKey, 0.0);
    const double clamped = std::clamp(stored, 0.0, 255.0);
    return static_cast<int>(std::round(clamped));
}

inline void save_background_texture_max_opacity(int value) {
    const double clamped = std::clamp(static_cast<double>(value), 0.0, 255.0);
    devmode::ui_settings::save_number(kBackgroundTextureOpacitySettingKey, clamped);
}

}
