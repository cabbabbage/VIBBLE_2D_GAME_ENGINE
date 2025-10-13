#pragma once

#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace map_layers {

inline constexpr int kLayerRadiusStepDefault = 512;
inline constexpr double kLayerEdgeBuffer = 400.0;
inline constexpr double kMapRadiusOuterPadding = 800.0;

struct LayerRadiiResult {
    std::vector<double> layer_radii;
    std::vector<double> layer_extents;
    double map_radius = 0.0;
};

LayerRadiiResult compute_layer_radii(const nlohmann::json& layers,
                                      const nlohmann::json* rooms_data);

double room_extent_from_rooms_data(const nlohmann::json* rooms_data,
                                   const std::string& room_name);

double map_radius_from_map_info(const nlohmann::json& map_info);

}  // namespace map_layers

