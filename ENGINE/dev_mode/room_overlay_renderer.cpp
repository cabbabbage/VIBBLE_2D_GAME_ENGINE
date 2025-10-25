#include "room_overlay_renderer.hpp"

#include <array>
#include <cmath>
#include <algorithm>

#include "dm_styles.hpp"
#include "draw_utils.hpp"
#include "render/camera.hpp"

namespace {

int compute_center_arm(const camera& cam) {
    double scale = cam.get_scale();
    if (!std::isfinite(scale) || scale <= 0.0) {
        scale = 1.0;
    }
    double inv_scale = 1.0 / scale;
    int arm = static_cast<int>(std::lround(6.0 * inv_scale));
    arm = std::clamp(arm, 4, 24);
    return arm;
}

} // namespace

namespace dm_draw {

const RoomBoundsOverlayStyle& ResolveRoomBoundsOverlayStyle() {
    static const RoomBoundsOverlayStyle kStyle = [] {
        RoomBoundsOverlayStyle style{};
        SDL_Color accent = DMStyles::AccentButton().bg;
        SDL_Color outline = LightenColor(accent, 0.12f);
        outline.a = 210;
        SDL_Color fill = outline;
        fill.a = 48;
        SDL_Color center = DMStyles::AccentButton().hover_bg;
        center = LightenColor(center, 0.08f);
        center.a = 235;
        style.outline = outline;
        style.fill = fill;
        style.center = center;
        return style;
    }();
    return kStyle;
}

void RenderRoomBoundsOverlay(
    SDL_Renderer* renderer,
    const camera& cam,
    const std::tuple<int, int, int, int>& bounds,
    SDL_Point center,
    const RoomBoundsOverlayStyle& style) {
    if (!renderer) return;

    const auto [min_x, min_y, max_x, max_y] = bounds;
    const int width = max_x - min_x;
    const int height = max_y - min_y;

    SDL_BlendMode prev_mode = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(renderer, &prev_mode);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    Uint8 prev_r = 0, prev_g = 0, prev_b = 0, prev_a = 0;
    SDL_GetRenderDrawColor(renderer, &prev_r, &prev_g, &prev_b, &prev_a);

    if (width > 0 && height > 0) {
        std::array<SDL_Point, 4> world{{
            SDL_Point{min_x, min_y},
            SDL_Point{max_x, min_y},
            SDL_Point{max_x, max_y},
            SDL_Point{min_x, max_y},
        }};

        std::array<SDL_Point, 5> screen{};
        for (std::size_t i = 0; i < world.size(); ++i) {
            screen[i] = cam.map_to_screen(world[i]);
        }
        screen.back() = screen.front();

#if SDL_VERSION_ATLEAST(2,0,18)
        if (style.fill.a > 0) {
            std::array<SDL_Vertex, 4> verts{};
            for (std::size_t i = 0; i < world.size(); ++i) {
                const SDL_Point p = screen[i];
                SDL_Vertex v{};
                v.position = SDL_FPoint{static_cast<float>(p.x), static_cast<float>(p.y)};
                v.color = style.fill;
                verts[i] = v;
            }
            const int indices[6] = {0, 1, 2, 0, 2, 3};
            SDL_RenderGeometry(renderer, nullptr, verts.data(), static_cast<int>(verts.size()), indices, 6);
        }
#endif

        SDL_SetRenderDrawColor(renderer, style.outline.r, style.outline.g, style.outline.b, style.outline.a);
        SDL_RenderDrawLines(renderer, screen.data(), static_cast<int>(screen.size()));
    }

    SDL_Point center_screen = cam.map_to_screen(center);
    int arm = compute_center_arm(cam);
    SDL_SetRenderDrawColor(renderer, style.center.r, style.center.g, style.center.b, style.center.a);
    SDL_RenderDrawLine(renderer, center_screen.x - arm, center_screen.y, center_screen.x + arm, center_screen.y);
    SDL_RenderDrawLine(renderer, center_screen.x, center_screen.y - arm, center_screen.x, center_screen.y + arm);

    SDL_SetRenderDrawColor(renderer, prev_r, prev_g, prev_b, prev_a);
    SDL_SetRenderDrawBlendMode(renderer, prev_mode);
}

} // namespace dm_draw

