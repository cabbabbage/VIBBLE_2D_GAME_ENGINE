#pragma once

#include <vector>

#include <SDL.h>

#include "stride_types.hpp"
#include "utils/grid.hpp"

class Asset;

class GetBestPath {
public:
    Plan operator()(const Asset& self, const std::vector<SDL_Point>& sanitized_checkpoints, int visited_thresh_px, const vibble::grid::Grid& grid) const;
};
