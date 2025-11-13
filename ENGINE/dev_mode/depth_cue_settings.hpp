#pragma once

#include <string_view>

#include "dev_mode/dev_ui_settings.hpp"

namespace devmode::camera_prefs {

inline constexpr std::string_view kDepthCueEnabledSettingKey = "dev_ui.camera.depthcue_enabled";

inline bool load_depthcue_enabled() {
    return devmode::ui_settings::load_bool(kDepthCueEnabledSettingKey, false);
}

inline void save_depthcue_enabled(bool enabled) {
    devmode::ui_settings::save_bool(kDepthCueEnabledSettingKey, enabled);
}

}  // namespace devmode::camera_prefs
