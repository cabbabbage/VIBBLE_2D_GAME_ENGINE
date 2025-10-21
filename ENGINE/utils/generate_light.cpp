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

#if defined(GENERATE_LIGHT_ENABLE_TEST_HOOKS)
namespace {
bool g_force_failure = false;
}

namespace generate_light_testing {
void set_force_failure(bool enabled) {
    g_force_failure = enabled;
}

bool force_failure_enabled() {
    return g_force_failure;
}
}  // namespace generate_light_testing
#endif

GenerateLight::GenerateLight(SDL_Renderer* renderer)
: renderer_(renderer) {}

SDL_Texture* GenerateLight::generate(SDL_Renderer* renderer,
                                     const std::string& asset_name,
                                     LightSource& light,
                                     std::size_t light_index)
{
    if (!renderer) return nullptr;

    const std::string cache_root = "cache/" + asset_name + "/lights";
    const std::string folder     = cache_root + "/" + std::to_string(light_index);
    const std::string meta_file  = folder + "/metadata.json";
    const std::string img_file   = folder + "/light.png";

    const int raw_radius = light.radius;
    const int intensity  = std::clamp(light.intensity, 0, 255);
    if (raw_radius <= 0 || intensity <= 0) {
        fs::remove_all(folder);
        std::cerr << "[GenerateLight] Invalid parameters prevented light generation\n";
        return nullptr;
    }

    const int blur_passes = 0;

    json meta;
    bool cache_ok = false;
    constexpr std::size_t kLightVariantCount = 1;
    constexpr std::size_t kLightStorageCount = render_pipeline::ScalingLogic::kDefaultVariantCount;
    constexpr int kLightVariantPercent = 100;
    if (CacheManager::load_metadata(meta_file, meta)) {
        try {
            cache_ok =
                meta.value("version", -1) == kLightCacheVersion && meta.value("radius",   -1) == light.radius && meta.value("fall_off", -1) == light.fall_off && meta.value("intensity",-1) == light.intensity && meta.value("blur_passes", -1) == blur_passes && meta.contains("color") && meta["color"].is_array() && meta["color"].size() == 3 && meta["color"][0].get<int>() == light.color.r && meta["color"][1].get<int>() == light.color.g && meta["color"][2].get<int>() == light.color.b;
            if (cache_ok) {
                if (!meta.contains("scale_steps") || !meta["scale_steps"].is_array()) {
                    cache_ok = false;
                } else {
                    const auto& steps = meta["scale_steps"];
                    if (steps.size() != 1 || !steps[0].is_number_integer() || steps[0].get<int>() != kLightVariantPercent) {
                        cache_ok = false;
                    }
                }
            }
        } catch (...) {
            cache_ok = false;
        }
    }

    std::array<SDL_Texture*, kLightStorageCount> new_variants{};
    std::array<int, kLightStorageCount> new_variant_w{};
    std::array<int, kLightStorageCount> new_variant_h{};
    int new_cached_w = 0;
    int new_cached_h = 0;
    SDL_Texture* new_base = nullptr;

    auto reset_new_state = [&]() {
        std::unordered_set<SDL_Texture*> destroyed;
        for (SDL_Texture*& tex : new_variants) {
            if (tex && destroyed.insert(tex).second) {
                SDL_DestroyTexture(tex);
            }
            tex = nullptr;
        }
        new_variant_w.fill(0);
        new_variant_h.fill(0);
        new_cached_w = 0;
        new_cached_h = 0;
        new_base = nullptr;
    };

    auto load_texture_from_surface = [&](std::size_t idx, SDL_Surface* surface) -> bool {
        if (!surface) {
            if (idx == 0) {
                std::cerr << "[GenerateLight] Missing surface for base light variant\n";
                return false;
            }
            new_variants[idx] = nullptr;
            new_variant_w[idx] = 0;
            new_variant_h[idx] = 0;
            return true;
        }

        SDL_Texture* tex_variant = CacheManager::surface_to_texture(renderer, surface);
        if (!tex_variant) {
            std::cerr << "[GenerateLight] Failed to create texture from surface: " << SDL_GetError() << "\n";
            return false;
        }

        SDL_SetTextureBlendMode(tex_variant, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(tex_variant, SDL_ScaleModeNearest);
#endif
        int tex_w = 0;
        int tex_h = 0;
        SDL_QueryTexture(tex_variant, nullptr, nullptr, &tex_w, &tex_h);
        new_variants[idx] = tex_variant;
        new_variant_w[idx] = tex_w;
        new_variant_h[idx] = tex_h;
        if (idx == 0) {
            new_base = tex_variant;
            new_cached_w = tex_w;
            new_cached_h = tex_h;
        }
        return true;
    };

    auto cleanup_surface_list = [](std::vector<SDL_Surface*>& list) {
        for (SDL_Surface* surface : list) {
            if (surface) {
                SDL_FreeSurface(surface);
            }
        }
        list.clear();
    };

    bool textures_ready = false;

    if (cache_ok) {
        std::array<std::vector<SDL_Surface*>, kLightVariantCount> variant_surfaces;
        bool variants_loaded = true;
        for (std::size_t idx = 0; idx < kLightVariantCount; ++idx) {
            std::string variant_path = render_pipeline::ScalingLogic::VariantFolder(folder, idx);
            std::vector<SDL_Surface*> loaded;
            if (!CacheManager::load_surface_sequence(variant_path, 1, loaded)) {
                variants_loaded = false;
                break;
            }
            variant_surfaces[idx] = std::move(loaded);
        }

        if (variants_loaded && !variant_surfaces[0].empty() && variant_surfaces[0][0]) {
            bool conversion_ok = true;
            for (std::size_t idx = 0; idx < kLightVariantCount; ++idx) {
                SDL_Surface* surface = (!variant_surfaces[idx].empty()) ? variant_surfaces[idx][0] : nullptr;
                if (!load_texture_from_surface(idx, surface)) {
                    conversion_ok = false;
                    break;
                }
            }
            for (auto& list : variant_surfaces) {
                cleanup_surface_list(list);
            }

            if (conversion_ok && new_base) {
                textures_ready = true;
            } else {
                reset_new_state();
            }
        } else {
            for (auto& list : variant_surfaces) {
                cleanup_surface_list(list);
            }
        }
    }

    if (!textures_ready) {
        fs::remove_all(folder);
        fs::create_directories(folder);

        const int radius    = std::max(1, raw_radius);
        const int falloff   = std::clamp(light.fall_off, 0, 100);
        const SDL_Color col = light.color;

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

                if (d2 > radius_sq) {
                    put_px(x, y, 0, 0, 0, 0);
                    continue;
                }

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

        std::array<SDL_Surface*, kLightVariantCount> surfaces{};
        surfaces[0] = surf;

        for (std::size_t idx = 0; idx < kLightVariantCount; ++idx) {
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
        steps_json.push_back(kLightVariantPercent);
        new_meta["scale_steps"] = std::move(steps_json);
        CacheManager::save_metadata(meta_file, new_meta);

        bool conversion_ok = true;
        for (std::size_t idx = 0; idx < kLightVariantCount; ++idx) {
            if (!load_texture_from_surface(idx, surfaces[idx])) {
                conversion_ok = false;
                break;
            }
        }

        for (SDL_Surface*& surface : surfaces) {
            if (surface) {
                SDL_FreeSurface(surface);
                surface = nullptr;
            }
        }

        if (conversion_ok && new_base) {
            textures_ready = true;
        } else {
            reset_new_state();
            return nullptr;
        }
    }

#if defined(GENERATE_LIGHT_ENABLE_TEST_HOOKS)
    if (generate_light_testing::force_failure_enabled()) {
        std::cerr << "[GenerateLight] Forced failure after texture build for testing\n";
        generate_light_testing::set_force_failure(false);
        reset_new_state();
        return nullptr;
    }
#endif

    SDL_Texture* old_texture = light.texture;
    auto old_variants = light.cached_variants;

    light.texture = new_base;
    light.cached_w = new_cached_w;
    light.cached_h = new_cached_h;
    light.cached_variants = new_variants;
    light.variant_w = new_variant_w;
    light.variant_h = new_variant_h;

    std::unordered_set<SDL_Texture*> keep;
    if (light.texture) {
        keep.insert(light.texture);
    }
    for (SDL_Texture* tex : light.cached_variants) {
        if (tex) {
            keep.insert(tex);
        }
    }

    std::unordered_set<SDL_Texture*> destroyed;
    auto destroy_old = [&](SDL_Texture* tex) {
        if (tex && keep.find(tex) == keep.end() && destroyed.insert(tex).second) {
            SDL_DestroyTexture(tex);
        }
    };

    destroy_old(old_texture);
    for (SDL_Texture* tex : old_variants) {
        destroy_old(tex);
    }

    return light.texture;
}
