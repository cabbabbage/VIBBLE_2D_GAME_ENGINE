#pragma once

#include <vector>
#include <memory>
#include <SDL.h>

struct MapGridSettings;
class Asset;
namespace world { class WorldGrid; }

// TileBuilder composes per-grid-cell textures from tileable assets and
// stores them onto the owning world chunks. This runs during loading.
namespace loader_tiles {

void build_grid_tiles(SDL_Renderer* renderer,
                      world::WorldGrid& grid,
                      const MapGridSettings& settings,
                      const std::vector<Asset*>& all_assets);

}

