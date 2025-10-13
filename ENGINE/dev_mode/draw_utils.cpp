#include "draw_utils.hpp"

#include <algorithm>

namespace dm_draw {
namespace {

Uint8 clamp_to_byte(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return static_cast<Uint8>(value);
}

SDL_Color blend_toward(const SDL_Color& color, float amount, bool lighten) {
    amount = std::clamp(amount, 0.0f, 1.0f);
    SDL_Color result = color;
    if (lighten) {
        result.r = clamp_to_byte(static_cast<int>(color.r + (255 - color.r) * amount + 0.5f));
        result.g = clamp_to_byte(static_cast<int>(color.g + (255 - color.g) * amount + 0.5f));
        result.b = clamp_to_byte(static_cast<int>(color.b + (255 - color.b) * amount + 0.5f));
    } else {
        result.r = clamp_to_byte(static_cast<int>(color.r * (1.0f - amount) + 0.5f));
        result.g = clamp_to_byte(static_cast<int>(color.g * (1.0f - amount) + 0.5f));
        result.b = clamp_to_byte(static_cast<int>(color.b * (1.0f - amount) + 0.5f));
    }
    return result;
}

void draw_horizontal(SDL_Renderer* renderer, int y, int x0, int x1) {
    if (x0 > x1) std::swap(x0, x1);
    SDL_RenderDrawLine(renderer, x0, y, x1, y);
}

void draw_vertical(SDL_Renderer* renderer, int x, int y0, int y1) {
    if (y0 > y1) std::swap(y0, y1);
    SDL_RenderDrawLine(renderer, x, y0, x, y1);
}

} // namespace

SDL_Color LightenColor(const SDL_Color& color, float amount) {
    return blend_toward(color, amount, true);
}

SDL_Color DarkenColor(const SDL_Color& color, float amount) {
    return blend_toward(color, amount, false);
}

void DrawBeveledRect(
    SDL_Renderer* renderer,
    const SDL_Rect& rect,
    int corner_radius,
    int bevel_depth,
    const SDL_Color& fill,
    const SDL_Color& highlight,
    const SDL_Color& shadow,
    bool draw_outline,
    float highlight_intensity,
    float shadow_intensity) {
    if (!renderer || rect.w <= 0 || rect.h <= 0) return;

    const int max_radius = std::max(0, std::min(rect.w, rect.h) / 2);
    corner_radius = std::clamp(corner_radius, 0, max_radius);

    const int max_bevel = std::max(0, std::min({bevel_depth, rect.w / 2, rect.h / 2}));

    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &rect);

    if (max_bevel > 0) {
        const int bevel_iterations = max_bevel;
        for (int i = 0; i < bevel_iterations; ++i) {
            const float t = (bevel_iterations <= 1) ? 1.0f : (1.0f - static_cast<float>(i) / (bevel_iterations - 1));
            const SDL_Color top_color = LightenColor(highlight, highlight_intensity * t);
            const SDL_Color left_color = LightenColor(highlight, highlight_intensity * t);
            const SDL_Color bottom_color = DarkenColor(shadow, shadow_intensity * t);
            const SDL_Color right_color = DarkenColor(shadow, shadow_intensity * t);

            const int inset = i;
            const int min_x = rect.x + corner_radius + inset;
            const int max_x = rect.x + rect.w - corner_radius - inset - 1;
            const int min_y = rect.y + corner_radius + inset;
            const int max_y = rect.y + rect.h - corner_radius - inset - 1;

            if (min_x <= max_x) {
                SDL_SetRenderDrawColor(renderer, top_color.r, top_color.g, top_color.b, top_color.a);
                draw_horizontal(renderer, rect.y + inset, min_x, max_x);

                SDL_SetRenderDrawColor(renderer, bottom_color.r, bottom_color.g, bottom_color.b, bottom_color.a);
                draw_horizontal(renderer, rect.y + rect.h - 1 - inset, min_x, max_x);
            }

            if (min_y <= max_y) {
                SDL_SetRenderDrawColor(renderer, left_color.r, left_color.g, left_color.b, left_color.a);
                draw_vertical(renderer, rect.x + inset, min_y, max_y);

                SDL_SetRenderDrawColor(renderer, right_color.r, right_color.g, right_color.b, right_color.a);
                draw_vertical(renderer, rect.x + rect.w - 1 - inset, min_y, max_y);
            }
        }
    }

    if (draw_outline) {
        const SDL_Color outline_color = DarkenColor(fill, 0.4f);
        SDL_SetRenderDrawColor(renderer, outline_color.r, outline_color.g, outline_color.b, outline_color.a);
        SDL_RenderDrawRect(renderer, &rect);
    }
}

} // namespace dm_draw

