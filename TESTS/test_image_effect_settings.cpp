#include "doctest/doctest.h"

#include <nlohmann/json.hpp>

#include "render/image_effect_settings.hpp"

TEST_CASE("image effect parser honors per-channel saturation keys") {
    nlohmann::json data{
        {"rgb_boost", 0.25},
        {"contrast", -2.0},
        {"brightness", 1.5},
        {"blur", 0.5},
        {"saturation_red", 1.5},
        {"saturation_green", -1.5},
        {"saturation_blue", 0.2},
        {"hue", 270.0},
    };

    camera_effects::ImageEffectSettings settings{};
    REQUIRE(camera_effects::read_effect_settings(data, settings));
    CHECK(settings.rgb_boost == doctest::Approx(0.25f));
    CHECK(settings.contrast == doctest::Approx(-1.0f)); // clamped
    CHECK(settings.brightness == doctest::Approx(1.0f)); // clamped
    CHECK(settings.blur == doctest::Approx(0.5f));
    CHECK(settings.saturation_red == doctest::Approx(1.0f));   // clamped
    CHECK(settings.saturation_green == doctest::Approx(-1.0f)); // clamped
    CHECK(settings.saturation_blue == doctest::Approx(0.2f));
    CHECK(settings.hue == doctest::Approx(180.0f)); // clamped
}

TEST_CASE("image effect parser supports legacy saturation key") {
    nlohmann::json data{
        {"saturation", -0.3},
    };

    camera_effects::ImageEffectSettings settings{};
    REQUIRE(camera_effects::read_effect_settings(data, settings));
    CHECK(settings.saturation_red == doctest::Approx(-0.3f));
    CHECK(settings.saturation_green == doctest::Approx(-0.3f));
    CHECK(settings.saturation_blue == doctest::Approx(-0.3f));
    CHECK(camera_effects::ImageEffectSettingsIsIdentity(settings, 1e-5f) == false);
    CHECK(settings.rgb_boost == doctest::Approx(0.0f));
}
