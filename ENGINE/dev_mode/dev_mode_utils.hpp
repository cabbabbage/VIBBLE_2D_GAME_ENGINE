#ifndef DEV_MODE_UTILS_HPP
#define DEV_MODE_UTILS_HPP

#include <SDL_ttf.h>
#include <string>
#include <unordered_map>

namespace devmode::utils {

TTF_Font* load_font(int size);
std::string trim_whitespace_copy(const std::string& value);

} // namespace devmode::utils

#endif // DEV_MODE_UTILS_HPP
