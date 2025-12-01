#pragma once

#include <functional>
#include <random>
#include <vector>

#include <SDL.h>

#include "utils/grid.hpp"

struct SpawnInfo;
class Area;
class SpawnContext;

class EdgeSpawner {
public:
    struct PlacementContext {
        std::mt19937& rng;
        vibble::grid::Grid& grid;
        int resolution = 0;
        SDL_Point center{0, 0};
        std::function<bool(SDL_Point)> overlaps_trail;
};

    std::vector<SDL_Point> plan_positions(const SpawnInfo& item, const Area& area, PlacementContext& placement) const;

    void spawn(const SpawnInfo& item, const Area* area, SpawnContext& ctx);
};

