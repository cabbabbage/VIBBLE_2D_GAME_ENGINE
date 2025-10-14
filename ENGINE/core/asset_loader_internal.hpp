#pragma once

#include <vector>
#include <SDL.h>

#include "utils/area.hpp"

namespace asset_loader_internal {

struct ZoneCacheEntry {
    const Area* area = nullptr;
    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;
    const std::vector<Area::Point>* points = nullptr;
};

std::vector<ZoneCacheEntry> build_zone_cache(const std::vector<const Area*>& zones);

bool point_inside_any_zone(const SDL_Point& point, const std::vector<ZoneCacheEntry>& cache);

double min_distance_sq_to_zones(const SDL_Point& point, const std::vector<ZoneCacheEntry>& cache, int remove_threshold);

}
