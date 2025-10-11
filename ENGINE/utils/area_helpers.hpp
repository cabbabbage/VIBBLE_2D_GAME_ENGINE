#pragma once

#include <SDL.h>

#include <string>
#include <vector>

#include "utils/area.hpp"

class AssetInfo;

namespace area_helpers {

Area make_world_area(const AssetInfo& info, const Area&       local_area, SDL_Point         world_pos, bool              flipped);

Area make_world_area(const AssetInfo& info, const std::string& area_name, SDL_Point          world_pos, bool               flipped);

}

