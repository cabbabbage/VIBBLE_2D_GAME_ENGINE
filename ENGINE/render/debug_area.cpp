#include "debug_area.hpp"

AreaDebugRenderer::AreaDebugRenderer(SDL_Renderer* renderer, Parallax& parallax)
    : renderer_(renderer), parallax_(parallax) {}

void AreaDebugRenderer::setTestAreas(const std::vector<std::string>& areas) {
    test_areas_ = areas;
}

void AreaDebugRenderer::render(const AssetInfo* info, int world_x, int world_y) {
    if (!info) return;

    for (const std::string& key : test_areas_) {
        const Area* area = nullptr;
        SDL_Color color = {255, 255, 255, 80};

        if (key == "spacing" && info->has_spacing_area && info->spacing_area) {
            area = info->spacing_area.get();
            color = {0, 255, 0, 80};
        } else if (key == "pass" && !info->passable && info->passability_area) {
            area = info->passability_area.get();
            color = {255, 255, 0, 80};
        } else if (key == "collision" && info->has_collision_area && info->collision_area) {
            area = info->collision_area.get();
            color = {255, 0, 255, 80};
        } else if (key == "interaction" && info->has_interaction_area && info->interaction_area) {
            area = info->interaction_area.get();
            color = {0, 255, 255, 80};
        } else if (key == "attack" && info->has_attack_area && info->attack_area) {
            area = info->attack_area.get();
            color = {255, 255, 0, 80};
        }

        if (!area) continue;

        SDL_Texture* tex = area->get_texture();
        if (!tex) continue;

        auto [minx, miny, maxx, maxy] = area->get_bounds();
        int w = maxx - minx + 1;
        int h = maxy - miny + 1;

        
        SDL_Point screen_pos = parallax_.apply(world_x, world_y);
        SDL_Rect dst{ screen_pos.x - w / 2, screen_pos.y - h, w, h };

        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureColorMod(tex, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(tex, color.a);
        SDL_RenderCopy(renderer_, tex, nullptr, &dst);
    }
}
