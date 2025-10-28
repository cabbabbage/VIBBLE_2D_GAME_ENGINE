#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <SDL.h>
#include <string>
#include <vector>

#include "render/camera.hpp"
#include "asset/Asset.hpp"

namespace {
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
    cam.update_zoom(nullptr, nullptr, &player, true);
    CHECK_EQ(cam.get_screen_center().x, player.pos.x);
    CHECK_EQ(cam.get_screen_center().y, player.pos.y);

    SDL_Point locked_center = cam.get_screen_center();

    player.pos = SDL_Point{120, 220};
    cam.update_zoom(nullptr, nullptr, &player, false);
    CHECK_EQ(cam.get_screen_center().x, locked_center.x);
    CHECK_EQ(cam.get_screen_center().y, locked_center.y);

    cam.update_zoom(nullptr, nullptr, &player, true);
    CHECK_EQ(cam.get_screen_center().x, player.pos.x);
    CHECK_EQ(cam.get_screen_center().y, player.pos.y);
}

TEST_CASE("Camera update continues zoom animations when refresh is skipped") {
    Area base_area = make_base_area("base_zoom", 800, 600);
    camera cam(800, 600, base_area);

    cam.zoom_to_scale(2.0, 10);
    const float initial_scale = cam.get_scale();

    cam.update_zoom(nullptr, nullptr, nullptr, false);
    CHECK(cam.zooming_);
    CHECK_GT(cam.get_scale(), initial_scale);
}
