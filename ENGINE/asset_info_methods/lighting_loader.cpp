#include "lighting_loader.hpp"
#include "asset/asset_info.hpp"
#include "utils/generate_light.hpp"
#include <nlohmann/json.hpp>
using nlohmann::json;

void LightingLoader::load(AssetInfo& info, const json& data) {
    info.has_light_source = false;
    info.light_sources.clear();
    info.orbital_light_sources.clear();
    if (!data.contains("lighting_info"))
        return;
    const auto& linfo = data["lighting_info"];
    auto parse_light = [](const json& l) -> std::optional<LightSource> {
        if (!l.is_object() || !l.value("has_light_source", false))
            return std::nullopt;
        LightSource light;
        light.intensity = l.value("light_intensity", 0);
        light.radius    = l.value("radius", 100);
        light.fall_off  = l.value("fall_off", 0);
        light.flare     = l.value("flare", 1);
        light.flicker   = l.value("flicker", 0);
        light.offset_x  = l.value("offset_x", 0);
        light.offset_y  = l.value("offset_y", 0);
        light.x_radius  = l.value("x_radius", 0);
        light.y_radius  = l.value("y_radius", 0);
        double factor   = l.value("factor", 100);
        light.color     = {255, 255, 255, 255};
        factor = factor / 100;
        light.x_radius = light.x_radius * factor;
        light.y_radius = light.y_radius * factor;
        if (l.contains("light_color") && l["light_color"].is_array() && l["light_color"].size() == 3) {
            light.color.r = l["light_color"][0].get<int>();
            light.color.g = l["light_color"][1].get<int>();
            light.color.b = l["light_color"][2].get<int>();
        }
        return light;
    };
    if (linfo.is_array()) {
        for (const auto& l : linfo) {
            auto maybe = parse_light(l);
            if (maybe.has_value()) {
                info.has_light_source = true;
                LightSource light = maybe.value();
                if (light.x_radius > 0 || light.y_radius > 0)
                    info.orbital_light_sources.push_back(light);
                else
                    info.light_sources.push_back(light);
            }
        }
    } else if (linfo.is_object()) {
        auto maybe = parse_light(linfo);
        if (maybe.has_value()) {
            info.has_light_source = true;
            LightSource light = maybe.value();
            if (light.x_radius > 0 || light.y_radius > 0)
                info.orbital_light_sources.push_back(light);
            else
                info.light_sources.push_back(light);
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

