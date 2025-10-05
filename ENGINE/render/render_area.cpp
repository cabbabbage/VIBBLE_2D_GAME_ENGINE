#include "render_area.hpp"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include <cmath>

#include "asset/Asset.hpp"
#include "render/camera.hpp"

namespace {

std::string to_lower_copy(const std::string& text) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lower;
}

SDL_Color color_for_area(const std::string& name) {
    const std::string lower = to_lower_copy(name);
    if (lower.find("boundary") != std::string::npos) {
        return SDL_Color{255, 0, 0, 128};
    }
    if (lower.find("trigger") != std::string::npos) {
        return SDL_Color{0, 0, 255, 128};
    }
    if (lower.find("spacing") != std::string::npos) {
        return SDL_Color{0, 200, 0, 128};
    }
    if (lower.find("child") != std::string::npos) {
        return SDL_Color{255, 220, 0, 128};
    }
    return SDL_Color{255, 140, 0, 128};
}

void draw_outline(SDL_Renderer* renderer, const std::vector<SDL_Point>& points, SDL_Color color) {
    if (points.size() < 2) return;
    std::vector<SDL_Point> outline = points;
    outline.push_back(points.front());
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawLines(renderer, outline.data(), static_cast<int>(outline.size()));
}

#if SDL_VERSION_ATLEAST(2,0,18)
void fill_polygon(SDL_Renderer* renderer, const std::vector<SDL_Point>& points, SDL_Color color) {
    if (points.size() < 3) return;
    std::vector<SDL_Vertex> vertices;
    vertices.reserve(points.size());
    for (const SDL_Point& p : points) {
        SDL_Vertex v{};
        v.position = SDL_FPoint{static_cast<float>(p.x), static_cast<float>(p.y)};
        v.color = color;
        vertices.push_back(v);
    }

    std::vector<int> indices;
    indices.reserve((points.size() - 2) * 3);
    for (std::size_t i = 1; i + 1 < points.size(); ++i) {
        indices.push_back(0);
        indices.push_back(static_cast<int>(i));
        indices.push_back(static_cast<int>(i + 1));
    }

    if (!indices.empty()) {
        SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()), indices.data(), static_cast<int>(indices.size()));
    }
}
#else
void fill_polygon(SDL_Renderer* renderer, const std::vector<SDL_Point>& points, SDL_Color color) {
    (void)renderer;
    (void)points;
    (void)color;
    // SDL_RenderGeometry is not available; fall back to outline only.
}
#endif

} // namespace

void render_asset_debug_areas(SDL_Renderer* renderer,
                              const camera& cam,
                              Asset& asset,
                              float asset_screen_height,
                              float reference_screen_height) {
    (void)asset_screen_height;
    if (!renderer || !asset.info) {
        return;
    }

    if (asset.info->areas.empty()) {
        return;
    }

    SDL_BlendMode previous_blend_mode = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(renderer, &previous_blend_mode);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    Uint8 prev_r = 0, prev_g = 0, prev_b = 0, prev_a = 0;
    SDL_GetRenderDrawColor(renderer, &prev_r, &prev_g, &prev_b, &prev_a);

    const float camera_scale = cam.get_scale();
    if (camera_scale <= 0.0f) {
        SDL_SetRenderDrawColor(renderer, prev_r, prev_g, prev_b, prev_a);
        SDL_SetRenderDrawBlendMode(renderer, previous_blend_mode);
        return;
    }
    const float inv_scale = 1.0f / camera_scale;

    for (const auto& named_area : asset.info->areas) {
        if (!named_area.area) {
            continue;
        }

        const auto& local_points = named_area.area->get_points();
        if (local_points.size() < 3) {
            continue;
        }

        int fw = asset.cached_w;
        int fh = asset.cached_h;
        if ((fw == 0 || fh == 0) && asset.get_final_texture()) {
            SDL_QueryTexture(asset.get_final_texture(), nullptr, nullptr, &fw, &fh);
            asset.cached_w = fw;
            asset.cached_h = fh;
        }
        if (fw <= 0 || fh <= 0) {
            continue;
        }

        float base_sw = static_cast<float>(fw) * inv_scale;
        float base_sh = static_cast<float>(fh) * inv_scale;
        if (base_sw <= 0.0f || base_sh <= 0.0f) {
            continue;
        }

        float ref_height = reference_screen_height;
        if (ref_height <= 0.0f) {
            ref_height = 1.0f;
        }

        const camera::RenderEffects effects = cam.compute_render_effects(asset.pos, base_sh, ref_height);

        float scaled_sw = base_sw * effects.distance_scale;
        float scaled_sh = base_sh * effects.distance_scale;
        float final_visible_h = scaled_sh * effects.vertical_scale;

        int sw = std::max(1, static_cast<int>(std::lround(scaled_sw)));
        int sh = std::max(1, static_cast<int>(std::lround(final_visible_h)));
        if (sw <= 0 || sh <= 0) {
            continue;
        }

        const SDL_Point& anchor_screen = effects.screen_position;
        SDL_Rect dst{ anchor_screen.x - sw / 2, anchor_screen.y - sh, sw, sh };
        const float scale_x = static_cast<float>(dst.w) / static_cast<float>(fw);
        const float scale_y = static_cast<float>(dst.h) / static_cast<float>(fh);

        std::vector<SDL_Point> screen_points;
        screen_points.reserve(local_points.size());
        for (const auto& lp : local_points) {
            float local_x = static_cast<float>(lp.x);
            if (asset.flipped) {
                local_x = static_cast<float>(fw) - local_x;
            }
            float local_y = static_cast<float>(lp.y);
            float sx = static_cast<float>(dst.x) + local_x * scale_x;
            float sy = static_cast<float>(dst.y) + local_y * scale_y;
            screen_points.push_back(SDL_Point{ static_cast<int>(std::lround(sx)), static_cast<int>(std::lround(sy)) });
        }

        SDL_Color fill_color = color_for_area(named_area.name);
        SDL_Color outline_color = fill_color;
        outline_color.a = 255;

        fill_polygon(renderer, screen_points, fill_color);
        draw_outline(renderer, screen_points, outline_color);
    }

    SDL_SetRenderDrawColor(renderer, prev_r, prev_g, prev_b, prev_a);
    SDL_SetRenderDrawBlendMode(renderer, previous_blend_mode);
}
