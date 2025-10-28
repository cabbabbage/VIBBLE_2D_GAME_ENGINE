#define DOCTEST_CONFIG_IMPLEMENT_WITHOUT_MAIN
#include "doctest/doctest.h"

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdexcept>
#include <string>
#include <vector>

#define private public
#define protected public
#include "dev_mode/dev_controls.hpp"
#undef private
#undef protected

#include "render/camera.hpp"
#include "utils/area.hpp"

namespace {
class SDLSubsystemGuard {
public:
    SDLSubsystemGuard() {
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            throw std::runtime_error(SDL_GetError());
        }
        if (TTF_Init() != 0) {
            std::string err = TTF_GetError();
            SDL_Quit();
            throw std::runtime_error(err);
        }
    }

    ~SDLSubsystemGuard() {
        TTF_Quit();
        SDL_Quit();
    }
};

SDLSubsystemGuard& ensure_sdl() {
    static SDLSubsystemGuard guard;
    return guard;
}
}

TEST_CASE("Camera realism toggles with DevControls mode transitions") {
    ensure_sdl();

    std::vector<SDL_Point> square{{0, 0}, {128, 0}, {128, 128}, {0, 128}};
    Area base_area("test", square);
    camera cam(800, 600, base_area);

    DevControls controls(nullptr, 800, 600);
    controls.set_camera_override_for_testing(&cam);

    // Initial mode is RoomEditor; realism should be disabled in editor.
    controls.set_mode(DevControls::Mode::RoomEditor);
    CHECK_FALSE(cam.realism_enabled());

    // Switching to Map mode should re-enable realism.
    controls.enter_map_editor_mode();
    CHECK_EQ(controls.mode(), DevControls::Mode::MapEditor);
    CHECK(cam.realism_enabled());

    // Exiting map mode restores room editor realism setting (disabled).
    controls.exit_map_editor_mode(false, true);
    CHECK_EQ(controls.mode(), DevControls::Mode::RoomEditor);
    CHECK_FALSE(cam.realism_enabled());

    controls.set_mode(DevControls::Mode::RoomEditor);
    CHECK_FALSE(cam.realism_enabled());
}

TEST_CASE("Camera zoom is preserved when switching modes") {
    ensure_sdl();

    std::vector<SDL_Point> square{{0, 0}, {256, 0}, {256, 256}, {0, 256}};
    Area base_area("test_zoom", square);
    camera cam(800, 600, base_area);

    DevControls controls(nullptr, 800, 600);
    controls.set_camera_override_for_testing(&cam);

    controls.set_mode(DevControls::Mode::RoomEditor);
    const double initial_scale = cam.get_scale();
    const bool initial_focus_override = cam.has_focus_override();

    controls.enter_map_editor_mode();
    CHECK_EQ(cam.get_scale(), doctest::Approx(initial_scale));
    CHECK_EQ(cam.has_focus_override(), initial_focus_override);

    controls.exit_map_editor_mode(false, true);
    CHECK_EQ(cam.get_scale(), doctest::Approx(initial_scale));
    CHECK_EQ(cam.has_focus_override(), initial_focus_override);

    controls.set_mode(DevControls::Mode::RoomEditor);
    CHECK_EQ(cam.get_scale(), doctest::Approx(initial_scale));

    controls.set_mode(DevControls::Mode::RoomEditor);
    CHECK_EQ(cam.get_scale(), doctest::Approx(initial_scale));
}
