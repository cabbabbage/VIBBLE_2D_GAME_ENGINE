#pragma once

#include <vector>

#include <SDL.h>

#include "stride_types.hpp"

class Asset;

class PathSanitizer {
public:
    std::vector<axis::WorldPos> sanitize(const Asset& self, const std::vector<axis::WorldPos>& absolute_checkpoints, int visited_thresh_px) const;
};
