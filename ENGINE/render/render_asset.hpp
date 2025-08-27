// === File: render_asset.hpp ===
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

    // Creates/loads a single combined texture for an asset (base sprite + lighting/shadows).
    // Returns a newly created texture owned by the caller (caller should assign into Asset).
    SDL_Texture* regenerateFinalTexture(Asset* a);

private:
    Asset* p;  // usually the player reference
    SDL_Texture* render_shadow_mask(Asset* a, int bw, int bh);
    void render_shadow_moving_lights(Asset* a, const SDL_Rect& bounds, Uint8 alpha);
    void render_shadow_orbital_lights(Asset* a, const SDL_Rect& bounds, Uint8 alpha);
    void render_shadow_received_static_lights(Asset* a, const SDL_Rect& bounds, Uint8 alpha);

private:
    SDL_Renderer* renderer_;
    Parallax& parallax_;
    Global_Light_Source& main_light_source_;
};
