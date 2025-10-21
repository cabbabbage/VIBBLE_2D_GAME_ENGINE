#define DOCTEST_CONFIG_IMPLEMENT_WITHOUT_MAIN
#include "doctest/doctest.h"

#include "dev_mode/map_mode_ui.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "utils/input.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

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

TEST_CASE("Layers panel toggles visibility via footer button") {
    ensure_sdl();

    devmode::core::ManifestStore store;
    MapModeUI ui(nullptr);
    ui.set_manifest_store(&store);

    nlohmann::json map_info = nlohmann::json::object();
    map_info["rooms_data"] = nlohmann::json::object();

    ui.set_map_context(&map_info, "test_map");
    ui.set_screen_dimensions(1280, 720);
    ui.set_footer_always_visible(true);
    ui.set_map_mode_active(true);

    Input input;
    ui.update(input);
    CHECK_FALSE(ui.is_layers_panel_visible());

    ui.toggle_layers_panel();
    ui.update(input);
    CHECK(ui.is_layers_panel_visible());

    ui.toggle_layers_panel();
    ui.update(input);
    CHECK_FALSE(ui.is_layers_panel_visible());
}
