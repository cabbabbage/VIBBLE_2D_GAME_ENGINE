#pragma once

#include <SDL.h>
#include <vector>

namespace world {
struct GridPoint;
}

class Asset;
class Assets;

class CompositeAssetRenderer {
public:
    CompositeAssetRenderer(SDL_Renderer* renderer, Assets* assets);
    ~CompositeAssetRenderer();

    void update(Asset* asset, const world::GridPoint* gp, float flicker_time_seconds = 0.0f);

private:
    void regenerate_package(Asset* asset, const world::GridPoint* gp, float flicker_time_seconds, float package_scale, float perspective_scale);
    void calculate_local_bounds(Asset* asset);

    SDL_Renderer* renderer_;
    Assets* assets_;
};
