#pragma once

#include <SDL.h>

namespace devmode::sdl {

bool is_pointer_event(const SDL_Event& e);

SDL_Point event_point(const SDL_Event& e);

}

