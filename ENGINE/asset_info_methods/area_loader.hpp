#pragma once

#include <nlohmann/json.hpp>
#include <vector>

#include "utils/area.hpp"

class AssetInfo;

#include <SDL.h>

namespace AreaSerialization {

struct LocalPoint {
    int x = 0;
    int y = 0;
};

std::vector<LocalPoint> read_local_points(const nlohmann::json& entry);

} // namespace AreaSerialization

class AreaLoader {

        public:
    static SDL_Point asset_anchor(const AssetInfo& info);

    static std::vector<Area::Point> decode_points(const nlohmann::json& entry,
                                                  SDL_Point anchor);

    static nlohmann::json encode_points(const std::vector<Area::Point>& points,
                                        SDL_Point anchor);

    static void load(AssetInfo& info, const nlohmann::json& data);
};
