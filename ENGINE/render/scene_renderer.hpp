#pragma once

#include <string>
#include <memory>
#include <SDL.h>
#include "light_map.hpp"
#include "global_light_source.hpp"
#include "render_asset.hpp"
#include "utils/parallax.hpp"

class Assets;
class Asset;

class SceneRenderer {

	public:
    SceneRenderer(SDL_Renderer* renderer,
    Assets* assets,
    int screen_width,
    int screen_height,
    const std::string& map_path);
    void render();

	private:
    void update_shading_groups();
    bool shouldRegen(Asset* a);
    SDL_Rect get_scaled_position_rect(Asset* a,
    int fw,
    int fh,
    float smooth_inv_scale,
    int min_w,
    int min_h);
    std::string map_path_;
    SDL_Renderer* renderer_;
    Assets* assets_;
    int screen_width_;
    int screen_height_;
    Parallax parallax_;
    Global_Light_Source main_light_source_;
    SDL_Texture* fullscreen_light_tex_;
    SDL_Texture* accumulation_tex_;
    RenderAsset render_asset_;
    std::unique_ptr<LightMap> z_light_pass_;
    int current_shading_group_ = 0;
    int num_groups_ = 20;
    bool debugging = false;
    float smooth_inv_scale_ = 1.0f;
};
