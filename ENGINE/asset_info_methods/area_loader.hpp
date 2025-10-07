#pragma once

#include <nlohmann/json.hpp>
#include <vector>

#include "utils/area.hpp"

class AssetInfo;

namespace AreaSerialization {

struct Transform {
    double scale_x = 1.0;
    double scale_y = 1.0;
    int    default_offset_x = 0;
    int    default_offset_y = 0;
    int    json_offset_x    = 0;
    int    json_offset_y    = 0;
    int    base_x           = 0;
    int    base_y           = 0;
    int    stored_orig_w    = 0;
    int    stored_orig_h    = 0;
    bool   legacy_coords    = false;
};

} // namespace AreaSerialization

class AreaLoader {

        public:
    static AreaSerialization::Transform make_transform(const AssetInfo& info,
                                                       const nlohmann::json* entry,
                                                       float scale,
                                                       int offset_x,
                                                       int offset_y);

    static AreaSerialization::Transform build_transform(const AssetInfo& info,
                                                        int stored_orig_w,
                                                        int stored_orig_h,
                                                        float scale,
                                                        int offset_x,
                                                        int offset_y,
                                                        int json_offset_x,
                                                        int json_offset_y,
                                                        bool legacy_coords);

    static std::vector<Area::Point> decode_points(const nlohmann::json& entry,
                                                  const AreaSerialization::Transform& transform);

    static nlohmann::json encode_points(const std::vector<Area::Point>& points,
                                        const AreaSerialization::Transform& transform);

    static void load(AssetInfo& info, const nlohmann::json& data, float scale, int offset_x, int offset_y);
};
