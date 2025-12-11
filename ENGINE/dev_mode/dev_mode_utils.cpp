#include "dev_mode_utils.hpp"

#include "dm_styles.hpp"
#include <SDL_ttf.h>
#include <algorithm>
#include <string>
#include <unordered_map>

namespace devmode::utils {

TTF_Font* load_font(int size) {
    static std::unordered_map<int, TTF_Font*> cache;
    auto it = cache.find(size);
    if (it != cache.end()) return it->second;

    const DMLabelStyle& label = DMStyles::Label();
    TTF_Font* font = TTF_OpenFont(label.font_path.c_str(), size);
    if (!font) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[DevModeUtils] Failed to load font '%s' size %d: %s", label.font_path.c_str(), size, TTF_GetError());
        return nullptr;
    }
    cache.emplace(size, font);
    return font;
}

std::string trim_whitespace_copy(const std::string& value) {
    auto begin = value.begin();
    auto end = value.end();
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) ++begin;
    while (end != begin) {
        auto prev = end; --prev;
        if (!std::isspace(static_cast<unsigned char>(*prev))) break;
        end = prev;
    }
    return std::string(begin, end);
}

}
