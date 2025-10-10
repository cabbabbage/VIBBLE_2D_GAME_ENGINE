#include "dev_mode/sdl_pointer_utils.hpp"

namespace devmode::sdl {

bool is_pointer_event(const SDL_Event& e) {
    return e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION;
}

SDL_Point event_point(const SDL_Event& e) {
    if (e.type == SDL_MOUSEMOTION) {
        return SDL_Point{e.motion.x, e.motion.y};
    }
    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
        return SDL_Point{e.button.x, e.button.y};
    }
    int mx = 0;
    int my = 0;
    SDL_GetMouseState(&mx, &my);
    return SDL_Point{mx, my};
}

}

