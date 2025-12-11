#ifndef DEV_MODE_UTILS_HPP
#define DEV_MODE_UTILS_HPP

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <unordered_map>

namespace devmode::utils {

inline SDL_Color with_alpha(SDL_Color c, Uint8 a) { c.a = a; return c; }

TTF_Font* load_font(int size);
std::string trim_whitespace_copy(const std::string& value);

}

#endif
