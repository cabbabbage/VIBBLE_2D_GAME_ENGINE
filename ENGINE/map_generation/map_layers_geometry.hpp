#pragma once

#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace map_layers {

inline constexpr int kLayerRadiusStepDefault = 512;
inline constexpr double kLayerEdgeBuffer = 400.0;
inline constexpr double kMapRadiusOuterPadding = 800.0;
inline constexpr int kDefaultMinEdgeDistance = 200;
inline constexpr double kMinEdgeDistanceMax = 10000.0;

struct LayerRadiiResult {
    std::vector<double> layer_radii;
    std::vector<double> layer_extents;
    double map_radius = 0.0;
    double min_edge_distance = static_cast<double>(kDefaultMinEdgeDistance);
};

struct RadialLayout {
    double radius = 0.0;
    std::vector<double> angles;
};

LayerRadiiResult compute_layer_radii(const nlohmann::json& layers, const nlohmann::json* rooms_data, double min_edge_distance = static_cast<double>(kDefaultMinEdgeDistance));

double room_extent_from_rooms_data(const nlohmann::json* rooms_data, const std::string& room_name);

double map_radius_from_map_info(const nlohmann::json& map_info);

double min_edge_distance_from_map_manifest(const nlohmann::json& map_manifest);

RadialLayout compute_radial_layout(double base_radius, const std::vector<double>& extents, double min_edge_distance, double start_angle);

}
