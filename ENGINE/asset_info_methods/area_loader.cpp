#include "area_loader.hpp"

#include "asset/asset_info.hpp"
#include "utils/area.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

using nlohmann::json;

namespace {

inline float sanitize_scale(float scale) {
    if (!(scale > 0.0f) || !std::isfinite(scale)) {
        return 1.0f;
    }
    return scale;
}

inline int compute_scaled_dimension(int dimension, float factor) {
    if (dimension <= 0) return 0;
    double value = static_cast<double>(dimension) * static_cast<double>(factor);
    long long rounded = std::llround(value);
    if (rounded < 0) rounded = 0;
    if (rounded > static_cast<long long>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(rounded);
}

inline bool is_legacy_coord_system(const json& entry) {
    std::string coord = entry.value("coord_system", std::string{});
    if (coord.empty()) return true;
    std::transform(coord.begin(), coord.end(), coord.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return !(coord == "screen" || coord == "sl" || coord == "world");
}

} // namespace

AreaSerialization::Transform AreaLoader::build_transform(const AssetInfo& info,
                                                         int stored_orig_w,
                                                         int stored_orig_h,
                                                         float scale,
                                                         int offset_x,
                                                         int offset_y,
                                                         int json_offset_x,
                                                         int json_offset_y,
                                                         bool legacy_coords) {
    AreaSerialization::Transform transform;

    const float active_scale = sanitize_scale(scale);
    const int scaled_canvas_w = compute_scaled_dimension(info.original_canvas_width, active_scale);
    const int scaled_canvas_h = compute_scaled_dimension(info.original_canvas_height, active_scale);

    transform.default_offset_x = (offset_x != 0) ? offset_x : (scaled_canvas_w > 0 ? scaled_canvas_w / 2 : 0);
    transform.default_offset_y = (offset_y != 0) ? offset_y : scaled_canvas_h;

    if (stored_orig_w <= 0) stored_orig_w = info.original_canvas_width;
    if (stored_orig_h <= 0) stored_orig_h = info.original_canvas_height;
    if (stored_orig_w <= 0) stored_orig_w = 1;
    if (stored_orig_h <= 0) stored_orig_h = 1;

    double width_ratio = 1.0;
    double height_ratio = 1.0;
    if (info.original_canvas_width > 0) {
        width_ratio = static_cast<double>(info.original_canvas_width) / static_cast<double>(stored_orig_w);
    }
    if (info.original_canvas_height > 0) {
        height_ratio = static_cast<double>(info.original_canvas_height) / static_cast<double>(stored_orig_h);
    }

    transform.scale_x = static_cast<double>(active_scale) * width_ratio;
    transform.scale_y = static_cast<double>(active_scale) * height_ratio;
    transform.json_offset_x = json_offset_x;
    transform.json_offset_y = json_offset_y;
    transform.stored_orig_w = stored_orig_w;
    transform.stored_orig_h = stored_orig_h;
    transform.legacy_coords = legacy_coords;

    transform.base_x = transform.default_offset_x + json_offset_x;
    if (legacy_coords) {
        transform.base_y = transform.default_offset_y - json_offset_y;
    } else {
        transform.base_y = transform.default_offset_y + json_offset_y;
    }

    return transform;
}

AreaSerialization::Transform AreaLoader::make_transform(const AssetInfo& info,
                                                        const json* entry,
                                                        float scale,
                                                        int offset_x,
                                                        int offset_y) {
    int stored_orig_w = info.original_canvas_width;
    int stored_orig_h = info.original_canvas_height;
    int json_offset_x = 0;
    int json_offset_y = 0;
    bool legacy = false;

    if (entry) {
        if (entry->contains("original_dimensions") && (*entry)["original_dimensions"].is_array() && (*entry)["original_dimensions"].size() == 2) {
            stored_orig_w = (*entry)["original_dimensions"][0].get<int>();
            stored_orig_h = (*entry)["original_dimensions"][1].get<int>();
        }
        json_offset_x = entry->value("offset_x", 0);
        json_offset_y = entry->value("offset_y", 0);
        legacy = is_legacy_coord_system(*entry);
    }

    return build_transform(info,
                           stored_orig_w,
                           stored_orig_h,
                           scale,
                           offset_x,
                           offset_y,
                           json_offset_x,
                           json_offset_y,
                           legacy);
}

std::vector<Area::Point> AreaLoader::decode_points(const json& entry,
                                                   const AreaSerialization::Transform& transform) {
    std::vector<Area::Point> pts;
    if (!entry.contains("points") || !entry["points"].is_array()) {
        return pts;
    }

    pts.reserve(entry["points"].size());
    for (const auto& p : entry["points"]) {
        if (!p.is_array() || p.size() < 2) continue;
        double rel_x = p[0].get<double>();
        double rel_y = p[1].get<double>();
        int lx = static_cast<int>(std::llround(rel_x * transform.scale_x)) + transform.base_x;
        int ly = static_cast<int>(std::llround(rel_y * transform.scale_y)) + transform.base_y;
        pts.push_back({ lx, ly });
    }
    return pts;
}

nlohmann::json AreaLoader::encode_points(const std::vector<Area::Point>& points,
                                         const AreaSerialization::Transform& transform) {
    auto encode = [](double value) {
        double snapped = std::round(value * 1000.0) / 1000.0;
        if (std::abs(snapped) < 1e-6) snapped = 0.0;
        return snapped;
    };

    json pts = json::array();
    for (const auto& p : points) {
        double rel_x = 0.0;
        double rel_y = 0.0;
        if (transform.scale_x != 0.0) {
            rel_x = (static_cast<double>(p.x) - static_cast<double>(transform.base_x)) / transform.scale_x;
        }
        if (transform.scale_y != 0.0) {
            rel_y = (static_cast<double>(p.y) - static_cast<double>(transform.base_y)) / transform.scale_y;
        }
        pts.push_back({ encode(rel_x), encode(rel_y) });
    }
    return pts;
}

void AreaLoader::load(AssetInfo& info,
                      const json& data,
                      float scale,
                      int offset_x,
                      int offset_y) {
    info.areas.clear();
    if (!data.contains("areas") || !data["areas"].is_array()) return;

    const float active_scale = sanitize_scale(scale);

    for (const auto& entry : data["areas"]) {
        if (!entry.is_object()) continue;
        std::string name = entry.value("name", std::string{});
        std::string type = entry.value("type", std::string{});
        if (name.empty()) continue;

        auto transform = make_transform(info, &entry, active_scale, offset_x, offset_y);
        auto pts = decode_points(entry, transform);
        if (pts.empty()) continue;

        auto area = std::make_unique<Area>(name, pts);
        area->set_type(type);

        AssetInfo::NamedArea na;
        na.name = name;
        na.type = type;
        na.area = std::move(area);
        info.areas.push_back(std::move(na));
    }
}

