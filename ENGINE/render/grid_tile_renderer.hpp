#pragma once

#include <SDL.h>
#include <vector>

class Assets;
class camera;
namespace world { class Chunk; }
namespace world { class Grid; }

// Renders prebuilt per-grid tiles with parallax applied. Intended to be
// invoked at the start of SceneRenderer::render().
class GridTileRenderer {
public:
    explicit GridTileRenderer(Assets* assets) : assets_(assets) {}

    // Render using Assets-owned camera and grid.
    void render(SDL_Renderer* renderer);

    // Render using explicit camera and grid references.
    void render(SDL_Renderer* renderer, const camera& cam, const world::Grid& grid);

private:
    Assets* assets_ = nullptr;
};
