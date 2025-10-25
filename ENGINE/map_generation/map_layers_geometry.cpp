#include "map_layers_geometry.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>

namespace map_layers {

namespace {

double clamp_min_edge(double value) {
    if (!std::isfinite(value)) {
        return static_cast<double>(kDefaultMinEdgeDistance);
    }
    if (value < 0.0) {
        return 0.0;
    }
    if (value > kMinEdgeDistanceMax) {
        return kMinEdgeDistanceMax;
    }
    return value;
}

double extract_dimension(const nlohmann::json& room, const char* key) {
    if (!room.is_object()) return 0.0;
    const auto it = room.find(key);
    if (it == room.end()) return 0.0;
    if (it->is_number_float() || it->is_number_integer()) {
        return it->get<double>();
    }
    return 0.0;
}

bool is_circle_geometry(std::string geometry_value) {
    if (geometry_value.empty()) return false;
    std::string lowered;
    lowered.reserve(geometry_value.size());
    for (char ch : geometry_value) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return lowered == "circle";
}

double sanitize_dimension(double value, double fallback) {
    if (value > 0.0) return value;
    return fallback;
}

}  // namespace

double room_extent_from_rooms_data(const nlohmann::json* rooms_data,
                                   const std::string& room_name) {
    if (!rooms_data || !rooms_data->is_object() || room_name.empty()) {
        return 0.0;
    }
    const auto room_it = rooms_data->find(room_name);
    if (room_it == rooms_data->end() || !room_it->is_object()) {
        return 0.0;
    }
    const auto& room = *room_it;

    double max_width = extract_dimension(room, "max_width");
    double max_height = extract_dimension(room, "max_height");
    const bool is_circle = is_circle_geometry(room.value("geometry", std::string()));

    double radius_value = 0.0;
    const auto radius_it = room.find("radius");
    if (radius_it != room.end() && (radius_it->is_number_float() || radius_it->is_number_integer())) {
        radius_value = std::max(0.0, radius_it->get<double>());
    }

    if (is_circle) {
        if (radius_value <= 0.0) {
            double diameter_guess = std::max(max_width, max_height);
            if (diameter_guess <= 0.0) {
                const double alt_w = extract_dimension(room, "min_width");
                const double alt_h = extract_dimension(room, "min_height");
                diameter_guess = std::max(alt_w, alt_h);
            }
            if (diameter_guess > 0.0) {
                radius_value = diameter_guess * 0.5;
            }
        }
        if (radius_value <= 0.0) {
            radius_value = std::max(max_width, max_height) * 0.5;
        }
        if (radius_value <= 0.0) {
            radius_value = 1.0;
        }
        return radius_value;
    }

    if (max_width <= 0.0 && max_height <= 0.0) {
        max_width = 100.0;
        max_height = 100.0;
    } else {
        max_width = sanitize_dimension(max_width, max_height);
        max_height = sanitize_dimension(max_height, max_width);
    }

    const double clamped_width = std::max(0.0, max_width);
    const double clamped_height = std::max(0.0, max_height);
    const double diagonal = std::sqrt(clamped_width * clamped_width + clamped_height * clamped_height);
    return diagonal * 0.5;
}

LayerRadiiResult compute_layer_radii(const nlohmann::json& layers,
                                      const nlohmann::json* rooms_data,
                                      double min_edge_distance) {
    LayerRadiiResult result;
    if (!layers.is_array() || layers.empty()) {
        result.map_radius = 0.0;
        return result;
    }

    const size_t layer_count = layers.size();
    result.layer_radii.assign(layer_count, 0.0);
    result.layer_extents.assign(layer_count, 0.0);

    const double sanitized_edge = clamp_min_edge(min_edge_distance);
    result.min_edge_distance = sanitized_edge;

    double max_extent = 0.0;
    double largest_extent = 0.0;

    for (size_t i = 0; i < layer_count; ++i) {
        const auto& layer = layers[i];
        if (!layer.is_object()) {
            continue;
        }

        double largest_room = 0.0;
        const auto rooms_it = layer.find("rooms");
        if (rooms_it != layer.end() && rooms_it->is_array()) {
            for (const auto& candidate : *rooms_it) {
                if (!candidate.is_object()) continue;
                const std::string room_name = candidate.value("name", std::string());
                largest_room = std::max(largest_room, room_extent_from_rooms_data(rooms_data, room_name));
            }
        }
        result.layer_extents[i] = largest_room;
        largest_extent = std::max(largest_extent, largest_room);
    }

    for (size_t i = 0; i < layer_count; ++i) {
        if (i == 0) {
            result.layer_radii[i] = 0.0;
            max_extent = std::max(max_extent, result.layer_extents[i]);
            continue;
        }

        const double prev_radius = result.layer_radii[i - 1];
        const double prev_extent = result.layer_extents[i - 1];
        const double current_extent = result.layer_extents[i];

        const double separation = prev_extent + current_extent + sanitized_edge;
        const double desired_radius = prev_radius + separation;
        int final_radius = static_cast<int>(std::ceil(std::max(0.0, desired_radius)));
        if (final_radius < 0) final_radius = 0;
        result.layer_radii[i] = static_cast<double>(final_radius);
        max_extent = std::max(max_extent, result.layer_radii[i] + current_extent);
    }

    if (max_extent <= 0.0) {
        max_extent = largest_extent;
    }
    if (max_extent <= 0.0) {
        max_extent = 1.0;
    }

    result.map_radius = max_extent + kMapRadiusOuterPadding;
    return result;
}

double map_radius_from_map_info(const nlohmann::json& map_info) {
    if (!map_info.is_object()) {
        return 0.0;
    }
    const auto layers_it = map_info.find("map_layers");
    if (layers_it == map_info.end()) {
        return 0.0;
    }
    const nlohmann::json* rooms_data_ptr = nullptr;
    const auto rooms_it = map_info.find("rooms_data");
    if (rooms_it != map_info.end() && rooms_it->is_object()) {
        rooms_data_ptr = &(*rooms_it);
    }
    const double min_edge = min_edge_distance_from_map_info(map_info);
    const LayerRadiiResult result = compute_layer_radii(*layers_it, rooms_data_ptr, min_edge);
    return result.map_radius;
}

double min_edge_distance_from_map_info(const nlohmann::json& map_info) {
    if (!map_info.is_object()) {
        return static_cast<double>(kDefaultMinEdgeDistance);
    }
    const auto settings_it = map_info.find("map_layers_settings");
    if (settings_it == map_info.end() || !settings_it->is_object()) {
        return static_cast<double>(kDefaultMinEdgeDistance);
    }
    const auto value_it = settings_it->find("min_edge_distance");
    if (value_it == settings_it->end()) {
        return static_cast<double>(kDefaultMinEdgeDistance);
    }
    if (value_it->is_number_integer() || value_it->is_number_float()) {
        return clamp_min_edge(value_it->get<double>());
    }
    return static_cast<double>(kDefaultMinEdgeDistance);
}

}  // namespace map_layers

