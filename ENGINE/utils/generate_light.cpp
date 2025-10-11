#include "generate_light.hpp"
#include "cache_manager.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <vector>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

static constexpr int kCacheVersion = 3;

GenerateLight::GenerateLight(SDL_Renderer* renderer)
: renderer_(renderer) {}

SDL_Texture* GenerateLight::generate(SDL_Renderer* renderer,
                                     const std::string& asset_name,
                                     const LightSource& light,
                                     std::size_t light_index)
{
    if (!renderer) return nullptr;

    const std::string cache_root = "cache/" + asset_name + "/lights";
    const std::string folder     = cache_root + "/" + std::to_string(light_index);
    const std::string meta_file  = folder + "/metadata.json";
    const std::string img_file   = folder + "/light.png";

    const int blur_passes = 0;

    json meta;
    bool cache_ok = false;
    if (CacheManager::load_metadata(meta_file, meta)) {
        try {
            cache_ok =
                meta.value("version", -1) == kCacheVersion && meta.value("radius",   -1) == light.radius && meta.value("fall_off", -1) == light.fall_off && meta.value("intensity",-1) == light.intensity && meta.value("blur_passes", -1) == blur_passes && meta.contains("color") && meta["color"].is_array() && meta["color"].size() == 3 && meta["color"][0].get<int>() == light.color.r && meta["color"][1].get<int>() == light.color.g && meta["color"][2].get<int>() == light.color.b;
        } catch (...) {
            cache_ok = false;
        }
    }

    if (cache_ok) {
        if (SDL_Surface* surf = CacheManager::load_surface(img_file)) {
            SDL_Texture* tex = CacheManager::surface_to_texture(renderer, surf);
            SDL_FreeSurface(surf);
            if (tex) {
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
                SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
#endif
                return tex;
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

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    if (!tex) {
        std::cerr << "[GenerateLight] Failed to create texture: " << SDL_GetError() << "\n";
        SDL_FreeSurface(surf);
        return nullptr;
    }
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
#endif

    CacheManager::save_surface_as_png(surf, img_file);
    SDL_FreeSurface(surf);

    json new_meta;
    new_meta["version"]     = kCacheVersion;
    new_meta["radius"]      = light.radius;
    new_meta["fall_off"]    = light.fall_off;
    new_meta["intensity"]   = light.intensity;
    new_meta["blur_passes"] = blur_passes;
    new_meta["color"]       = { col.r, col.g, col.b };
    CacheManager::save_metadata(meta_file, new_meta);

    return tex;
}
