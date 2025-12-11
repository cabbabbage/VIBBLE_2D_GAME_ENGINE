#pragma once

#include <SDL.h>
#include <cmath>

namespace dm_draw {

inline void stamp_circle(SDL_Surface* surf, int cx, int cy, int r, Uint32 color) {
    if (!surf) return;
    SDL_LockSurface(surf);
    Uint8* pixels = static_cast<Uint8*>(surf->pixels);
    const int pitch = surf->pitch;
    const int w = surf->w;
    const int h = surf->h;
    for (int y = -r; y <= r; ++y) {
        int yy = cy + y;
        if (yy < 0 || yy >= h) continue;
        int xr = static_cast<int>(std::sqrt(static_cast<double>(r * r - y * y)));
        for (int x = -xr; x <= xr; ++x) {
            int xx = cx + x;
            if (xx < 0 || xx >= w) continue;
            Uint32* p = reinterpret_cast<Uint32*>(pixels + yy * pitch) + xx;
            *p = color;
        }
    }
    SDL_UnlockSurface(surf);
}

SDL_Color LightenColor(const SDL_Color& color, float amount);
SDL_Color DarkenColor(const SDL_Color& color, float amount);

void DrawRoundedSolidRect(SDL_Renderer* renderer, const SDL_Rect& rect, int corner_radius, const SDL_Color& color);

void DrawRoundedGradientRect(SDL_Renderer* renderer, const SDL_Rect& rect, int corner_radius, const SDL_Color& top_color, const SDL_Color& bottom_color);

void DrawBeveledRect( SDL_Renderer* renderer, const SDL_Rect& rect, int corner_radius, int bevel_depth, const SDL_Color& fill, const SDL_Color& highlight, const SDL_Color& shadow, bool draw_outline = true, float highlight_intensity = 0.35f, float shadow_intensity = 0.35f);

void DrawRoundedOutline( SDL_Renderer* renderer, const SDL_Rect& rect, int corner_radius, int thickness, const SDL_Color& color);

void DrawRoundedFocusRing( SDL_Renderer* renderer, const SDL_Rect& rect, int corner_radius, int thickness, const SDL_Color& color);

}
