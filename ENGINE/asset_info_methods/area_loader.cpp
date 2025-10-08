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

} // namespace

std::vector<AreaSerialization::LocalPoint>
AreaSerialization::read_local_points(const json& entry) {
    std::vector<LocalPoint> locals;
    if (!entry.contains("points") || !entry["points"].is_array()) {
        return locals;
    }
    locals.reserve(entry["points"].size());
    for (const auto& point : entry["points"]) {
        if (!point.is_object()) continue;
        LocalPoint lp;
        lp.x = point.value("x", 0);
        lp.y = point.value("y", 0);
        locals.push_back(lp);
    }
    return locals;
}

SDL_Point AreaLoader::asset_anchor(const AssetInfo& info) {
    const float scale = sanitize_scale(info.scale_factor);
    const int scaled_w = compute_scaled_dimension(info.original_canvas_width, scale);
    const int scaled_h = compute_scaled_dimension(info.original_canvas_height, scale);
    SDL_Point anchor{0, 0};
    anchor.x = (scaled_w > 0) ? scaled_w / 2 : 0;
    anchor.y = scaled_h;
    return anchor;
}

std::vector<Area::Point> AreaLoader::decode_points(const json& entry,
                                                   SDL_Point anchor) {
    std::vector<Area::Point> pts;
    auto locals = AreaSerialization::read_local_points(entry);
    pts.reserve(locals.size());
    for (const auto& local : locals) {
        pts.push_back({ anchor.x + local.x, anchor.y + local.y });
    }
    return pts;
}

nlohmann::json AreaLoader::encode_points(const std::vector<Area::Point>& points,
                                         SDL_Point anchor) {
    json pts = json::array();
    pts.get_ref<json::array_t&>().reserve(points.size());
    for (const auto& p : points) {
        pts.push_back({ {"x", p.x - anchor.x}, {"y", p.y - anchor.y} });
    }
    return pts;
}

void AreaLoader::load(AssetInfo& info, const json& data) {
    info.areas.clear();
    if (!data.contains("areas") || !data["areas"].is_array()) return;

    const SDL_Point anchor = asset_anchor(info);

    for (const auto& entry : data["areas"]) {
        if (!entry.is_object()) continue;
        std::string name = entry.value("name", std::string{});
        std::string type = entry.value("type", std::string{});
        std::string kind = entry.value("kind", std::string{});
        if (name.empty()) continue;

        auto pts = decode_points(entry, anchor);
        if (pts.size() < 3) continue;

        auto area = std::make_unique<Area>(name, pts);
        area->set_type(type);

        AssetInfo::NamedArea na;
        na.name = name;
        na.type = type;
        na.kind = kind;
        na.area = std::move(area);
        info.areas.push_back(std::move(na));
    }
}

