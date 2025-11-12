#include "draw_utils.hpp"

#include <algorithm>
#include <cmath>

namespace dm_draw {
namespace {

Uint8 clamp_to_byte(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return static_cast<Uint8>(value);
}

int effective_corner_radius(const SDL_Rect& rect, int corner_radius) {
    if (rect.w <= 0 || rect.h <= 0) {
        return 0;
    }
    const int max_radius = std::max(0, std::min(rect.w, rect.h) / 2);
    return std::clamp(corner_radius, 0, max_radius);
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

bool compute_horizontal_span(const SDL_Rect& rect, int effective_radius, int inset, int y, int& out_start, int& out_end) {
    const int inner_x = rect.x + inset;
    const int inner_y = rect.y + inset;
    const int inner_w = rect.w - inset * 2;
    const int inner_h = rect.h - inset * 2;
    if (inner_w <= 0 || inner_h <= 0 || y < inner_y || y >= inner_y + inner_h) {
        return false;
    }

    const int radius = std::max(0, effective_radius - inset);
    int offset = 0;
    if (radius > 0) {
        const int rel_y = y - inner_y;
        if (rel_y < radius) {
            const float dy = static_cast<float>(radius - rel_y - 0.5f);
            const float dx = std::sqrt(std::max(0.0f, static_cast<float>(radius * radius) - dy * dy));
            offset = static_cast<int>(std::ceil(radius - dx));
        } else if (rel_y >= inner_h - radius) {
            const int rel_bottom = inner_h - 1 - rel_y;
            const float dy = static_cast<float>(radius - rel_bottom - 0.5f);
            const float dx = std::sqrt(std::max(0.0f, static_cast<float>(radius * radius) - dy * dy));
            offset = static_cast<int>(std::ceil(radius - dx));
        }
    }

    offset = std::min(offset, std::max(0, inner_w / 2));
    out_start = inner_x + offset;
    out_end = inner_x + inner_w - offset - 1;
    if (out_start > out_end) {
        const int mid = inner_x + inner_w / 2;
        out_start = mid;
        out_end = mid;
    }
    return true;
}

bool compute_vertical_span(const SDL_Rect& rect, int effective_radius, int inset, int x, int& out_start, int& out_end) {
    const int inner_x = rect.x + inset;
    const int inner_y = rect.y + inset;
    const int inner_w = rect.w - inset * 2;
    const int inner_h = rect.h - inset * 2;
    if (inner_w <= 0 || inner_h <= 0 || x < inner_x || x >= inner_x + inner_w) {
        return false;
    }

    const int radius = std::max(0, effective_radius - inset);
    int offset = 0;
    if (radius > 0) {
        const int rel_x = x - inner_x;
        if (rel_x < radius) {
            const float dx = static_cast<float>(radius - rel_x - 0.5f);
            const float dy = std::sqrt(std::max(0.0f, static_cast<float>(radius * radius) - dx * dx));
            offset = static_cast<int>(std::ceil(radius - dy));
        } else if (rel_x >= inner_w - radius) {
            const int rel_right = inner_w - 1 - rel_x;
            const float dx = static_cast<float>(radius - rel_right - 0.5f);
            const float dy = std::sqrt(std::max(0.0f, static_cast<float>(radius * radius) - dx * dx));
            offset = static_cast<int>(std::ceil(radius - dy));
        }
    }

    offset = std::min(offset, std::max(0, inner_h / 2));
    out_start = inner_y + offset;
    out_end = inner_y + inner_h - offset - 1;
    if (out_start > out_end) {
        const int mid = inner_y + inner_h / 2;
        out_start = mid;
        out_end = mid;
    }
    return true;
}

bool draw_outline_layer(SDL_Renderer* renderer, const SDL_Rect& rect, int effective_radius, int inset) {
    int start = 0;
    int end = -1;
    bool drew_any = false;

    const int top_y = rect.y + inset;
    if (compute_horizontal_span(rect, effective_radius, inset, top_y, start, end)) {
        draw_horizontal(renderer, top_y, start, end);
        drew_any = true;
    }

    const int bottom_y = rect.y + rect.h - 1 - inset;
    if (bottom_y != top_y && compute_horizontal_span(rect, effective_radius, inset, bottom_y, start, end)) {
        draw_horizontal(renderer, bottom_y, start, end);
        drew_any = true;
    }

    const int left_x = rect.x + inset;
    if (compute_vertical_span(rect, effective_radius, inset, left_x, start, end)) {
        draw_vertical(renderer, left_x, start, end);
        drew_any = true;
    }

    const int right_x = rect.x + rect.w - 1 - inset;
    if (right_x != left_x && compute_vertical_span(rect, effective_radius, inset, right_x, start, end)) {
        draw_vertical(renderer, right_x, start, end);
        drew_any = true;
    }

    return drew_any;
}

SDL_Color lerp_color(const SDL_Color& a, const SDL_Color& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    SDL_Color result{};
    result.r = clamp_to_byte(static_cast<int>(a.r + (b.r - a.r) * t + 0.5f));
    result.g = clamp_to_byte(static_cast<int>(a.g + (b.g - a.g) * t + 0.5f));
    result.b = clamp_to_byte(static_cast<int>(a.b + (b.b - a.b) * t + 0.5f));
    result.a = clamp_to_byte(static_cast<int>(a.a + (b.a - a.a) * t + 0.5f));
    return result;
}

template <typename ColorProvider>
void fill_rounded_rect(SDL_Renderer* renderer,
                       const SDL_Rect& rect,
                       int corner_radius,
                       ColorProvider&& color_provider) {
    if (!renderer || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    const int effective_radius = effective_corner_radius(rect, corner_radius);
    const int span = std::max(1, rect.h - 1);

    for (int y = rect.y; y < rect.y + rect.h; ++y) {
        int span_start = 0;
        int span_end = -1;
        if (!compute_horizontal_span(rect, effective_radius, 0, y, span_start, span_end)) {
            continue;
        }

        const float t = static_cast<float>(y - rect.y) / static_cast<float>(span);
        SDL_Color line_color = color_provider(t);
        SDL_SetRenderDrawColor(renderer, line_color.r, line_color.g, line_color.b, line_color.a);
        SDL_RenderDrawLine(renderer, span_start, y, span_end, y);
    }
}

}

SDL_Color LightenColor(const SDL_Color& color, float amount) {
    return blend_toward(color, amount, true);
}

SDL_Color DarkenColor(const SDL_Color& color, float amount) {
    return blend_toward(color, amount, false);
}

void DrawRoundedSolidRect(SDL_Renderer* renderer,
                          const SDL_Rect& rect,
                          int corner_radius,
                          const SDL_Color& color) {
    fill_rounded_rect(renderer, rect, corner_radius, [color](float) { return color; });
}

void DrawRoundedGradientRect(SDL_Renderer* renderer,
                             const SDL_Rect& rect,
                             int corner_radius,
                             const SDL_Color& top_color,
                             const SDL_Color& bottom_color) {
    fill_rounded_rect(renderer, rect, corner_radius, [top_color, bottom_color](float t) {
        return lerp_color(top_color, bottom_color, t);
    });
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

    (void)bevel_depth;
    (void)highlight;
    (void)shadow;
    (void)highlight_intensity;
    (void)shadow_intensity;

    const int effective_radius = effective_corner_radius(rect, corner_radius);

    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    for (int y = rect.y; y < rect.y + rect.h; ++y) {
        int span_start = 0;
        int span_end = -1;
        if (compute_horizontal_span(rect, effective_radius, 0, y, span_start, span_end)) {
            SDL_RenderDrawLine(renderer, span_start, y, span_end, y);
        }
    }

    if (draw_outline) {
        const SDL_Color outline_color = DarkenColor(fill, 0.4f);
        DrawRoundedOutline(renderer, rect, effective_radius, 1, outline_color);
    }
}

void DrawRoundedOutline(
    SDL_Renderer* renderer,
    const SDL_Rect& rect,
    int corner_radius,
    int thickness,
    const SDL_Color& color) {
    if (!renderer || rect.w <= 0 || rect.h <= 0 || thickness <= 0) {
        return;
    }

    const int effective_radius = effective_corner_radius(rect, corner_radius);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int layer = 0; layer < thickness; ++layer) {
        if (!draw_outline_layer(renderer, rect, effective_radius, layer)) {
            break;
        }
    }
}

void DrawRoundedFocusRing(
    SDL_Renderer* renderer,
    const SDL_Rect& rect,
    int corner_radius,
    int thickness,
    const SDL_Color& color) {
    DrawRoundedOutline(renderer, rect, corner_radius, thickness, color);
}

}

