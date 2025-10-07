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

    // Initial mode is RoomEditor; realism should be enabled.
    controls.set_mode(DevControls::Mode::RoomEditor);
    CHECK(cam.realism_enabled());

    // Enter Area mode and ensure realism is disabled.
    controls.set_mode(DevControls::Mode::AreaMode);
    CHECK_FALSE(cam.realism_enabled());

    // Switching to Map mode should re-enable realism.
    controls.enter_map_editor_mode();
    CHECK_EQ(controls.mode(), DevControls::Mode::MapEditor);
    CHECK(cam.realism_enabled());

    // Exiting map mode restores room mode realism.
    controls.exit_map_editor_mode(false, true);
    CHECK_EQ(controls.mode(), DevControls::Mode::RoomEditor);
    CHECK(cam.realism_enabled());

    // Returning to area mode disables realism again.
    controls.set_mode(DevControls::Mode::AreaMode);
    CHECK_FALSE(cam.realism_enabled());

    controls.set_mode(DevControls::Mode::RoomEditor);
    CHECK(cam.realism_enabled());
}
