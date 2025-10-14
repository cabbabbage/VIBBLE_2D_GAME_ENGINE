#include "generate_light.hpp"
#include "cache_manager.hpp"
#include "render_pipeline/ScalingLogic.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <vector>
#include <array>
#include <iostream>
#include <unordered_set>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

static constexpr int kLightCacheVersion = 3;

}  // namespace

namespace {

void clear_light_cache(LightSource& light) {
    std::unordered_set<SDL_Texture*> destroyed;
    auto destroy_texture = [&destroyed](SDL_Texture*& texture) {
        if (texture && destroyed.insert(texture).second) {
            SDL_DestroyTexture(texture);
        }
        texture = nullptr;
    };

    destroy_texture(light.texture);
    light.cached_w = 0;
    light.cached_h = 0;

    for (std::size_t idx = 0; idx < light.cached_variants.size(); ++idx) {
        destroy_texture(light.cached_variants[idx]);
        light.variant_w[idx] = 0;
        light.variant_h[idx] = 0;
    }
}

}  // namespace

GenerateLight::GenerateLight(SDL_Renderer* renderer)
: renderer_(renderer) {}

SDL_Texture* GenerateLight::generate(SDL_Renderer* renderer,
                                     const std::string& asset_name,
                                     LightSource& light,
                                     std::size_t light_index)
{
    if (!renderer) return nullptr;

    clear_light_cache(light);

    const std::string cache_root = "cache/" + asset_name + "/lights";
    const std::string folder     = cache_root + "/" + std::to_string(light_index);
    const std::string meta_file  = folder + "/metadata.json";
    const std::string img_file   = folder + "/light.png";

    const int blur_passes = 0;

    json meta;
    bool cache_ok = false;
    const auto expected_steps = render_pipeline::ScalingLogic::PercentSteps();
    if (CacheManager::load_metadata(meta_file, meta)) {
        try {
            cache_ok =
                meta.value("version", -1) == kLightCacheVersion && meta.value("radius",   -1) == light.radius && meta.value("fall_off", -1) == light.fall_off && meta.value("intensity",-1) == light.intensity && meta.value("blur_passes", -1) == blur_passes && meta.contains("color") && meta["color"].is_array() && meta["color"].size() == 3 && meta["color"][0].get<int>() == light.color.r && meta["color"][1].get<int>() == light.color.g && meta["color"][2].get<int>() == light.color.b;
        } catch (...) {
            cache_ok = false;
        }
    }

    std::array<std::vector<SDL_Surface*>, render_pipeline::ScalingLogic::kDefaultVariantCount> variant_surfaces;

    if (cache_ok) {
        bool variants_loaded = true;
        for (std::size_t idx = 0; idx < render_pipeline::ScalingLogic::kDefaultVariantCount; ++idx) {
            std::string variant_path = render_pipeline::ScalingLogic::VariantFolder(folder, idx);
            std::vector<SDL_Surface*> loaded;
            if (!CacheManager::load_surface_sequence(variant_path, 1, loaded)) {
                variants_loaded = false;
                break;
            }
            variant_surfaces[idx] = std::move(loaded);
        }
        if (variants_loaded && !variant_surfaces[0].empty() && variant_surfaces[0][0]) {
            SDL_Texture* base_tex = nullptr;
            for (std::size_t idx = 0; idx < render_pipeline::ScalingLogic::kDefaultVariantCount; ++idx) {
                SDL_Surface* surface = (!variant_surfaces[idx].empty()) ? variant_surfaces[idx][0] : nullptr;
                SDL_Texture* tex_variant = surface ? CacheManager::surface_to_texture(renderer, surface) : nullptr;
                if (tex_variant) {
                    SDL_SetTextureBlendMode(tex_variant, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
                    SDL_SetTextureScaleMode(tex_variant, SDL_ScaleModeNearest);
#endif
                }
                if (idx == 0) {
                    base_tex = tex_variant;
                    if (tex_variant) {
                        SDL_QueryTexture(tex_variant, nullptr, nullptr, &light.cached_w, &light.cached_h);
                        light.texture = tex_variant;
                        light.cached_variants[0] = tex_variant;
                        light.variant_w[0] = light.cached_w;
                        light.variant_h[0] = light.cached_h;
                    }
                } else {
                    if (tex_variant) {
                        SDL_QueryTexture(tex_variant, nullptr, nullptr, &light.variant_w[idx], &light.variant_h[idx]);
                    } else {
                        light.variant_w[idx] = 0;
                        light.variant_h[idx] = 0;
                    }
                    light.cached_variants[idx] = tex_variant;
                }
            }
            for (auto& list : variant_surfaces) {
                for (SDL_Surface* s : list) {
                    if (s) SDL_FreeSurface(s);
                }
                list.clear();
            }
            if (base_tex) {
                return base_tex;
            }
            clear_light_cache(light);
        } else {
            for (auto& list : variant_surfaces) {
                for (SDL_Surface* s : list) {
                    if (s) SDL_FreeSurface(s);
                }
                list.clear();
            }
        }
    }

    fs::remove_all(folder);
    fs::create_directories(folder);

    const int radius    = std::max(1, light.radius);
    const int falloff   = std::clamp(light.fall_off, 0, 100);
    const SDL_Color col = light.color;
    const int intensity = std::clamp(light.intensity, 0, 255);

    const int size = std::max(1, radius * 2);
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) {
        std::cerr << "[GenerateLight] Failed to create surface: " << SDL_GetError() << "\n";
        return nullptr;
    }
    if (SDL_LockSurface(surf) != 0) {
        std::cerr << "[GenerateLight] Failed to lock surface: " << SDL_GetError() << "\n";
        SDL_FreeSurface(surf);
        return nullptr;
    }

    Uint32* pixels = static_cast<Uint32*>(surf->pixels);
    SDL_PixelFormat* fmt = surf->format;

    const float falloff_norm    = std::clamp(falloff / 100.0f, 0.0f, 1.0f);
    const float fade_exponent   = 0.6f + 3.4f * falloff_norm;
    const float core_ratio      = 0.25f + (1.0f - falloff_norm) * 0.55f;
    const float white_core_r    = radius * std::clamp(core_ratio, 0.05f, 0.95f);

    const float radius_f   = static_cast<float>(radius);
    const float radius_sq  = radius_f * radius_f;
    const float inv_radius = 1.0f / radius_f;

    const float cr = col.r / 255.0f;
    const float cg = col.g / 255.0f;
    const float cb = col.b / 255.0f;

    auto put_px = [&](int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
        pixels[y * size + x] = SDL_MapRGBA(fmt, r, g, b, a);
};

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = x - radius + 0.5f;
            const float dy = y - radius + 0.5f;
            const float d2 = dx*dx + dy*dy;

            if (d2 > radius_sq) { put_px(x,y,0,0,0,0); continue; }

            const float d   = std::sqrt(d2);
            const float base = std::max(0.0f, 1.0f - (d * inv_radius));

            float alpha_ratio = std::pow(base, fade_exponent);
            alpha_ratio = std::clamp(alpha_ratio, 0.0f, 1.0f);

            float brightness = 1.0f;
            if (d > white_core_r) {
                const float t = (d - white_core_r) / std::max(1e-6f, (radius_f - white_core_r));
                brightness = std::clamp(1.0f - t * 0.6f, 0.0f, 1.0f);
            }

            Uint8 a = static_cast<Uint8>(std::min(255.0f, intensity * alpha_ratio * 1.6f));
            Uint8 r = static_cast<Uint8>(std::clamp(cr * brightness * 255.0f, 0.0f, 255.0f));
            Uint8 g = static_cast<Uint8>(std::clamp(cg * brightness * 255.0f, 0.0f, 255.0f));
            Uint8 b = static_cast<Uint8>(std::clamp(cb * brightness * 255.0f, 0.0f, 255.0f));

            put_px(x, y, r, g, b, a);
        }
    }

    SDL_UnlockSurface(surf);

    std::array<SDL_Surface*, render_pipeline::ScalingLogic::kDefaultVariantCount> surfaces{};
    surfaces[0] = surf;
    const auto& default_steps = render_pipeline::ScalingLogic::DefaultScaleSteps();
    for (std::size_t idx = 1; idx < render_pipeline::ScalingLogic::kDefaultVariantCount; ++idx) {
        const float step = (idx < default_steps.size()) ? default_steps[idx] : 1.0f;
        SDL_Surface* scaled_surface = render_pipeline::CreateScaledSurface(surf, step);
        if (!scaled_surface) {
            scaled_surface = render_pipeline::CreateScaledSurface(surf, 1.0f);
        }
        surfaces[idx] = scaled_surface;
    }

    for (std::size_t idx = 0; idx < render_pipeline::ScalingLogic::kDefaultVariantCount; ++idx) {
        std::vector<SDL_Surface*> single;
        if (surfaces[idx]) {
            single.push_back(surfaces[idx]);
            const std::string variant_path = render_pipeline::ScalingLogic::VariantFolder(folder, idx);
            CacheManager::save_surface_sequence(variant_path, single);
        }
    }

    CacheManager::save_surface_as_png(surf, img_file);

    json new_meta;
    new_meta["version"]     = kLightCacheVersion;
    new_meta["radius"]      = light.radius;
    new_meta["fall_off"]    = light.fall_off;
    new_meta["intensity"]   = light.intensity;
    new_meta["blur_passes"] = blur_passes;
    new_meta["color"]       = { col.r, col.g, col.b };
    nlohmann::json steps_json = nlohmann::json::array();
    for (std::size_t idx = 0; idx < render_pipeline::ScalingLogic::kDefaultVariantCount; ++idx) {
        steps_json.push_back(expected_steps[idx]);
    }
    new_meta["scale_steps"] = std::move(steps_json);
    CacheManager::save_metadata(meta_file, new_meta);

    SDL_Texture* base_tex = nullptr;
    for (std::size_t idx = 0; idx < render_pipeline::ScalingLogic::kDefaultVariantCount; ++idx) {
        SDL_Surface* surface = surfaces[idx];
        SDL_Texture* tex_variant = surface ? CacheManager::surface_to_texture(renderer, surface) : nullptr;
        if (tex_variant) {
            SDL_SetTextureBlendMode(tex_variant, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
            SDL_SetTextureScaleMode(tex_variant, SDL_ScaleModeNearest);
#endif
        }
        if (idx == 0) {
            base_tex = tex_variant;
            if (tex_variant) {
                SDL_QueryTexture(tex_variant, nullptr, nullptr, &light.cached_w, &light.cached_h);
                light.texture = tex_variant;
                light.cached_variants[0] = tex_variant;
                light.variant_w[0] = light.cached_w;
                light.variant_h[0] = light.cached_h;
            }
        } else {
            if (tex_variant) {
                SDL_QueryTexture(tex_variant, nullptr, nullptr, &light.variant_w[idx], &light.variant_h[idx]);
            } else {
                light.variant_w[idx] = 0;
                light.variant_h[idx] = 0;
            }
            light.cached_variants[idx] = tex_variant;
        }
    }

    for (SDL_Surface*& surface : surfaces) {
        if (surface) {
            SDL_FreeSurface(surface);
            surface = nullptr;
        }
    }

    if (!base_tex) {
        clear_light_cache(light);
    }

    return base_tex;
}
