#pragma once

#include <SDL.h>
#include <SDL_ttf.h>

#include <mutex>
#include <string>
#include <unordered_map>

struct DMLabelStyle;

class DMFontCache {
public:
    static DMFontCache& instance();

    TTF_Font* get_font(const std::string& path, int size) const;

    SDL_Point measure_text(const std::string& path, int size, const std::string& text) const;
    SDL_Point measure_text(const DMLabelStyle& style, const std::string& text) const;

    bool draw_text(SDL_Renderer* renderer, const std::string& path, int size, const std::string& text, SDL_Color color, int x, int y, SDL_Rect* out_rect = nullptr) const;

    bool draw_text(SDL_Renderer* renderer, const DMLabelStyle& style, const std::string& text, int x, int y, SDL_Rect* out_rect = nullptr) const;

    void clear();

private:
    struct FontKey {
        std::string path;
        int size = 0;

        bool operator==(const FontKey& other) const;
};

    struct FontKeyHash {
        std::size_t operator()(const FontKey& key) const noexcept;
};

    DMFontCache() = default;
    ~DMFontCache();

    DMFontCache(const DMFontCache&) = delete;
    DMFontCache& operator=(const DMFontCache&) = delete;

    TTF_Font* load_font(const std::string& path, int size) const;

    mutable std::unordered_map<FontKey, TTF_Font*, FontKeyHash> fonts_;
    mutable std::mutex mutex_;
};

SDL_Point MeasureLabelText(const DMLabelStyle& style, const std::string& text);

bool DrawLabelText(SDL_Renderer* renderer, const std::string& text, int x, int y, const DMLabelStyle& style, SDL_Rect* out_rect = nullptr);

bool DrawLabelText(SDL_Renderer* renderer, const std::string& text, const SDL_Rect& rect, const DMLabelStyle& style, SDL_Rect* out_rect = nullptr);

