#include "font_cache.hpp"

#include "dm_styles.hpp"

namespace {
constexpr SDL_Point kZeroPoint{0, 0};
}

DMFontCache& DMFontCache::instance() {
    static DMFontCache cache;
    return cache;
}

bool DMFontCache::FontKey::operator==(const FontKey& other) const {
    return size == other.size && path == other.path;
}

std::size_t DMFontCache::FontKeyHash::operator()(const FontKey& key) const noexcept {
    std::size_t h1 = std::hash<std::string>{}(key.path);
    std::size_t h2 = std::hash<int>{}(key.size);
    return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
}

DMFontCache::~DMFontCache() {
    clear();
}

TTF_Font* DMFontCache::load_font(const std::string& path, int size) const {
    if (path.empty() || size <= 0) {
        return nullptr;
    }
    return TTF_OpenFont(path.c_str(), size);
}

TTF_Font* DMFontCache::get_font(const std::string& path, int size) const {
    FontKey key{path, size};
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = fonts_.find(key);
    if (it != fonts_.end()) {
        return it->second;
    }
    TTF_Font* font = load_font(path, size);
    if (!font) {
        return nullptr;
    }
    fonts_.emplace(std::move(key), font);
    return font;
}

SDL_Point DMFontCache::measure_text(const std::string& path, int size, const std::string& text) const {
    if (text.empty()) {
        return kZeroPoint;
    }
    TTF_Font* font = get_font(path, size);
    if (!font) {
        return kZeroPoint;
    }
    int w = 0;
    int h = 0;
    if (TTF_SizeUTF8(font, text.c_str(), &w, &h) != 0) {
        return kZeroPoint;
    }
    return SDL_Point{w, h};
}

SDL_Point DMFontCache::measure_text(const DMLabelStyle& style, const std::string& text) const {
    return measure_text(style.font_path, style.font_size, text);
}

bool DMFontCache::draw_text(SDL_Renderer* renderer,
                            const std::string& path,
                            int size,
                            const std::string& text,
                            SDL_Color color,
                            int x,
                            int y,
                            SDL_Rect* out_rect) const {
    if (!renderer || text.empty()) {
        if (out_rect) {
            *out_rect = SDL_Rect{x, y, 0, 0};
        }
        return false;
    }
    TTF_Font* font = get_font(path, size);
    if (!font) {
        return false;
    }
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surf) {
        return false;
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    if (!tex) {
        SDL_FreeSurface(surf);
        return false;
    }
    SDL_Rect dst{x, y, surf->w, surf->h};
    SDL_RenderCopy(renderer, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    if (out_rect) {
        *out_rect = dst;
    }
    SDL_FreeSurface(surf);
    return true;
}

bool DMFontCache::draw_text(SDL_Renderer* renderer,
                            const DMLabelStyle& style,
                            const std::string& text,
                            int x,
                            int y,
                            SDL_Rect* out_rect) const {
    return draw_text(renderer, style.font_path, style.font_size, text, style.color, x, y, out_rect);
}

void DMFontCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : fonts_) {
        if (entry.second) {
            TTF_CloseFont(entry.second);
        }
    }
    fonts_.clear();
}

SDL_Point MeasureLabelText(const DMLabelStyle& style, const std::string& text) {
    return DMFontCache::instance().measure_text(style, text);
}

bool DrawLabelText(SDL_Renderer* renderer,
                   const std::string& text,
                   int x,
                   int y,
                   const DMLabelStyle& style,
                   SDL_Rect* out_rect) {
    return DMFontCache::instance().draw_text(renderer, style, text, x, y, out_rect);
}

bool DrawLabelText(SDL_Renderer* renderer,
                   const std::string& text,
                   const SDL_Rect& rect,
                   const DMLabelStyle& style,
                   SDL_Rect* out_rect) {
    SDL_Rect dst;
    bool result = DMFontCache::instance().draw_text(renderer, style, text, rect.x, rect.y, &dst);
    if (out_rect) {
        *out_rect = dst;
    }
    return result;
}

