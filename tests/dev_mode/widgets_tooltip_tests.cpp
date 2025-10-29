#include "doctest/doctest.h"

#include "dev_mode/widgets.hpp"
#include "dev_mode/dm_styles.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

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

int icon_center(const SDL_Rect& rect) {
    return rect.x + rect.w / 2;
}
}

TEST_CASE("DMButton tooltip activation respects enabled flag and hover delay") {
    ensure_sdl();

    DMButton button("Tooltip", &DMStyles::HeaderButton(), 160, DMButton::height());
    DMWidgetTooltipState state;
    state.text = "Informative tooltip";
    state.enabled = false;
    button.set_tooltip_state(&state);

    SDL_Rect icon_rect = DMWidgetTooltipIconRect(button.rect());
    SDL_Event down{};
    down.type = SDL_MOUSEBUTTONDOWN;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.x = icon_center(icon_rect);
    down.button.y = icon_center(icon_rect);
    CHECK(button.handle_event(down));

    SDL_Event up{};
    up.type = SDL_MOUSEBUTTONUP;
    up.button.button = SDL_BUTTON_LEFT;
    up.button.x = icon_center(icon_rect);
    up.button.y = icon_center(icon_rect);
    CHECK(button.handle_event(up));
    CHECK_FALSE(state.icon_hovered);

    state.enabled = true;
    button.set_tooltip_state(&state);

    SDL_Event motion{};
    motion.type = SDL_MOUSEMOTION;
    motion.motion.x = icon_center(icon_rect);
    motion.motion.y = icon_center(icon_rect);
    CHECK_FALSE(button.handle_event(motion));
    CHECK(state.icon_hovered);
    REQUIRE(state.hover_start_ms != 0u);

    CHECK(button.handle_event(down));
    CHECK_FALSE(button.handle_event(up));

    CHECK_FALSE(DMWidgetTooltipShouldDisplay(state, state.hover_start_ms + 500));
    CHECK(DMWidgetTooltipShouldDisplay(state, state.hover_start_ms + 1500));
}

TEST_CASE("Widget tooltip helpers keep state in sync") {
    ensure_sdl();

    DMButton button("Wrapper", &DMStyles::HeaderButton(), 180, DMButton::height());
    ButtonWidget widget(&button);

    widget.set_tooltip_text("Wrapper tooltip");
    CHECK_FALSE(widget.tooltip_enabled());

    widget.set_tooltip_enabled(true);
    CHECK(widget.tooltip_enabled());

    SDL_Rect icon_rect = DMWidgetTooltipIconRect(widget.rect());
    SDL_Event motion{};
    motion.type = SDL_MOUSEMOTION;
    motion.motion.x = icon_center(icon_rect);
    motion.motion.y = icon_center(icon_rect);
    CHECK_FALSE(widget.handle_event(motion));

    SDL_Event down{};
    down.type = SDL_MOUSEBUTTONDOWN;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.x = icon_center(icon_rect);
    down.button.y = icon_center(icon_rect);
    CHECK(widget.handle_event(down));

    SDL_Event up{};
    up.type = SDL_MOUSEBUTTONUP;
    up.button.button = SDL_BUTTON_LEFT;
    up.button.x = icon_center(icon_rect);
    up.button.y = icon_center(icon_rect);
    CHECK_FALSE(widget.handle_event(up));

    widget.set_tooltip_text("");
    CHECK_FALSE(widget.tooltip_enabled());
}
