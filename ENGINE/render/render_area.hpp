#pragma once

struct SDL_Renderer;

class Asset;
class camera;

void render_asset_debug_areas(SDL_Renderer* renderer,
                              const camera& cam,
                              const Asset& asset,
                              float asset_screen_height,
                              float reference_screen_height);
