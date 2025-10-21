#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "dev_mode/dev_footer_bar.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/widgets.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

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

const DevFooterBar::Button* require_button(const DevFooterBar& footer, const std::string& id) {
    const auto* btn = footer.find_button(id);
    INFO("Expected footer button with id '" << id << "'");
    REQUIRE(btn != nullptr);
    return btn;
}

std::vector<std::string> button_ids(const DevFooterBar& footer) {
    std::vector<std::string> ids;
    const auto& buttons = footer.buttons();
    ids.reserve(buttons.size());
    for (const auto& b : buttons) {
        ids.push_back(b.id);
    }
    return ids;
}
}  // namespace

TEST_CASE("Footer buttons preserve insertion order and state") {
    ensure_sdl();

    DevFooterBar footer("Dev Footer");
    footer.set_bounds(800, 400);

    int toggled = 0;
    std::vector<std::pair<std::string, bool>> toggles;
    std::vector<DevFooterBar::Button> buttons;

    buttons.push_back({"switch_mode", "Switch", true, [&](bool active) {
                           ++toggled;
                           toggles.emplace_back("switch_mode", active);
                       }});
    buttons.push_back({"export", "Export", false, [&](bool active) {
                           ++toggled;
                           toggles.emplace_back("export", active);
                       }, true});
    buttons.push_back({"lighting", "Lighting", false, [&](bool active) {
                           ++toggled;
                           toggles.emplace_back("lighting", active);
                       }});

    footer.set_buttons(std::move(buttons));

    auto ids = button_ids(footer);
    CHECK_EQ(ids, std::vector<std::string>{"switch_mode", "export", "lighting"});

    const auto* switch_btn = require_button(footer, "switch_mode");
    CHECK(switch_btn->active);
    CHECK_FALSE(require_button(footer, "export")->active);
    CHECK_FALSE(require_button(footer, "lighting")->active);

    footer.set_active_button("lighting", true);
    CHECK_EQ(toggled, 2); // switch_mode false, lighting true
    CHECK_EQ(toggles.size(), 2);
    CHECK_EQ(toggles[0], std::make_pair(std::string("switch_mode"), false));
    CHECK_EQ(toggles[1], std::make_pair(std::string("lighting"), true));

    CHECK_FALSE(require_button(footer, "switch_mode")->active);
    CHECK(require_button(footer, "lighting")->active);
}

TEST_CASE("Momentary buttons never remain latched after activation") {
    ensure_sdl();

    DevFooterBar footer("Momentary Test");
    footer.set_bounds(640, 360);

    bool triggered = false;
    std::vector<DevFooterBar::Button> buttons;
    buttons.push_back({"moment", "Moment", false, [&](bool active) {
                           triggered = active;
                       }, true});

    footer.set_buttons(std::move(buttons));
    auto* btn = require_button(footer, "moment");
    SDL_Rect rect = btn->widget ? btn->widget->rect() : SDL_Rect{0, 0, 0, 0};
    SDL_Event down{};
    down.type = SDL_MOUSEBUTTONDOWN;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.x = rect.x + rect.w / 2;
    down.button.y = rect.y + rect.h / 2;
    SDL_Event up = down;
    up.type = SDL_MOUSEBUTTONUP;

    footer.handle_event(down);
    footer.handle_event(up);

    CHECK(triggered);
    CHECK_FALSE(btn->active);

    footer.set_button_active_state("moment", true);
    CHECK_FALSE(btn->active);
}

TEST_CASE("Manual active state updates without triggering callbacks") {
    ensure_sdl();

    DevFooterBar footer("Manual Active");
    footer.set_bounds(1024, 300);

    std::vector<std::pair<std::string, bool>> events;
    std::vector<DevFooterBar::Button> buttons;
    buttons.push_back({"layers", "Layers", false, [&](bool active) {
                           events.emplace_back("layers", active);
                       }});
    buttons.push_back({"lights", "Lights", false, [&](bool active) {
                           events.emplace_back("lights", active);
                       }});

    footer.set_buttons(std::move(buttons));

    footer.set_button_active_state("layers", true);
    CHECK(require_button(footer, "layers")->active);
    CHECK(events.empty());

    footer.set_active_button("lights", true);
    CHECK(require_button(footer, "lights")->active);
    CHECK_FALSE(require_button(footer, "layers")->active);
    REQUIRE_EQ(events.size(), 2);
    CHECK_EQ(events[0], std::make_pair(std::string("layers"), false));
    CHECK_EQ(events[1], std::make_pair(std::string("lights"), true));
}

TEST_CASE("Footer rect anchors to bottom of the screen") {
    ensure_sdl();

    DevFooterBar footer("Layout Test");
    footer.set_bounds(1200, 600);

    const SDL_Rect rect = footer.rect();
    CHECK_EQ(rect.w, 1200);
    CHECK_EQ(rect.h, 40); // default height
    CHECK_EQ(rect.x, 0);
    CHECK_EQ(rect.y + rect.h, 600);

    footer.set_height(64);
    footer.set_bounds(800, 500);
    const SDL_Rect rect2 = footer.rect();
    CHECK_EQ(rect2.w, 800);
    CHECK_EQ(rect2.h, 64);
    CHECK_EQ(rect2.y + rect2.h, 500);
}

