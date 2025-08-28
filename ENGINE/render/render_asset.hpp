
#pragma once

#include <SDL.h>
#include <string>

class Asset;
class Global_Light_Source;
class Parallax;

class RenderAsset {
public:
    RenderAsset(SDL_Renderer* renderer,
                Parallax& parallax,
                Global_Light_Source& main_light,
                Asset* player);

    
    
    SDL_Texture* regenerateFinalTexture(Asset* a);

private:
    Asset* p;  
    SDL_Texture* render_shadow_mask(Asset* a, int bw, int bh);
    void render_shadow_moving_lights(Asset* a, const SDL_Rect& bounds, Uint8 alpha);
    void render_shadow_orbital_lights(Asset* a, const SDL_Rect& bounds, Uint8 alpha);
    void render_shadow_received_static_lights(Asset* a, const SDL_Rect& bounds, Uint8 alpha);

private:
    SDL_Renderer* renderer_;
    Parallax& parallax_;
    Global_Light_Source& main_light_source_;
};
