#include "area_loader.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include <cmath>
#include <limits>
#include <filesystem>
#include <iostream>
using nlohmann::json;

void AreaLoader::load(AssetInfo& info,
                      const json& data,
                      float scale,
                      int offset_x,
                      int offset_y) {
    info.areas.clear();
    if (!data.contains("areas") || !data["areas"].is_array()) return;

    const float active_scale = (scale <= 0.0f) ? 1.0f : scale;
    auto compute_scaled = [](int dimension, float factor) {
        double value = static_cast<double>(dimension) * static_cast<double>(factor);
        long long rounded = std::llround(value);
        if (rounded < 1) rounded = 1;
        if (rounded > static_cast<long long>(std::numeric_limits<int>::max())) {
            return std::numeric_limits<int>::max();
        }
        return static_cast<int>(rounded);
    };

    int default_offset_x = offset_x;
    int default_offset_y = offset_y;

    if (default_offset_x == 0 && info.original_canvas_width > 0) {
        default_offset_x = compute_scaled(info.original_canvas_width, active_scale) / 2;
    }
    if (default_offset_y == 0 && info.original_canvas_height > 0) {
        default_offset_y = compute_scaled(info.original_canvas_height, active_scale);
    }

    for (const auto& entry : data["areas"]) {
        if (!entry.is_object()) continue;
        std::string name = entry.value("name", std::string{});
        if (name.empty()) continue;

        int stored_orig_w = info.original_canvas_width;
        int stored_orig_h = info.original_canvas_height;
        if (entry.contains("original_dimensions") && entry["original_dimensions"].is_array() && entry["original_dimensions"].size() == 2) {
            stored_orig_w = entry["original_dimensions"][0].get<int>();
            stored_orig_h = entry["original_dimensions"][1].get<int>();
        }

        int json_offset_x = entry.value("offset_x", 0);
        int json_offset_y = entry.value("offset_y", 0);

        const int base_offset_x = default_offset_x + json_offset_x;
        const int base_offset_y = default_offset_y - json_offset_y;

        double width_ratio = 1.0;
        double height_ratio = 1.0;
        if (stored_orig_w > 0 && info.original_canvas_width > 0 && stored_orig_w != info.original_canvas_width) {
            width_ratio = static_cast<double>(info.original_canvas_width) / static_cast<double>(stored_orig_w);
        }
        if (stored_orig_h > 0 && info.original_canvas_height > 0 && stored_orig_h != info.original_canvas_height) {
            height_ratio = static_cast<double>(info.original_canvas_height) / static_cast<double>(stored_orig_h);
        }

        const double scale_x = static_cast<double>(active_scale) * width_ratio;
        const double scale_y = static_cast<double>(active_scale) * height_ratio;

        std::vector<Area::Point> pts;
        if (entry.contains("points") && entry["points"].is_array()) {
            pts.reserve(entry["points"].size());
            for (const auto& p : entry["points"]) {
                if (!p.is_array() || p.size() < 2) continue;
                double rel_x = p[0].get<double>();
                double rel_y = p[1].get<double>();
                int lx = static_cast<int>(std::llround(rel_x * scale_x)) + base_offset_x;
                int ly = static_cast<int>(std::llround(rel_y * scale_y)) + base_offset_y;
                pts.push_back({ lx, ly });
            }
        }

        if (pts.empty()) continue;
        auto area = std::make_unique<Area>(name, pts);
        info.areas.push_back({ name, std::move(area) });
    }
}

