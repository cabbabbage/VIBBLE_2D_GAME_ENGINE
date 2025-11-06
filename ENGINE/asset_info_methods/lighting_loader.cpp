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
                try {
                    if (l.contains("light_color") && l["light_color"].is_array()) {
                        const auto& arr = l["light_color"];
                        if (arr.size() >= 3) {
                            int r = std::clamp(arr.at(0).get<int>(), 0, 255);
                            int g = std::clamp(arr.at(1).get<int>(), 0, 255);
                            int b = std::clamp(arr.at(2).get<int>(), 0, 255);
                            light.color = SDL_Color{ static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), 255 };
                        }
                    }
                } catch (...) {
                }
                light.behind       = l.value("behind", false);
                // If explicit front flag present, use it; otherwise default to !behind for backwards-compat
                if (l.contains("front")) {
                    try {
                        light.in_front = l.value("front", true);
                    } catch (...) {
                        light.in_front = true;
                    }
                } else {
                    light.in_front = !light.behind;
                }
                // Legacy support: if older data disabled render_texture entirely, respect it by disabling both placements.
                if (l.contains("render_texture")) {
                    bool legacy_render_texture = true;
                    try {
                        legacy_render_texture = l.value("render_texture", true);
                    } catch (...) {
                        legacy_render_texture = true;
                    }
                    if (!legacy_render_texture) {
                        light.in_front = false;
                        light.behind   = false;
                    }
                }
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

