#include "map_layers_geometry.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>

namespace map_layers {

namespace {

constexpr double kTau = 6.28318530717958647692;

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

double sanitize_extent(double value) {
    if (!std::isfinite(value) || value <= 0.0) {
        return 1.0;
    }
    return value;
}

double minimal_radius_requirement(const std::vector<double>& extents, double min_edge) {
    if (extents.empty()) {
        return 0.0;
    }
    if (extents.size() == 1) {
        return sanitize_extent(extents.front()) + std::max(0.0, min_edge) * 0.5;
    }
    double minimum = 0.0;
    const std::size_t count = extents.size();
    for (std::size_t i = 0; i < count; ++i) {
        const double current = sanitize_extent(extents[i]);
        const double next = sanitize_extent(extents[(i + 1) % count]);
        const double required = (current + next + std::max(0.0, min_edge)) * 0.5;
        minimum = std::max(minimum, required);
    }
    return minimum;
}

double total_required_angle(double radius, const std::vector<double>& extents, double min_edge) {
    if (extents.size() <= 1) {
        return 0.0;
    }
    if (!(radius > 0.0) || !std::isfinite(radius)) {
        return std::numeric_limits<double>::infinity();
    }
    const std::size_t count = extents.size();
    const double edge = std::max(0.0, min_edge);
    double total = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double current = sanitize_extent(extents[i]);
        const double next = sanitize_extent(extents[(i + 1) % count]);
        const double chord = current + next + edge;
        if (chord <= 0.0) {
            continue;
        }
        const double ratio = chord / (2.0 * radius);
        if (ratio >= 1.0) {
            return std::numeric_limits<double>::infinity();
        }
        total += 2.0 * std::asin(std::clamp(ratio, -1.0, 1.0));
    }
    return total;
}

double ensure_radius_for_extents(double base_radius,
                                 const std::vector<double>& extents,
                                 double min_edge) {
    if (extents.empty()) {
        return std::max(0.0, base_radius);
    }
    const double edge = clamp_min_edge(min_edge);
    double radius = std::max(base_radius, minimal_radius_requirement(extents, edge));
    if (!(radius > 0.0) || !std::isfinite(radius)) {
        radius = minimal_radius_requirement(extents, edge);
        if (!(radius > 0.0) || !std::isfinite(radius)) {
            radius = 1.0;
        }
    }

    for (int iter = 0; iter < 32; ++iter) {
        const double required = total_required_angle(radius, extents, edge);
        if (!std::isfinite(required)) {
            radius = std::max(radius * 1.25, minimal_radius_requirement(extents, edge) + edge);
            continue;
        }
        if (required <= kTau) {
            break;
        }
        const double scale = std::max(1.01, required / kTau);
        radius *= scale;
    }
    return radius;
}

std::vector<double> normalize_angles(const std::vector<double>& raw_angles) {
    if (raw_angles.empty()) {
        return {};
    }
    std::vector<double> adjusted;
    adjusted.reserve(raw_angles.size());
    double offset = std::floor(raw_angles.front() / kTau) * kTau;
    double previous = 0.0;
    for (std::size_t i = 0; i < raw_angles.size(); ++i) {
        double angle = raw_angles[i] - offset;
        while (angle < 0.0) {
            angle += kTau;
        }
        if (i == 0) {
            previous = angle;
            adjusted.push_back(angle);
            continue;
        }
        while (angle <= previous) {
            angle += kTau;
        }
        previous = angle;
        adjusted.push_back(angle);
    }
    return adjusted;
}

}

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
    std::vector<std::vector<double>> layer_room_extents(layer_count);

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
        std::vector<double> extents_list;
        const int max_rooms_setting = layer.value("max_rooms", 0);
        const auto rooms_it = layer.find("rooms");
        if (rooms_it != layer.end() && rooms_it->is_array()) {
            for (const auto& candidate : *rooms_it) {
                if (!candidate.is_object()) continue;
                const std::string room_name = candidate.value("name", std::string());
                double extent_value = room_extent_from_rooms_data(rooms_data, room_name);
                extent_value = sanitize_extent(extent_value);
                largest_room = std::max(largest_room, extent_value);
                const int max_instances = std::max(0, candidate.value("max_instances", 0));
                for (int inst = 0; inst < max_instances; ++inst) {
                    extents_list.push_back(extent_value);
                }
            }
        }
        if (max_rooms_setting > 0 && extents_list.size() > static_cast<std::size_t>(max_rooms_setting)) {
            std::sort(extents_list.begin(), extents_list.end(), std::greater<double>());
            extents_list.resize(static_cast<std::size_t>(max_rooms_setting));
        }
        extents_list.erase(
            std::remove_if(extents_list.begin(), extents_list.end(), [](double v) { return !(v > 0.0); }),
            extents_list.end());
        if (extents_list.empty() && largest_room > 0.0) {
            extents_list.push_back(largest_room);
        }
        layer_room_extents[i] = std::move(extents_list);
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
        const double desired_radius = std::max(0.0, prev_radius + separation);
        double final_radius = std::ceil(desired_radius);
        const auto& same_layer_extents = layer_room_extents[i];
        if (!same_layer_extents.empty()) {
            final_radius = ensure_radius_for_extents(final_radius, same_layer_extents, sanitized_edge);
        }
        result.layer_radii[i] = final_radius;
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
    const double min_edge = min_edge_distance_from_map_manifest(map_info);
    const LayerRadiiResult result = compute_layer_radii(*layers_it, rooms_data_ptr, min_edge);
    return result.map_radius;
}

double min_edge_distance_from_map_manifest(const nlohmann::json& map_manifest) {
    if (!map_manifest.is_object()) {
        return static_cast<double>(kDefaultMinEdgeDistance);
    }
    const auto settings_it = map_manifest.find("map_layers_settings");
    if (settings_it == map_manifest.end() || !settings_it->is_object()) {
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

RadialLayout compute_radial_layout(double base_radius,
                                   const std::vector<double>& extents,
                                   double min_edge_distance,
                                   double start_angle) {
    RadialLayout layout;
    const double sanitized_edge = clamp_min_edge(min_edge_distance);
    layout.radius = ensure_radius_for_extents(std::max(0.0, base_radius), extents, sanitized_edge);
    if (extents.empty()) {
        return layout;
    }

    const std::size_t count = extents.size();
    if (count == 1) {
        layout.angles = normalize_angles({start_angle});
        return layout;
    }

    double total_required = total_required_angle(layout.radius, extents, sanitized_edge);
    if (!std::isfinite(total_required)) {
        total_required = kTau;
    }
    const double slack = std::max(0.0, kTau - total_required);
    const double extra = (count > 0) ? slack / static_cast<double>(count) : 0.0;

    std::vector<double> raw_angles;
    raw_angles.reserve(count);
    double current = start_angle;
    for (std::size_t i = 0; i < count; ++i) {
        raw_angles.push_back(current);
        const double current_extent = sanitize_extent(extents[i]);
        const double next_extent = sanitize_extent(extents[(i + 1) % count]);
        const double chord = current_extent + next_extent + sanitized_edge;
        double delta = 0.0;
        if (chord > 0.0 && layout.radius > 0.0) {
            const double ratio = std::clamp(chord / (2.0 * layout.radius), -1.0, 1.0);
            delta = 2.0 * std::asin(ratio);
        }
        current += delta + extra;
    }
    layout.angles = normalize_angles(raw_angles);
    return layout;
}

}
