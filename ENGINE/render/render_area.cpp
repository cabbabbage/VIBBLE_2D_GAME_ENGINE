#include "render_area.hpp"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

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
                              const Asset& asset,
                              float asset_screen_height,
                              float reference_screen_height) {
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

    for (const auto& named_area : asset.info->areas) {
        if (!named_area.area) {
            continue;
        }

        Area world_area = asset.get_area(named_area.name);
        const auto& world_points = world_area.get_points();
        if (world_points.size() < 3) {
            continue;
        }

        std::vector<SDL_Point> screen_points;
        screen_points.reserve(world_points.size());
        for (const auto& pt : world_points) {
            camera::RenderEffects effects = cam.compute_render_effects(pt, asset_screen_height, reference_screen_height);
            screen_points.push_back(effects.screen_position);
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
