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
        if (!data.contains("lighting_info"))
        return;
        const auto& linfo = data["lighting_info"];
        auto parse_light = [](const json& l) -> std::optional<LightSource> {
                if (!l.is_object() || !l.value("has_light_source", false))
                return std::nullopt;
                LightSource light;

                auto clamp_int = [](int value, int min_value, int max_value) {
                        return std::clamp(value, min_value, max_value);
};

                light.intensity = clamp_int(l.value("light_intensity", light.intensity), 1, 255);
                light.radius    = std::max(1, l.value("radius", light.radius));
                light.fall_off  = std::max(0, l.value("fall_off", light.fall_off));
                light.flare     = clamp_int(l.value("flare", light.flare), 0, 100);
                light.flicker   = std::max(0, l.value("flicker", light.flicker));
                light.offset_x  = l.value("offset_x", light.offset_x);
                light.offset_y  = l.value("offset_y", light.offset_y);
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
                light.in_front = l.value("in_front", false);
                light.behind   = l.value("behind", false);
                light.render_to_dark_mask = l.value("render_to_dark_mask", false);
                light.render_front_and_back_to_asset_alpha_mask =
                    l.value("render_front_and_back_to_asset_alpha_mask", false);

                // Legacy key migration: support historical fields once on read.
                if (!l.contains("in_front")) {
                    if (l.contains("front")) {
                        try {
                            light.in_front = l.value("front", false);
                        } catch (...) {
                            light.in_front = false;
                        }
                    }
                }

                if (!l.contains("render_to_dark_mask") && l.contains("dark_mask")) {
                    try {
                        light.render_to_dark_mask = l.value("dark_mask", false);
                    } catch (...) {
                        light.render_to_dark_mask = false;
                    }
                }

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
                return light;
};
        auto append_light = [&](const LightSource& light) {
                info.is_light_source = true;
                info.light_sources.push_back(light);
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
