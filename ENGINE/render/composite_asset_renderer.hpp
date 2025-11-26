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

    // Updates the composite render package for the given asset if needed.
    void update(Asset* asset, const world::GridPoint* gp, float desired_scale = 1.0f);

private:
    void regenerate_package(Asset* asset, const world::GridPoint* gp, float desired_scale);
    void calculate_local_bounds(Asset* asset);

    SDL_Renderer* renderer_;
    Assets* assets_;
};
