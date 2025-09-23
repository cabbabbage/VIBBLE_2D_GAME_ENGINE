#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "dev_mode/full_screen_collapsible.hpp"
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

const FullScreenCollapsible::HeaderButton* require_button(const FullScreenCollapsible& footer,
                                                          const std::string& id) {
    const auto* btn = footer.find_button(id);
    INFO("Expected footer button with id '" << id << "'");
    REQUIRE(btn != nullptr);
    return btn;
}

std::vector<std::string> button_ids(const FullScreenCollapsible& footer) {
    std::vector<std::string> ids;
    const auto& buttons = footer.header_buttons();
    ids.reserve(buttons.size());
    for (const auto& b : buttons) {
        ids.push_back(b.id);
    }
    return ids;
}
}

TEST_CASE("Header buttons preserve insertion order and state") {
    ensure_sdl();

    FullScreenCollapsible footer("Dev Footer");
    footer.set_bounds(800, 400);

    int toggled = 0;
    std::vector<std::pair<std::string, bool>> toggles;
    std::vector<FullScreenCollapsible::HeaderButton> buttons;

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

    footer.set_header_buttons(std::move(buttons));

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

    FullScreenCollapsible footer("Momentary Test");
    footer.set_bounds(640, 360);

    bool triggered = false;
    std::vector<FullScreenCollapsible::HeaderButton> buttons;
    buttons.push_back({"moment", "Moment", false, [&](bool active) {
                           triggered = active;
                       }, true});

    footer.set_header_buttons(std::move(buttons));
    auto* btn = require_button(footer, "moment");
    SDL_Rect rect = btn->widget ? btn->widget->rect() : SDL_Rect{0,0,0,0};
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

    FullScreenCollapsible footer("Manual Active");
    footer.set_bounds(1024, 300);

    std::vector<std::pair<std::string, bool>> events;
    std::vector<FullScreenCollapsible::HeaderButton> buttons;
    buttons.push_back({"layers", "Layers", false, [&](bool active) {
                           events.emplace_back("layers", active);
                       }});
    buttons.push_back({"lights", "Lights", false, [&](bool active) {
                           events.emplace_back("lights", active);
                       }});

    footer.set_header_buttons(std::move(buttons));

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

TEST_CASE("Layout reacts to expansion state and bounds") {
    ensure_sdl();

    FullScreenCollapsible footer("Layout Test");
    footer.set_bounds(1200, 600);

    footer.set_header_buttons({});

    CHECK_FALSE(footer.expanded());
    auto header = footer.header_rect();
    auto content = footer.content_rect();

    CHECK_EQ(header.w, 1200);
    CHECK_EQ(header.h, footer.header_rect().h);
    CHECK_EQ(content.h, 0);

    footer.set_expanded(true);
    CHECK(footer.expanded());
    header = footer.header_rect();
    content = footer.content_rect();
    CHECK_EQ(header.y, 0);
    CHECK_EQ(content.y, header.y + header.h);
    CHECK_EQ(content.w, 1200);
    CHECK_EQ(content.h, 600 - header.h);

    footer.set_bounds(800, 400);
    header = footer.header_rect();
    content = footer.content_rect();
    CHECK_EQ(header.w, 800);
    CHECK_EQ(content.w, 800);
    CHECK_EQ(content.h, 400 - header.h);
}

TEST_CASE("Arrow button toggles expanded state via synthesized events") {
    ensure_sdl();

    FullScreenCollapsible footer("Arrow Toggle");
    footer.set_bounds(640, 480);
    footer.set_header_buttons({});

    auto click_arrow = [&](int x, int y) {
        SDL_Event down{};
        down.type = SDL_MOUSEBUTTONDOWN;
        down.button.button = SDL_BUTTON_LEFT;
        down.button.x = x;
        down.button.y = y;
        SDL_Event up = down;
        up.type = SDL_MOUSEBUTTONUP;
        footer.handle_event(down);
        footer.handle_event(up);
    };

    auto arrow_point = [&]() {
        const SDL_Rect header = footer.header_rect();
        const int arrow_w = 36; // matches implementation constant
        const int gap = DMSpacing::item_gap();
        const int x = header.x + header.w - arrow_w / 2 - gap;
        const int y = header.y + header.h / 2;
        return SDL_Point{x, y};
    };

    SDL_Point p = arrow_point();
    click_arrow(p.x, p.y);
    CHECK(footer.expanded());

    p = arrow_point();
    click_arrow(p.x, p.y);
    CHECK_FALSE(footer.expanded());
}
