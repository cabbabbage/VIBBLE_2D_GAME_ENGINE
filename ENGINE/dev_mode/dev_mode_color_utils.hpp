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

inline SDL_Color with_alpha(SDL_Color color, Uint8 alpha) {
    color.a = alpha;
    return color;
}

constexpr int kLabelPadding = 6;
constexpr int kLabelVerticalOffset = 32;
const SDL_Color kLabelBg{32, 32, 32, 200};
const SDL_Color kLabelBorder{255, 255, 255, 96};
const SDL_Color kLabelText{240, 240, 240, 255};

inline float display_color_luminance(SDL_Color color) {
    return static_cast<float>(0.2126 * static_cast<double>(color.r) / 255.0 + 0.7152 * static_cast<double>(color.g) / 255.0 + 0.0722 * static_cast<double>(color.b) / 255.0);
}

inline bool colors_equal(SDL_Color lhs, SDL_Color rhs) {
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}
