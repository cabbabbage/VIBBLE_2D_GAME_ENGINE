#include "lighting_loader.hpp"
#include "asset/asset_info.hpp"
#include "utils/generate_light.hpp"
#include <cmath>
#include <nlohmann/json.hpp>
using nlohmann::json;

void LightingLoader::load(AssetInfo& info, const json& data) {
        info.has_light_source = false;
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
                light.intensity = l.value("light_intensity", 0);
                light.radius    = l.value("radius", 100);
                light.fall_off  = l.value("fall_off", 0);
                light.flare     = l.value("flare", 1);
                light.flicker   = l.value("flicker", 0);
                int raw_offset_x = l.value("offset_x", 0);
                int raw_offset_y = l.value("offset_y", 0);
                int raw_x_radius = l.value("x_radius", 0);
                int raw_y_radius = l.value("y_radius", 0);
                parsed.factor_percent = l.value("factor", 100);
                const double factor = static_cast<double>(parsed.factor_percent) / 100.0;
                light.offset_x  = static_cast<int>(std::lround(static_cast<double>(raw_offset_x) * factor));
                light.offset_y  = static_cast<int>(std::lround(static_cast<double>(raw_offset_y) * factor));
                light.x_radius  = static_cast<int>(std::lround(static_cast<double>(raw_x_radius) * factor));
                light.y_radius  = static_cast<int>(std::lround(static_cast<double>(raw_y_radius) * factor));
                light.color     = {255, 255, 255, 255};
                if (l.contains("light_color") && l["light_color"].is_array() && l["light_color"].size() == 3) {
                        light.color.r = l["light_color"][0].get<int>();
                        light.color.g = l["light_color"][1].get<int>();
                        light.color.b = l["light_color"][2].get<int>();
                }
                return parsed;
        };
        auto append_light = [&](const ParsedLight& parsed) {
                info.has_light_source = true;
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

void LightingLoader::generate_textures(AssetInfo& info, SDL_Renderer* renderer) {
	GenerateLight generator(renderer);
	for (std::size_t i = 0; i < info.light_sources.size(); ++i) {
		SDL_Texture* tex = generator.generate(renderer, info.name, info.light_sources[i], i);
		if (tex) {
			SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
			info.light_sources[i].texture = tex;
			SDL_QueryTexture(tex, nullptr, nullptr, &info.light_sources[i].cached_w, &info.light_sources[i].cached_h);
		}
	}
	std::size_t base_index = info.light_sources.size();
	for (std::size_t i = 0; i < info.orbital_light_sources.size(); ++i) {
		SDL_Texture* tex = generator.generate(renderer, info.name, info.orbital_light_sources[i], base_index + i);
		if (tex) {
			SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
			info.orbital_light_sources[i].texture = tex;
			SDL_QueryTexture(tex, nullptr, nullptr, &info.orbital_light_sources[i].cached_w, &info.orbital_light_sources[i].cached_h);
		}
	}
}
