#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>

namespace camera_effects {

struct ImageEffectSettings {

    float contrast = 0.0f;
    float brightness = 0.0f;
    float blur = 0.0f;
    float saturation_red = 0.0f;
    float saturation_green = 0.0f;
    float saturation_blue = 0.0f;
    float hue = 0.0f;
};

bool ImageEffectSettingsIsIdentity(const ImageEffectSettings& s, float epsilon);
bool ImageEffectSettingsIsIdentity(const ImageEffectSettings& s);
void ClampImageEffectSettings(ImageEffectSettings& s);
std::uint64_t HashImageEffectSettings(const ImageEffectSettings& s);

namespace image_effects {

struct GlobalState {
    ImageEffectSettings foreground;
    ImageEffectSettings background;
};

void set_global_state(const GlobalState& state);

}

}
