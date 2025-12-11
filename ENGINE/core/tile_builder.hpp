#pragma once

#include <vector>
#include <memory>
#include <SDL.h>

struct MapGridSettings;
class Asset;
namespace world { class WorldGrid; }
namespace world { class WorldGrid; }

namespace loader_tiles {

void build_grid_tiles(SDL_Renderer* renderer, world::WorldGrid& grid, const MapGridSettings& settings, const std::vector<Asset*>& all_assets);

}

