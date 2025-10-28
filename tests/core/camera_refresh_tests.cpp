#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "render/camera.hpp"
#include "asset/Asset.hpp"

namespace {
constexpr float kFrameDt = 1.0f / 60.0f;

Area make_base_area(const std::string& name, int width, int height) {
    std::vector<SDL_Point> corners{
        SDL_Point{0, 0},
        SDL_Point{width, 0},
        SDL_Point{width, height},
        SDL_Point{0, height}
    };
    return Area(name, corners);
}
}

TEST_CASE("Camera refresh tracks player movement when requested") {
    Area base_area = make_base_area("base", 640, 480);
    camera cam(640, 480, base_area);

    Asset player;

    player.pos = SDL_Point{12, 34};
    cam.update_zoom(nullptr, nullptr, &player, true, kFrameDt);
    CHECK_EQ(cam.get_screen_center().x, player.pos.x);
    CHECK_EQ(cam.get_screen_center().y, player.pos.y);

    SDL_Point locked_center = cam.get_screen_center();

    player.pos = SDL_Point{120, 220};
    cam.update_zoom(nullptr, nullptr, &player, false, kFrameDt);
    CHECK_EQ(cam.get_screen_center().x, locked_center.x);
    CHECK_EQ(cam.get_screen_center().y, locked_center.y);

    cam.update_zoom(nullptr, nullptr, &player, true, kFrameDt);
    CHECK_EQ(cam.get_screen_center().x, player.pos.x);
    CHECK_EQ(cam.get_screen_center().y, player.pos.y);
}

TEST_CASE("Camera update continues zoom animations when refresh is skipped") {
    Area base_area = make_base_area("base_zoom", 800, 600);
    camera cam(800, 600, base_area);

    cam.zoom_to_scale(2.0, 10);
    const float initial_scale = cam.get_scale();

    cam.update_zoom(nullptr, nullptr, nullptr, false, kFrameDt);
    CHECK(cam.zooming_);
    CHECK_GT(cam.get_scale(), initial_scale);
}

TEST_CASE("Parallax offset preserves fractional precision") {
    Area base_area = make_base_area("base_zoom", 640, 480);
    camera cam(640, 480, base_area);

    const camera::RealismSettings& settings = cam.realism_settings();

    SDL_Point center = cam.get_screen_center();
    SDL_Point world{ center.x + 120, center.y + 80 };

    const float asset_screen_height = 96.0f;
    const float reference_height    = 128.0f;

    camera::RenderEffects effects = cam.compute_render_effects(world, asset_screen_height, reference_height);
    SDL_Point baseline            = cam.map_to_screen(world);

    CHECK_EQ(effects.screen_position.x, baseline.x);
    CHECK_EQ(effects.screen_position.y, baseline.y);

    const auto [minx, miny, maxx, maxy] = cam.get_current_view().get_bounds();
    const int view_width  = std::max(0, maxx - minx);
    const int view_height = std::max(0, maxy - miny);

    constexpr double EPS              = 1e-6;
    constexpr double SY               = 200.0;
    constexpr double PARALLAX_KV      = 0.25;
    constexpr double PARALLAX_STEEPEN = 1.5;
    constexpr double PARALLAX_MAX     = 4000.0;

    const double safe_scale       = std::max(1e-6, static_cast<double>(cam.get_scale()));
    const double pixels_per_world = 1.0 / safe_scale;

    const double raw_scale      = std::isfinite(cam.get_scale()) ? static_cast<double>(cam.get_scale()) : 0.0;
    const double zoom_norm      = std::clamp(raw_scale, 0.0, 1.0);
    const double height_at_zoom1 = std::isfinite(settings.height_at_zoom1)
                                       ? std::max(0.0f, settings.height_at_zoom1)
                                       : 0.0f;
    const double camera_height  = height_at_zoom1 * zoom_norm;

    const double tripod_distance = std::isfinite(settings.tripod_distance_y)
                                       ? static_cast<double>(settings.tripod_distance_y)
                                       : 0.0;

    const double base_x = static_cast<double>(center.x);
    const double base_y = static_cast<double>(center.y) - tripod_distance;

    const double dx = static_cast<double>(world.x) - base_x;
    const double dy = static_cast<double>(world.y) - base_y;

    const double ndy = (view_height > 0) ? dy / (static_cast<double>(view_height) * 0.5) : 0.0;
    const double ndx = (view_width > 0) ? dx / (static_cast<double>(view_width) * 0.5) : 0.0;

    const double vertical_bias = 1.0 + PARALLAX_KV *
        std::tanh(ndy * (static_cast<double>(view_height) / SY) * PARALLAX_STEEPEN);

    double zoom_gain = (height_at_zoom1 > EPS) ? (height_at_zoom1 / (camera_height + EPS)) : 1.0;
    if (zoom_gain >= 1.0) {
        zoom_gain = std::pow(zoom_gain, 1.5);
    }

    double expected_parallax = static_cast<double>(settings.parallax_strength) *
                               ndx * ndy *
                               pixels_per_world * vertical_bias * zoom_gain;

    expected_parallax = std::clamp(expected_parallax, -PARALLAX_MAX, PARALLAX_MAX);

    CHECK(effects.parallax_offset_x == doctest::Approx(expected_parallax).epsilon(1e-6));
    CHECK_GT(std::fabs(effects.parallax_offset_x - std::round(effects.parallax_offset_x)), 1e-3);
}
