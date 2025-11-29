#include "room_overlay_renderer.hpp"

#include <cmath>
#include <algorithm>
#include <vector>

#include "draw_utils.hpp"
#include "utils/area.hpp"
#include "render/warped_screen_grid.hpp"

namespace {

int compute_center_arm(const WarpedScreenGrid& cam) {
    double scale = cam.get_scale();
    if (!std::isfinite(scale) || scale <= 0.0) {
        scale = 1.0;
    }
    double inv_scale = 1.0 / scale;
    int arm = static_cast<int>(std::lround(6.0 * inv_scale));
    arm = std::clamp(arm, 4, 24);
    return arm;
}

}

namespace dm_draw {

RoomBoundsOverlayStyle ResolveRoomBoundsOverlayStyle(SDL_Color base_color) {
    RoomBoundsOverlayStyle style{};
    base_color.a = 255;
    SDL_Color outline = LightenColor(base_color, 0.12f);
    outline.a = 210;
    SDL_Color fill = LightenColor(base_color, 0.02f);
    fill.a = 56;
    SDL_Color center = LightenColor(base_color, 0.2f);
    center.a = 235;
    style.outline = outline;
    style.fill = fill;
    style.center = center;
    return style;
}

void RenderRoomBoundsOverlay(
    SDL_Renderer* renderer,
    const WarpedScreenGrid& cam,
    const Area& area,
    const RoomBoundsOverlayStyle& style) {
    if (!renderer) return;

    SDL_BlendMode prev_mode = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(renderer, &prev_mode);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    Uint8 prev_r = 0, prev_g = 0, prev_b = 0, prev_a = 0;
    SDL_GetRenderDrawColor(renderer, &prev_r, &prev_g, &prev_b, &prev_a);

    const auto& area_points = area.get_points();
    if (!area_points.empty()) {
        std::vector<SDL_Point> screen_points;
        screen_points.reserve(area_points.size() + 1);
        for (const SDL_Point& world_point : area_points) {
            SDL_FPoint screen_f = cam.map_to_screen(world_point);
            screen_points.push_back(SDL_Point{static_cast<int>(std::lround(screen_f.x)),
                                              static_cast<int>(std::lround(screen_f.y))});
        }
        if (screen_points.size() >= 2) {
            const SDL_Point& first = screen_points.front();
            const SDL_Point& last  = screen_points.back();
            if (first.x != last.x || first.y != last.y) {
                screen_points.push_back(first);
            }
            SDL_SetRenderDrawColor(renderer, style.outline.r, style.outline.g, style.outline.b, style.outline.a);
            SDL_RenderDrawLines(renderer, screen_points.data(), static_cast<int>(screen_points.size()));
        }
    }

    SDL_FPoint center_screen_f = cam.map_to_screen(area.get_center());
    SDL_Point center_screen{static_cast<int>(std::lround(center_screen_f.x)),
                            static_cast<int>(std::lround(center_screen_f.y))};
    int arm = compute_center_arm(cam);
    SDL_SetRenderDrawColor(renderer, style.center.r, style.center.g, style.center.b, style.center.a);
    SDL_RenderDrawLine(renderer, center_screen.x - arm, center_screen.y, center_screen.x + arm, center_screen.y);
    SDL_RenderDrawLine(renderer, center_screen.x, center_screen.y - arm, center_screen.x, center_screen.y + arm);

    SDL_SetRenderDrawColor(renderer, prev_r, prev_g, prev_b, prev_a);
    SDL_SetRenderDrawBlendMode(renderer, prev_mode);
}

}

