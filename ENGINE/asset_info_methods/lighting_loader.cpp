#include "lighting_loader.hpp"
#include "asset/asset_info.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <nlohmann/json.hpp>
using nlohmann::json;

void LightingLoader::load(AssetInfo& info, const json& data) {
        info.is_light_source = false;
        info.light_sources.clear();
        info.orbital_light_sources.clear();
        info.shading_factor = 100;
        if (!data.contains("lighting_info"))
        return;
        const auto& linfo = data["lighting_info"];
        struct ParsedLight {
                LightSource light;
                int factor_percent = 100;
};
        auto parse_light = [](const json& l) -> std::optional<ParsedLight> {
                if (!l.is_object() || !l.value("has_light_source", false))
                return std::nullopt;
                ParsedLight parsed;
                LightSource& light = parsed.light;

                auto clamp_int = [](int value, int min_value, int max_value) {
                        return std::clamp(value, min_value, max_value);
                };

                light.intensity = clamp_int(l.value("light_intensity", light.intensity), 1, 255);
                light.radius    = std::max(1, l.value("radius", light.radius));
                light.fall_off  = std::max(0, l.value("fall_off", light.fall_off));
                light.flare     = clamp_int(l.value("flare", light.flare), 0, 100);
                light.flicker   = std::max(0, l.value("flicker", light.flicker));
                int raw_offset_x = l.value("offset_x", light.offset_x);
                int raw_offset_y = l.value("offset_y", light.offset_y);
                int raw_x_radius = l.value("x_radius", light.x_radius);
                int raw_y_radius = l.value("y_radius", light.y_radius);
                parsed.factor_percent = clamp_int(l.value("factor", 100), 0, 100);
                light.apex_speed_bias = clamp_int(l.value("apex_speed_bias", light.apex_speed_bias), 0, 100);
                const double factor = static_cast<double>(parsed.factor_percent) / 100.0;
                light.offset_x  = static_cast<int>(std::lround(static_cast<double>(raw_offset_x) * factor));
                light.offset_y  = static_cast<int>(std::lround(static_cast<double>(raw_offset_y) * factor));
                light.x_radius  = static_cast<int>(std::lround(static_cast<double>(raw_x_radius) * factor));
                light.y_radius  = static_cast<int>(std::lround(static_cast<double>(raw_y_radius) * factor));
                light.color     = {255, 255, 255, 255};
                light.behind    = l.value("behind", false);
                return parsed;
};
        auto append_light = [&](const ParsedLight& parsed) {
                info.is_light_source = true;
                const LightSource& light = parsed.light;
                if (light.x_radius > 0 || light.y_radius > 0) {
                        if (info.orbital_light_sources.empty()) {
                                info.shading_factor = parsed.factor_percent;
                        }
                        info.orbital_light_sources.push_back(light);
                } else {
                        info.light_sources.push_back(light);
                }
};
        if (linfo.is_array()) {
                for (const auto& l : linfo) {
                        auto maybe = parse_light(l);
                        if (maybe.has_value()) {
                                append_light(*maybe);
                        }
                }
        } else if (linfo.is_object()) {
                auto maybe = parse_light(linfo);
                if (maybe.has_value()) {
                        append_light(*maybe);
                }
        }
}

