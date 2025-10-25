#pragma once

#include <algorithm>
#include <cmath>

#include <SDL.h>

inline SDL_Color mix_color(SDL_Color a, SDL_Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto mix = [t](Uint8 x, Uint8 y) {
        return static_cast<Uint8>(std::lround((1.0f - t) * x + t * y));
};
    return SDL_Color{mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}

inline SDL_Color lighten(SDL_Color c, float amount) {
    return mix_color(c, SDL_Color{255, 255, 255, c.a}, amount);
}

inline SDL_Color darken(SDL_Color c, float amount) {
    return mix_color(c, SDL_Color{0, 0, 0, c.a}, amount);
}
